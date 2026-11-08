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
// drops the SIZES part; non-record-varying data has no records dimension at all, so a scalar is
// (1,) whether the producer wrote SIZES = 1 or nothing (CEFLIB's convention too). Ranges gain the
// trailing start/stop axis.
[[nodiscard]] std::vector<py::ssize_t> shape_of(const cefast::variable& var)
{
    const bool scalar = var.sizes.empty() || (var.sizes.size() == 1 && var.sizes.front() == 1);
    std::vector<py::ssize_t> shape;
    if (var.is_nrv)
    {
        if (var.sizes.empty())
            shape.push_back(1);
        else
            for (const auto size : var.sizes)
                shape.push_back(static_cast<py::ssize_t>(size));
    }
    else
    {
        shape.push_back(static_cast<py::ssize_t>(var.records));
        if (!scalar)
            for (const auto size : var.sizes)
                shape.push_back(static_cast<py::ssize_t>(size));
    }
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

    [[nodiscard]] py::tuple shape() const
    {
        const auto dims = shape_of(var());
        py::tuple result { dims.size() };
        for (std::size_t dim = 0; dim < dims.size(); ++dim)
            result[dim] = py::int_(dims[dim]);
        return result;
    }
};

struct py_cef
{
    std::shared_ptr<cefast::cef_file> file;

    // Keys of any type, like a dict: a key that is not text is simply not there.
    [[nodiscard]] std::optional<std::size_t> find(const py::handle& key) const
    {
        if (!py::isinstance<py::str>(key))
            return std::nullopt;
        const auto name = key.cast<std::string>();
        for (std::size_t index = 0; index < file->variables.size(); ++index)
            if (file->variables[index].name == name)
                return index;
        return std::nullopt;
    }

    [[nodiscard]] py_variable getitem(const py::object& key) const
    {
        if (const auto index = find(key))
            return py_variable { file, *index };
        PyErr_SetObject(PyExc_KeyError, key.ptr());
        throw py::error_already_set();
    }

    [[nodiscard]] bool contains(const py::object& key) const { return find(key).has_value(); }

    [[nodiscard]] std::size_t len() const { return file->variables.size(); }
    [[nodiscard]] py::dict attributes() const { return attributes_of(file->globals); }

    [[nodiscard]] py::list keys() const
    {
        py::list names;
        for (const auto& var : file->variables)
            names.append(py::str(var.name));
        return names;
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
    const std::vector<std::filesystem::path>& include_paths, const py::object& resolver,
    int threads)
{
    cefast::load_options options;
    options.include_paths = include_paths;
    options.include_resolver = make_resolver(resolver);
    options.threads = threads > 0 ? static_cast<std::size_t>(threads) : 0;

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

    // pybind11 looks numpy's C API up once, releasing and re-acquiring the GIL around it. Under
    // Pyodide with JSPI, that re-acquire crashed ("memory access out of bounds") when the first
    // array was made in a later runPythonAsync call than the import; at import time it is safe,
    // as register_exception's own one-time setup below shows. So look it up now.
    (void)py::dtype::of<float>();

    auto parse_error
        = py::register_exception<cefast::parse_error>(m, "ParseError", PyExc_ValueError);
    parse_error.attr("__doc__") = R"(A malformed CEF file.

The message names the file, the line and, inside the data section, the record::

    broken.cef:214: expected 31 values, got 30 (record 12)

A subclass of :class:`ValueError`.
)";

    py::class_<py_cef>(m, "CEF", R"(A loaded CEF file, returned by :func:`load`.

A read-only :class:`collections.abc.Mapping` of :class:`Variable`, in file order::

    f = cefast.load("file.cef")
    len(f), list(f), "B_mag" in f
    f["B_mag"].values
)")
        .def("__getitem__", &py_cef::getitem, py::arg("name"),
            "The variable with that name. Raises :class:`KeyError` when there is none.")
        .def("__contains__", &py_cef::contains, py::arg("name"))
        .def("__iter__", [](const py_cef& self) { return py::iter(self.keys()); })
        .def("__len__", &py_cef::len)
        .def("keys", &py_cef::keys, R"(The variable names, in file order.

Returns
-------
list of str
)")
        .def_property_readonly("attributes", &py_cef::attributes,
            R"(The global attributes: the file's metadata blocks and global entries.

A new :class:`dict` on each access. A single entry is the value itself, several entries are a
:class:`list`. Text and numbers come as :class:`str`, :class:`int` and :class:`float`, an
``ISO_TIME`` as :class:`datetime.datetime`, an ``ISO_TIME_RANGE`` as a pair of them.
)");

    py::class_<py_variable>(m, "Variable", R"(One variable of a :class:`CEF` file.

Get it with ``f[name]``. It keeps the file's data alive on its own.
)")
        .def_property_readonly(
            "name", &py_variable::name, "The variable name, as written in the file.")
        .def_property_readonly("value_type", &py_variable::value_type,
            "The CEF ``VALUE_TYPE``: ``'FLOAT'``, ``'DOUBLE'``, ``'INT'``, ``'BYTE'``, ``'CHAR'``, "
            "``'ISO_TIME'`` or ``'ISO_TIME_RANGE'``.")
        .def_property_readonly("shape", &py_variable::shape,
            "The shape of :attr:`values`, without building the array: ``(records, *sizes)``, "
            "``(records,)`` for a scalar, ``sizes`` for a non-record-varying variable.")
        .def_property_readonly("sizes", &py_variable::sizes,
            "The CEF ``SIZES`` attribute as a tuple of ints, ``()`` when the file gives none.")
        .def_property_readonly("is_nrv", &py_variable::is_nrv,
            "True for a non-record-varying variable: its values are in the header (``DATA``), "
            "not in each record.")
        .def_property_readonly("attributes", &py_variable::attributes,
            R"(The variable attributes other than ``VALUE_TYPE``, ``SIZES`` and ``DATA``.

A new :class:`dict` on each access, with the same value rules as :attr:`CEF.attributes`.
``FILLVAL`` is a number for numeric variables.
)")
        .def_property_readonly("values", &py_variable::values,
            R"(The values, as a :class:`numpy.ndarray`.

A record-varying variable has shape ``(records, *sizes)``, and ``(records,)`` when it is a
scalar. A non-record-varying variable has shape ``sizes``. An ``ISO_TIME_RANGE`` gets a last axis
of 2 (start, stop).

==================  ======================================
``value_type``      dtype
==================  ======================================
``FLOAT``           ``float32``
``DOUBLE``          ``float64``
``INT``             ``int32``
``BYTE``            ``uint8``
``ISO_TIME``        ``datetime64[ns]``
``ISO_TIME_RANGE``  ``datetime64[ns]``
``CHAR``            numpy unicode string
==================  ======================================

Numeric and time arrays are views on the parsed data, not copies: writing into one changes what
later accesses return. ``CHAR`` arrays are copied on each access. Fill values stay as written: the ``FILLVAL`` number for numeric
variables, ``NaT`` for times.
)");

    m.def(
        "load",
        [](const py::object& source, const std::vector<std::filesystem::path>& include_paths,
            const py::object& resolver, int threads)
        {
            return py_cef { std::make_shared<cefast::cef_file>(
                load_source(source, include_paths, resolver, threads)) };
        },
        py::arg("source"), py::kw_only(), py::arg("include_paths") = py::tuple { },
        py::arg("include_resolver") = py::none(), py::arg("threads") = 0,
        R"(Load a CEF 2.0 file.

Parameters
----------
source : str, os.PathLike, bytes, bytearray or memoryview
    A path, or the content of a file. Gzip is detected from the content, not the name.
include_paths : sequence of str or os.PathLike, optional
    Directories searched for ``INCLUDE`` files, after the directory of the file being read.
include_resolver : callable, optional
    Called first with each ``INCLUDE`` name. Returns the included text as a :class:`str`, or
    ``None`` to fall back to the directories.
threads : int, optional
    ``0`` (the default) picks a count from the machine, ``1`` parses serially, ``N`` uses at
    most ``N`` threads. The result is the same either way.

Returns
-------
CEF

Raises
------
ParseError
    The file cannot be read, or is malformed. The message names the file, the line and, in the
    data section, the record.

Notes
-----
The GIL is released while the file is parsed. ``INCLUDE`` names are looked up with
``include_resolver``, then in the directory of the file, then in ``include_paths``, then in the
``CEFPATH`` environment variable, then in the current directory.
)");
}
