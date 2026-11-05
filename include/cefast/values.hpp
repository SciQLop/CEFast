#pragma once
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fast_float/fast_float.h>

#include <cefast/model.hpp>
#include <cefast/time.hpp>

namespace cefast
{
namespace detail
{
    [[nodiscard]] inline bool parse_int(std::string_view token, std::int64_t& value) noexcept
    {
        if (!token.empty() && token.front() == '+')
            token.remove_prefix(1);
        const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
        return result.ec == std::errc { } && result.ptr == token.data() + token.size();
    }

    // Up to four decimal digits fit an unsigned value that is exact in float and double, and CSA
    // writes most samples as short integers, so this skips fast_float entirely. Longer tokens take
    // the one size compare and go to fast_float.
    template <typename T>
    [[nodiscard]] inline bool parse_small_integer(std::string_view token, T& value) noexcept
    {
        if (token.empty() || token.size() > 4)
            return false;
        std::uint32_t integer = 0;
        for (const char digit : token)
        {
            if (digit < '0' || digit > '9')
                return false;
            integer = integer * 10 + static_cast<std::uint32_t>(digit - '0');
        }
        value = static_cast<T>(integer);
        return true;
    }

    template <typename T>
    [[nodiscard]] inline bool parse_float(std::string_view token, T& value) noexcept
    {
        if (parse_small_integer(token, value))
            return true;
        const auto result
            = fast_float::from_chars(token.data(), token.data() + token.size(), value);
        return result.ec == std::errc { } && result.ptr == token.data() + token.size();
    }
}

[[nodiscard]] inline column make_column(value_type type)
{
    using namespace cpp_utils::containers;
    switch (type)
    {
        case value_type::FLOAT:
            return no_init_vector<float> { };
        case value_type::DOUBLE:
            return no_init_vector<double> { };
        case value_type::INT:
            return no_init_vector<std::int32_t> { };
        case value_type::BYTE:
            return no_init_vector<std::uint8_t> { };
        case value_type::ISO_TIME:
        case value_type::ISO_TIME_RANGE:
            return no_init_vector<std::int64_t> { };
        case value_type::CHAR:
            return std::vector<std::string> { };
    }
    return std::vector<std::string> { };
}

// One data token into its typed column, in record order. Returns false when the text cannot
// represent the type: the caller owns the source/line/record context of the error.
[[nodiscard]] inline bool append_value(
    column& values, value_type type, std::string_view token, std::string_view fillval_token)
{
    using namespace cpp_utils::containers;
    switch (type)
    {
        case value_type::FLOAT:
        {
            float value = 0;
            if (!detail::parse_float(token, value))
                return false;
            std::get<no_init_vector<float>>(values).push_back(value);
            return true;
        }
        case value_type::DOUBLE:
        {
            double value = 0;
            if (!detail::parse_float(token, value))
                return false;
            std::get<no_init_vector<double>>(values).push_back(value);
            return true;
        }
        case value_type::INT:
        {
            std::int64_t value = 0;
            if (!detail::parse_int(token, value) || value < std::numeric_limits<std::int32_t>::min()
                || value > std::numeric_limits<std::int32_t>::max())
                return false;
            std::get<no_init_vector<std::int32_t>>(values).push_back(
                static_cast<std::int32_t>(value));
            return true;
        }
        case value_type::BYTE:
        {
            std::int64_t value = 0;
            if (!detail::parse_int(token, value) || value < 0 || value > 255)
                return false;
            std::get<no_init_vector<std::uint8_t>>(values).push_back(
                static_cast<std::uint8_t>(value));
            return true;
        }
        case value_type::ISO_TIME:
        {
            auto& times = std::get<no_init_vector<std::int64_t>>(values);
            if (!fillval_token.empty() && token == fillval_token)
            {
                times.push_back(nat);
                return true;
            }
            const auto time = parse_iso_time(token);
            if (!time)
                return false;
            times.push_back(*time);
            return true;
        }
        case value_type::ISO_TIME_RANGE:
        {
            auto& times = std::get<no_init_vector<std::int64_t>>(values);
            if (!fillval_token.empty() && token == fillval_token)
            {
                times.push_back(nat);
                times.push_back(nat);
                return true;
            }
            const auto range = parse_iso_time_range(token);
            if (!range)
                return false;
            times.push_back((*range)[0]);
            times.push_back((*range)[1]);
            return true;
        }
        case value_type::CHAR:
            std::get<std::vector<std::string>>(values).push_back(std::string { token });
            return true;
    }
    return false;
}
}
