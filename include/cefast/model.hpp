#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <cpp_utils/containers/no_init_vector.hpp>

namespace cefast
{
enum class value_type
{
    FLOAT,
    DOUBLE,
    INT,
    BYTE,
    ISO_TIME,
    ISO_TIME_RANGE,
    CHAR
};

struct iso_time
{
    std::int64_t ns;
};

struct iso_time_range
{
    std::int64_t start;
    std::int64_t stop;
};

// Attribute entries are text unless a VALUE_TYPE says otherwise, so metadata that looks numeric
// (version numbers, leading zeros) keeps its exact spelling.
using scalar = std::variant<std::string, std::int64_t, double, iso_time, iso_time_range>;

struct attribute
{
    std::string name;
    std::vector<scalar> entries;
};

using attributes = std::vector<attribute>;

// One buffer per value type: INT is int32 (CEF's 4-byte integer), BYTE is unsigned (0-255),
// ISO_TIME and ISO_TIME_RANGE share int64 (a range stores its start/stop pairs interleaved).
using column = std::variant<cpp_utils::containers::no_init_vector<float>,
    cpp_utils::containers::no_init_vector<double>,
    cpp_utils::containers::no_init_vector<std::int32_t>,
    cpp_utils::containers::no_init_vector<std::int64_t>,
    cpp_utils::containers::no_init_vector<std::uint8_t>, std::vector<std::string>>;

struct variable
{
    std::string name;
    value_type type = value_type::FLOAT;
    std::vector<std::size_t> sizes;
    bool is_nrv = false;
    std::size_t records = 0;
    column values;
    attributes attrs;
    std::string fillval_token;
};

struct cef_file
{
    attributes globals;
    std::vector<variable> variables;
};
}
