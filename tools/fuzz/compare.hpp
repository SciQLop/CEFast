#pragma once
#include <cefast/model.hpp>

#include <cstddef>
#include <cstring>
#include <string>
#include <type_traits>
#include <variant>

// Deep, bit-exact comparison of two parsed files: the parallel path must not change a value, a
// fill or an error message, so "looks the same" means every scalar compared bit for bit.
namespace fuzz
{
inline bool same_scalar(const cefast::scalar& left, const cefast::scalar& right)
{
    if (left.index() != right.index())
        return false;
    return std::visit(
        [&right](const auto& value)
        {
            using type = std::decay_t<decltype(value)>;
            const auto& other = std::get<type>(right);
            if constexpr (std::is_same_v<type, double>)
                return std::memcmp(&value, &other, sizeof(double)) == 0;
            else if constexpr (std::is_same_v<type, cefast::iso_time>)
                return value.ns == other.ns;
            else if constexpr (std::is_same_v<type, cefast::iso_time_range>)
                return value.start == other.start && value.stop == other.stop;
            else
                return value == other;
        },
        left);
}

inline bool same_attribute(const cefast::attribute& left, const cefast::attribute& right)
{
    if (left.name != right.name || left.entries.size() != right.entries.size())
        return false;
    for (std::size_t entry = 0; entry < left.entries.size(); ++entry)
        if (!same_scalar(left.entries[entry], right.entries[entry]))
            return false;
    return true;
}

inline bool same_attributes(const cefast::attributes& left, const cefast::attributes& right)
{
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (!same_attribute(left[index], right[index]))
            return false;
    return true;
}

inline std::size_t column_size(const cefast::column& column)
{
    return std::visit([](const auto& values) { return values.size(); }, column);
}

inline bool same_column(const cefast::column& left, const cefast::column& right)
{
    if (left.index() != right.index())
        return false;
    return std::visit(
        [&right](const auto& values)
        {
            using vector_type = std::decay_t<decltype(values)>;
            const auto& other = std::get<vector_type>(right);
            if (values.size() != other.size())
                return false;
            using value_type = typename vector_type::value_type;
            if constexpr (std::is_same_v<value_type, float>
                || std::is_same_v<value_type, double>)
                return values.empty()
                    || std::memcmp(values.data(), other.data(),
                           values.size() * sizeof(value_type))
                    == 0;
            else
                return values == other;
        },
        left);
}

inline bool same_variable(const cefast::variable& left, const cefast::variable& right)
{
    return left.name == right.name && left.type == right.type && left.sizes == right.sizes
        && left.is_nrv == right.is_nrv && left.records == right.records
        && left.fillval_token == right.fillval_token && same_column(left.values, right.values)
        && same_attributes(left.attrs, right.attrs);
}

inline bool same_file(const cefast::cef_file& left, const cefast::cef_file& right)
{
    if (!same_attributes(left.globals, right.globals)
        || left.variables.size() != right.variables.size())
        return false;
    for (std::size_t index = 0; index < left.variables.size(); ++index)
        if (!same_variable(left.variables[index], right.variables[index]))
            return false;
    return true;
}

// Triage aid: the first place two parsed files differ, small enough to print on a fuzz finding.
inline std::string describe_difference(const cefast::cef_file& left, const cefast::cef_file& right)
{
    if (left.globals.size() != right.globals.size())
        return "globals: " + std::to_string(left.globals.size()) + " vs "
            + std::to_string(right.globals.size());
    for (std::size_t index = 0; index < left.globals.size(); ++index)
        if (left.globals[index].name != right.globals[index].name
            || !same_attribute(left.globals[index], right.globals[index]))
            return "global attribute " + std::to_string(index) + " (" + left.globals[index].name
                + ")";
    if (left.variables.size() != right.variables.size())
        return "variables: " + std::to_string(left.variables.size()) + " vs "
            + std::to_string(right.variables.size());
    for (std::size_t index = 0; index < left.variables.size(); ++index)
    {
        const auto& first = left.variables[index];
        const auto& second = right.variables[index];
        const auto name = std::to_string(index) + " (" + first.name + ")";
        if (first.name != second.name)
            return "variable " + name + " name";
        if (first.type != second.type)
            return "variable " + name + " type";
        if (first.sizes != second.sizes)
            return "variable " + name + " sizes";
        if (first.is_nrv != second.is_nrv || first.records != second.records)
            return "variable " + name + " records: " + std::to_string(first.records) + " vs "
                + std::to_string(second.records);
        if (!same_attributes(first.attrs, second.attrs))
            return "variable " + name + " attributes";
        if (!same_column(first.values, second.values))
            return "variable " + name + " values: " + std::to_string(column_size(first.values))
                + " vs " + std::to_string(column_size(second.values));
    }
    return "no difference found";
}
}
