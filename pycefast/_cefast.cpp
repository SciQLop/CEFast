#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <cefast/cefast.hpp>
#include <cpp_utils/cpp_utils_python/datetime.hpp>
#include <cpp_utils/cpp_utils_python/numpy.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace py = pybind11;

namespace
{
using cpp_utils::containers::no_init_vector;

[[nodiscard]] std::string type_name(cefast::value_type type)
{
    switch (type)
    {
        case cefast::value_type::FLOAT:
            return "FLOAT";
        case cefast::value_type::DOUBLE:
            return "DOUBLE";
        case cefast::value_type::INT:
            return "INT";
        case cefast::value_type::BYTE:
            return "BYTE";
        case cefast::value_type::ISO_TIME:
            return "ISO_TIME";
        case cefast::value_type::ISO_TIME_RANGE:
            return "ISO_TIME_RANGE";
        case cefast::value_type::CHAR:
            return "CHAR";
    }
    return "CHAR";
}

[[nodiscard]] py::object scalar_to_python(const cefast::scalar& value)
{
    return std::visit(
        [](const auto& entry) -> py::object
        {
            using entry_t = std::decay_t<decltype(entry)>;
            if constexpr (std::is_same_v<entry_t, cefast::iso_time>)
                return py::reinterpret_steal<py::object>(cpp_utils::python::to_datetime(entry.ns));
            else if constexpr (std::is_same_v<entry_t, cefast::iso_time_range>)
                return py::make_tuple(
                    py::reinterpret_steal<py::object>(cpp_utils::python::to_datetime(entry.start)),
                    py::reinterpret_steal<py::object>(cpp_utils::python::to_datetime(entry.stop)));
            else
                return py::cast(entry);
        },
        value);
}

// One entry is the value itself, several are a list (metadata keeps its file spelling otherwise).
[[nodiscard]] py::dict attributes_of(const cefast::attributes& attributes)
{
    py::dict result;
    for (const auto& attribute : attributes)
    {
        py::object value;
        if (attribute.entries.size() == 1)
            value = scalar_to_python(attribute.entries.front());
        else
        {
            py::list entries;
            for (const auto& entry : attribute.entries)
                entries.append(scalar_to_python(entry));
            value = std::move(entries);
        }
        result[py::str(attribute.name)] = std::move(value);
    }
    return result;
}

// Named shapes: record-varying data is (records, *SIZES) but a scalar (no SIZES, or SIZES = 1)
// drops the SIZES part; non-record-varying data has no records dimension at all, so SIZES = 1
// really is (1,) and an undeclared SIZES is 0-d. Ranges gain the trailing start/stop axis.
[[nodiscard]] std::vector<py::ssize_t> shape_of(const cefast::variable& var)
{
    const bool scalar = var.sizes.empty() || (var.sizes.size() == 1 && var.sizes.front() == 1);
    std::vector<py::ssize_t> shape;
    if (!var.is_nrv)
        shape.push_back(static_cast<py::ssize_t>(var.records));
    if (var.is_nrv || !scalar)
        for (const auto size : var.sizes)
            shape.push_back(static_cast<py::ssize_t>(size));
    if (var.type == cefast::value_type::ISO_TIME_RANGE)
        shape.push_back(2);
    return shape;
}

template <typename Container>
[[nodiscard]] py::array view_of(
    Container& values, const std::vector<py::ssize_t>& shape, std::shared_ptr<const void> owner)
{
    return cpp_utils::python::view_with_owner(values.data(), shape, std::move(owner));
}

[[nodiscard]] py::array char_values(
    const std::vector<std::string>& values, const std::vector<py::ssize_t>& shape)
{
    py::list items;
    for (const auto& value : values)
        items.append(py::str(value));
    py::object array = py::module_::import("numpy").attr("array")(items, py::arg("dtype") = "str");
    return py::reinterpret_borrow<py::array>(array.attr("reshape")(py::cast(shape)));
}

// Numeric and time columns are zero-copy views whose capsule keeps the parsed file alive; CHAR is
// variable-width and copied into a fixed-width numpy string array.
[[nodiscard]] py::array values_of(const std::shared_ptr<cefast::cef_file>& file, std::size_t index)
{
    cefast::variable& var = file->variables[index];
    const auto shape = shape_of(var);
    const std::shared_ptr<const void> owner = file;
    switch (var.type)
    {
        case cefast::value_type::FLOAT:
            return view_of(std::get<no_init_vector<float>>(var.values), shape, owner);
        case cefast::value_type::DOUBLE:
            return view_of(std::get<no_init_vector<double>>(var.values), shape, owner);
        case cefast::value_type::INT:
            return view_of(std::get<no_init_vector<std::int32_t>>(var.values), shape, owner);
        case cefast::value_type::BYTE:
            return view_of(std::get<no_init_vector<std::uint8_t>>(var.values), shape, owner);
        case cefast::value_type::ISO_TIME:
        case cefast::value_type::ISO_TIME_RANGE:
            return cpp_utils::python::as_datetime64_ns(
                view_of(std::get<no_init_vector<std::int64_t>>(var.values), shape, owner));
        case cefast::value_type::CHAR:
            return char_values(std::get<std::vector<std::string>>(var.values), shape);
    }
    return { };
}

struct py_variable
{
    std::shared_ptr<cefast::cef_file> file;
    std::size_t index = 0;

    [[nodiscard]] cefast::variable& var() const { return file->variables[index]; }
    [[nodiscard]] std::string name() const { return var().name; }
    [[nodiscard]] std::string value_type() const { return type_name(var().type); }
    [[nodiscard]] bool is_nrv() const { return var().is_nrv; }
    [[nodiscard]] py::dict attributes() const { return attributes_of(var().attrs); }
    [[nodiscard]] py::array values() const { return values_of(file, index); }

    [[nodiscard]] py::tuple sizes() const
    {
        py::tuple result { var().sizes.size() };
        for (std::size_t size_index = 0; size_index < var().sizes.size(); ++size_index)
            result[size_index] = py::int_(var().sizes[size_index]);
        return result;
    }

    [[nodiscard]] std::string repr() const
    {
        return "<cefast.Variable '" + var().name + "' " + type_name(var().type)
            + (var().is_nrv ? " non-record-varying>" : " record-varying>");
    }
};

struct py_cef
{
    std::shared_ptr<cefast::cef_file> file;

    [[nodiscard]] py_variable getitem(const std::string& name) const
    {
        for (std::size_t index = 0; index < file->variables.size(); ++index)
            if (file->variables[index].name == name)
                return py_variable { file, index };
        throw py::key_error(name);
    }

    [[nodiscard]] bool contains(const std::string& name) const
    {
        for (const auto& var : file->variables)
            if (var.name == name)
                return true;
        return false;
    }

    [[nodiscard]] std::size_t len() const { return file->variables.size(); }
    [[nodiscard]] py::dict attributes() const { return attributes_of(file->globals); }

    [[nodiscard]] py::list keys() const
    {
        py::list names;
        for (const auto& var : file->variables)
            names.append(py::str(var.name));
        return names;
    }

    [[nodiscard]] std::string repr() const
    {
        return "<cefast.CEF with " + std::to_string(file->variables.size()) + " variables>";
    }
};

// The resolver runs while the parser holds no GIL, so every call re-acquires it; the returned
// std::function outlives the released-GIL scope, so the Python callable is released under it.
[[nodiscard]] std::function<std::optional<std::string>(std::string_view)> make_resolver(
    const py::object& resolver)
{
    if (resolver.is_none())
        return { };
    auto callable = std::make_shared<py::object>(resolver);
    return [callable](std::string_view name) -> std::optional<std::string>
    {
        const py::gil_scoped_acquire acquire;
        const py::object result = (*callable)(py::str(name));
        if (result.is_none())
            return std::nullopt;
        return py::cast<std::string>(result);
    };
}

[[nodiscard]] cefast::cef_file load_source(const py::object& source,
    const std::vector<std::filesystem::path>& include_paths, const py::object& resolver)
{
    cefast::load_options options;
    options.include_paths = include_paths;
    options.include_resolver = make_resolver(resolver);

    cefast::cef_file file;
    if (py::isinstance<py::str>(source))
    {
        const auto path = py::cast<std::filesystem::path>(source);
        const py::gil_scoped_release release;
        file = cefast::load(path, options);
        return file;
    }
    if (py::isinstance<py::bytes>(source))
    {
        const auto buffer = py::reinterpret_borrow<py::buffer>(source);
        const py::buffer_info info = buffer.request();
        const auto size
            = static_cast<std::size_t>(info.size) * static_cast<std::size_t>(info.itemsize);
        const auto* data = static_cast<const char*>(info.ptr);
        const py::gil_scoped_release release;
        file = cefast::load(std::span<const char> { data, size }, options);
        return file;
    }
    if (py::isinstance<py::bytearray>(source) || py::isinstance<py::memoryview>(source))
    {
        Py_buffer view { };
        if (PyObject_GetBuffer(source.ptr(), &view, PyBUF_CONTIG_RO) != 0)
            throw py::error_already_set();
        const std::span<const char> bytes { static_cast<const char*>(view.buf),
            static_cast<std::size_t>(view.len) };
        try
        {
            const py::gil_scoped_release release;
            file = cefast::load(bytes, options);
        }
        catch (...)
        {
            PyBuffer_Release(&view);
            throw;
        }
        PyBuffer_Release(&view);
        return file;
    }
    const auto path = py::cast<std::filesystem::path>(source);
    const py::gil_scoped_release release;
    file = cefast::load(path, options);
    return file;
}
}

PYBIND11_MODULE(_cefast, m)
{
    m.doc() = "Fast reader for ESA's Cluster Exchange Format (CEF 2.0)";

    py::register_exception<cefast::parse_error>(m, "ParseError", PyExc_ValueError);

    py::class_<py_cef>(m, "CEF")
        .def("__getitem__", &py_cef::getitem, py::arg("name"))
        .def("__contains__", &py_cef::contains, py::arg("name"))
        .def("__iter__", [](const py_cef& self) { return py::iter(self.keys()); })
        .def("__len__", &py_cef::len)
        .def("keys", &py_cef::keys)
        .def_property_readonly("attributes", &py_cef::attributes)
        .def("__repr__", &py_cef::repr);

    py::class_<py_variable>(m, "Variable")
        .def_property_readonly("name", &py_variable::name)
        .def_property_readonly("value_type", &py_variable::value_type)
        .def_property_readonly("sizes", &py_variable::sizes)
        .def_property_readonly("is_nrv", &py_variable::is_nrv)
        .def_property_readonly("attributes", &py_variable::attributes)
        .def_property_readonly("values", &py_variable::values)
        .def("__repr__", &py_variable::repr);

    m.def(
        "load",
        [](const py::object& source, const std::vector<std::filesystem::path>& include_paths,
            const py::object& resolver)
        {
            return py_cef { std::make_shared<cefast::cef_file>(
                load_source(source, include_paths, resolver)) };
        },
        py::arg("source"), py::kw_only(), py::arg("include_paths") = py::tuple { },
        py::arg("include_resolver") = py::none(),
        "Load a CEF file from a path, bytes, bytearray or memoryview.");
}
