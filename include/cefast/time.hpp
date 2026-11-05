#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace cefast
{
// numpy's NaT for datetime64[ns]: a well-formed ISO time never lands on int64 min.
constexpr std::int64_t nat = std::numeric_limits<std::int64_t>::min();

namespace detail
{
    constexpr bool is_digit(char c) noexcept
    {
        return c >= '0' && c <= '9';
    }

    constexpr int two_digits(std::string_view text, std::size_t at) noexcept
    {
        return (text[at] - '0') * 10 + (text[at + 1] - '0');
    }

    constexpr int four_digits(std::string_view text, std::size_t at) noexcept
    {
        return ((text[at] - '0') * 10 + (text[at + 1] - '0')) * 10 * 10 + (text[at + 2] - '0') * 10
            + (text[at + 3] - '0');
    }

    // YYYY-MM-DDThh:mm:ss[.f+]Z, T and Z case-insensitive (CEF facts Q7). Digits are read at fixed
    // positions: no locale, no allocations, and every rejection is a plain nullopt.
    constexpr std::size_t time_prefix_size = 19;
    constexpr std::array<std::size_t, 14> date_digits { 0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17,
        18 };

    // int64 nanoseconds cover +/-9223372036 seconds; date-invalid values are rejected before this.
    constexpr std::int64_t max_seconds = 9'223'372'036;
    constexpr std::int64_t nanoseconds_per_second = 1'000'000'000;
}

[[nodiscard]] inline std::optional<std::int64_t> parse_iso_time(std::string_view text) noexcept
{
    using namespace std::chrono;
    if (text.size() < detail::time_prefix_size + 1)
        return std::nullopt;
    if (text[4] != '-' || text[7] != '-' || (text[10] != 'T' && text[10] != 't') || text[13] != ':'
        || text[16] != ':')
        return std::nullopt;
    for (const auto position : detail::date_digits)
        if (!detail::is_digit(text[position]))
            return std::nullopt;

    const year_month_day date { year { detail::four_digits(text, 0) },
        month { static_cast<unsigned>(detail::two_digits(text, 5)) },
        day { static_cast<unsigned>(detail::two_digits(text, 8)) } };
    const int hour = detail::two_digits(text, 11);
    const int minute = detail::two_digits(text, 14);
    const int second = detail::two_digits(text, 17);
    if (!date.ok() || hour > 23 || minute > 59 || second > 60)
        return std::nullopt;

    std::size_t position = detail::time_prefix_size;
    std::int64_t fraction = 0;
    if (text[position] == '.')
    {
        ++position;
        std::size_t digits = 0;
        while (position < text.size() && detail::is_digit(text[position]))
        {
            if (digits < 9)
            {
                fraction = fraction * 10 + (text[position] - '0');
                ++digits;
            }
            ++position;
        }
        if (digits == 0)
            return std::nullopt;
        for (; digits < 9; ++digits)
            fraction *= 10;
    }
    if (position + 1 != text.size() || (text[position] != 'Z' && text[position] != 'z'))
        return std::nullopt;

    // A leap second (:60) simply rolls into the next minute, as numpy's datetime64 does.
    const std::int64_t seconds_since_epoch
        = duration_cast<seconds>(sys_days { date }.time_since_epoch()).count() + hour * 3600
        + minute * 60 + second;
    if (seconds_since_epoch > detail::max_seconds || seconds_since_epoch < -detail::max_seconds)
        return std::nullopt;
    return seconds_since_epoch * detail::nanoseconds_per_second + fraction;
}

[[nodiscard]] inline std::optional<std::array<std::int64_t, 2>> parse_iso_time_range(
    std::string_view text) noexcept
{
    const auto separator = text.find('/');
    if (separator == std::string_view::npos)
        return std::nullopt;
    const auto start = parse_iso_time(text.substr(0, separator));
    const auto stop = parse_iso_time(text.substr(separator + 1));
    if (!start || !stop)
        return std::nullopt;
    return std::array<std::int64_t, 2> { *start, *stop };
}
}
