#pragma once
#include <array>
#include <cfloat>
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
    // CEF writes a leading '+', which std::from_chars does not take. One sign only: "+-5" becomes
    // an empty token, which std::from_chars rejects.
    [[nodiscard]] inline std::string_view without_plus(std::string_view token) noexcept
    {
        if (token.empty() || token.front() != '+')
            return token;
        token.remove_prefix(1);
        return !token.empty() && token.front() == '-' ? std::string_view { } : token;
    }

    [[nodiscard]] inline bool parse_int(std::string_view token, std::int64_t& value) noexcept
    {
        token = without_plus(token);
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

    // CEF writes signed samples, and INT already takes the '+' int32 spelling, so the floating
    // types must too: fast_float only accepts it when asked.
    constexpr fast_float::chars_format float_format
        = fast_float::chars_format::general | fast_float::chars_format::allow_leading_plus;

    template <typename T>
    [[nodiscard]] inline bool parse_float(std::string_view token, T& value) noexcept
    {
        if (parse_small_integer(token, value))
            return true;
        const auto result = fast_float::from_chars(
            token.data(), token.data() + token.size(), value, float_format);
        return result.ec == std::errc { } && result.ptr == token.data() + token.size();
    }

    // End-pointer spellings for the record fast path: the parser stops at the first byte that does
    // not belong to the number, and the caller accepts the value only if that byte is a stop byte
    // (or the end of the data). A null return means there is no number to parse at all.
    [[nodiscard]] inline const char* parse_int_prefix(
        std::string_view text, std::int64_t& value) noexcept
    {
        text = without_plus(text);
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc { } || result.ptr == text.data())
            return nullptr;
        return result.ptr;
    }

    // The common CSA spellings, read in one loop: an optional sign and up to 18 digits. Returns
    // the end of the digits, or null for anything else (no digits, 19+ digits, "+-5"...), which
    // the caller hands to std::from_chars. When this accepts, std::from_chars would stop at the
    // same byte with the same value.
    [[nodiscard]] inline const char* parse_plain_int(
        const char* first, const char* last, std::int64_t& value) noexcept
    {
        const bool negative = first != last && *first == '-';
        if (negative || (first != last && *first == '+'))
            ++first;
        const char* digits = first;
        std::uint64_t integer = 0;
        while (first != last && static_cast<unsigned char>(*first - '0') < 10 && first - digits < 19)
            integer = integer * 10 + static_cast<std::uint64_t>(*first++ - '0');
        if (first == digits || first - digits > 18)
            return nullptr;
        value = negative ? -static_cast<std::int64_t>(integer) : static_cast<std::int64_t>(integer);
        return first;
    }

    // Powers of ten that are exact in double; float uses the first 11, which are exact in float too.
    inline constexpr std::array<double, 23> exact_powers_of_ten { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5,
        1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21,
        1e22 };

    // Reads up to four exponent digits after 'e'/'E'. Null when there are none (fast_float then
    // stops before the 'e') or too many.
    [[nodiscard]] inline const char* parse_plain_exponent(
        const char* first, const char* last, int& exponent) noexcept
    {
        const bool negative = first != last && *first == '-';
        if (negative || (first != last && *first == '+'))
            ++first;
        const char* digits = first;
        int value = 0;
        while (first != last && static_cast<unsigned char>(*first - '0') < 10 && first - digits < 5)
            value = value * 10 + (*first++ - '0');
        if (first == digits || first - digits > 4)
            return nullptr;
        exponent = negative ? -value : value;
        return first;
    }

    // [sign]digits[.digits][e[sign]digits] with a mantissa M exact in T and a decimal scale small
    // enough that 10^|scale| is exact in T: then M * 10^scale (or M / 10^-scale) is one correctly
    // rounded IEEE operation, the value fast_float returns (Clinger's fast path,
    // https://doi.org/10.1145/93548.93557). Anything else returns null and goes to fast_float.
    // Leading zeros count as digits, which only sends a few more tokens to fast_float.
    template <typename T>
    [[nodiscard]] inline const char* parse_plain_decimal(
        const char* first, const char* last, T& value) noexcept
    {
#if FLT_EVAL_METHOD != 0
        // x87 evaluates in extended precision: the operation would round twice.
        return nullptr;
#endif
        constexpr int max_digits = std::is_same_v<T, float> ? 7 : 15;
        constexpr int max_scale = std::is_same_v<T, float> ? 10 : 22;
        const bool negative = first != last && *first == '-';
        if (negative || (first != last && *first == '+'))
            ++first;
        std::uint64_t mantissa = 0;
        int digits = 0;
        int fraction = 0;
        for (; first != last && static_cast<unsigned char>(*first - '0') < 10; ++first, ++digits)
            mantissa = mantissa * 10 + static_cast<std::uint64_t>(*first - '0');
        if (first != last && *first == '.')
            for (++first; first != last && static_cast<unsigned char>(*first - '0') < 10;
                ++first, ++digits, ++fraction)
                mantissa = mantissa * 10 + static_cast<std::uint64_t>(*first - '0');
        if (digits == 0 || digits > max_digits)
            return nullptr;
        int exponent = 0;
        if (first != last && (*first == 'e' || *first == 'E'))
        {
            first = parse_plain_exponent(first + 1, last, exponent);
            if (first == nullptr)
                return nullptr;
        }
        const int scale = exponent - fraction;
        if (scale == 0)
        {
            value = negative ? -static_cast<T>(mantissa) : static_cast<T>(mantissa);
            return first;
        }
        if (scale < -max_scale || scale > max_scale)
            return nullptr;
        const auto power = static_cast<T>(exact_powers_of_ten[static_cast<std::size_t>(scale < 0 ? -scale : scale)]);
        const T exact = static_cast<T>(mantissa);
        const T magnitude = scale < 0 ? exact / power : exact * power;
        value = negative ? -magnitude : magnitude;
        return first;
    }

    template <typename T>
    [[nodiscard]] inline const char* parse_float_prefix(std::string_view text, T& value) noexcept
    {
        const auto result
            = fast_float::from_chars(text.data(), text.data() + text.size(), value, float_format);
        if (result.ec != std::errc { } || result.ptr == text.data())
            return nullptr;
        return result.ptr;
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
