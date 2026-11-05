#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

#if defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__wasm_simd128__)
#include <wasm_simd128.h>
#endif

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
    // The first and last representable instants are not whole seconds, so the seconds boundary
    // alone would let a fraction overflow int64.
    constexpr std::int64_t max_seconds_fraction
        = std::numeric_limits<std::int64_t>::max() % nanoseconds_per_second;

    // CSA writes YYYY-MM-DDThh:mm:ss.fffZ: 24 bytes with digits at fixed positions. The fast path
    // below and the general one must accept exactly the same strings, so the tests run both over
    // the corpus and over every one-byte mutation of a sample.
    constexpr std::size_t fixed_time_size = 24;
    constexpr std::uint32_t fixed_time_digits_at = 0xdb6f; // bytes 0..15, bit i = byte i is a digit
    constexpr std::uint32_t fixed_time_digits_from_8 = 0x76db; // bytes 8..23

    // Test hook: false forces the general path.
    inline bool& fixed_time_path() noexcept
    {
        static bool value = true;
        return value;
    }

#if defined(__ARM_NEON)
    // Packs the sign bit of each of eight lanes into a byte-sized mask.
    inline std::uint32_t movemask_8(uint8x8_t bits) noexcept
    {
        const std::uint64_t packed = vget_lane_u64(vreinterpret_u64_u8(bits), 0);
        return static_cast<std::uint32_t>(
            ((packed >> 7) & 0x0101010101010101ULL) * 0x0102040810204080ULL >> 56);
    }
#endif

    // Bit i is set when byte i is a decimal digit. The scalar twin is the loop in the last branch.
    inline std::uint32_t digit_mask_16(const char* bytes) noexcept
    {
#if defined(__SSE2__)
        const __m128i text = _mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes));
        const __m128i after_zero = _mm_cmpgt_epi8(text, _mm_set1_epi8('0' - 1));
        const __m128i before_ten = _mm_cmplt_epi8(text, _mm_set1_epi8('9' + 1));
        return static_cast<std::uint32_t>(_mm_movemask_epi8(_mm_and_si128(after_zero, before_ten)));
#elif defined(__ARM_NEON)
        const uint8x16_t text = vld1q_u8(reinterpret_cast<const std::uint8_t*>(bytes));
        const uint8x16_t digits = vandq_u8(vcgeq_u8(text, vdupq_n_u8('0')),
            vcleq_u8(text, vdupq_n_u8('9')));
        return movemask_8(vget_low_u8(digits))
            | (movemask_8(vget_high_u8(digits)) << 8);
#elif defined(__wasm_simd128__)
        const v128_t text = wasm_v128_load(bytes);
        const v128_t digits = wasm_v128_and(wasm_u8x16_ge(text, wasm_u8x16_splat('0')),
            wasm_u8x16_le(text, wasm_u8x16_splat('9')));
        return wasm_i8x16_bitmask(digits);
#else
        std::uint32_t mask = 0;
        for (std::uint32_t index = 0; index < 16; ++index)
            mask |= static_cast<std::uint32_t>(is_digit(bytes[index])) << index;
        return mask;
#endif
    }

    // Fixed positions that the SIMD check already proved to be digits, read without a string_view.
    constexpr int two_digits_at(const char* bytes, std::size_t at) noexcept
    {
        return (bytes[at] - '0') * 10 + (bytes[at + 1] - '0');
    }

    constexpr int four_digits_at(const char* bytes, std::size_t at) noexcept
    {
        return two_digits_at(bytes, at) * 100 + two_digits_at(bytes, at + 2);
    }

    constexpr bool is_leap_year(int year) noexcept
    {
        return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    }

    inline constexpr std::array<int, 12> month_lengths { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31,
        30, 31 };
    // Days before the first of each month counted from March 1, the year start days_from_civil
    // uses. Leap days need no entry here: yoe / 4 - yoe / 100 counts them within the era.
    inline constexpr std::array<unsigned, 12> days_before_march { 306, 337, 0, 31, 61, 92, 122, 153,
        184, 214, 245, 275 };

    constexpr int days_in_month(int year, int month) noexcept
    {
        if (month == 2 && is_leap_year(year))
            return 29;
        return month_lengths[static_cast<std::size_t>(month - 1)];
    }

    // Howard Hinnant's days_from_civil: a few divisions instead of the chrono calendar tables.
    // Years are 0..9999 here, so the unsigned form is enough and skips the sign fixups.
    constexpr std::int64_t days_from_civil(int calendar_year, int month, int day) noexcept
    {
        const unsigned year = static_cast<unsigned>(calendar_year) - (month <= 2 ? 1u : 0u);
        const unsigned era = year / 400;
        const unsigned year_of_era = year - era * 400;
        const unsigned day_of_year = days_before_march[static_cast<std::size_t>(month - 1)]
            + static_cast<unsigned>(day - 1);
        const unsigned day_of_era
            = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
        return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(day_of_era)
            - 719468;
    }

    // Nullopt for everything the general path also rejects; the caller then runs that path.
    inline std::optional<std::int64_t> parse_fixed_time(std::string_view text) noexcept
    {
        const char* bytes = text.data();
        if (digit_mask_16(bytes) != fixed_time_digits_at
            || digit_mask_16(bytes + 8) != fixed_time_digits_from_8)
            return std::nullopt;
        if (bytes[4] != '-' || bytes[7] != '-' || (bytes[10] != 'T' && bytes[10] != 't')
            || bytes[13] != ':' || bytes[16] != ':' || bytes[19] != '.'
            || (bytes[23] != 'Z' && bytes[23] != 'z'))
            return std::nullopt;

        const int year = four_digits_at(bytes, 0);
        const int month = two_digits_at(bytes, 5);
        const int day = two_digits_at(bytes, 8);
        const int hour = two_digits_at(bytes, 11);
        const int minute = two_digits_at(bytes, 14);
        const int second = two_digits_at(bytes, 17);
        if (month < 1 || month > 12 || day < 1 || day > days_in_month(year, month) || hour > 23
            || minute > 59 || second > 60)
            return std::nullopt;

        const std::int64_t fraction
            = ((bytes[20] - '0') * 100 + (bytes[21] - '0') * 10 + (bytes[22] - '0')) * 1'000'000;
        const std::int64_t seconds = days_from_civil(year, month, day) * 86400 + hour * 3600
            + minute * 60 + second;
        if (seconds > max_seconds || seconds < -max_seconds)
            return std::nullopt;
        if (seconds == max_seconds && fraction > max_seconds_fraction)
            return std::nullopt;
        return seconds * nanoseconds_per_second + fraction;
    }
}

// The general path is the scalar twin of the fast one: it reads any YYYY-MM-DDThh:mm:ss[.f+]Z.
[[nodiscard]] inline std::optional<std::int64_t> parse_iso_time_general(
    std::string_view text) noexcept
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

    // Leap seconds are ignored, as in numpy's datetime64: a :60 second rolls over into the next
    // minute by plain arithmetic.
    const std::int64_t seconds_since_epoch
        = duration_cast<seconds>(sys_days { date }.time_since_epoch()).count() + hour * 3600
        + minute * 60 + second;
    if (seconds_since_epoch > detail::max_seconds || seconds_since_epoch < -detail::max_seconds)
        return std::nullopt;
    if (seconds_since_epoch == detail::max_seconds && fraction > detail::max_seconds_fraction)
        return std::nullopt;
    return seconds_since_epoch * detail::nanoseconds_per_second + fraction;
}

[[nodiscard]] inline std::optional<std::int64_t> parse_iso_time(std::string_view text) noexcept
{
    if (detail::fixed_time_path() && text.size() == detail::fixed_time_size)
        if (const auto fixed = detail::parse_fixed_time(text))
            return fixed;
    return parse_iso_time_general(text);
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
