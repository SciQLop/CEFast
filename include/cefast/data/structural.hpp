#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace cefast::detail
{
// A stop byte ends an unquoted value or opens a quoted/comment region: after it the scanner
// runs the state machine, between two stops there is only value text to skip.
constexpr std::array<bool, 256> make_stop_table()
{
    std::array<bool, 256> table { };
    for (const char c : { ',', '\n', '"', '!', ' ', '\t', '\r' })
        table[static_cast<unsigned char>(c)] = true;
    return table;
}

inline constexpr auto stop_table = make_stop_table();

constexpr bool is_stop(char c, char marker) noexcept
{
    return stop_table[static_cast<unsigned char>(c)] || c == marker;
}

// Test hook: forces the byte-by-byte traversal so a test can parse the whole corpus through
// both paths and compare every column. The runs-skipping traversal is only a table lookup per
// byte, not a different algorithm, so this is a regression guard rather than a second parser.
inline bool& scan_one_byte_at_a_time() noexcept
{
    static bool value = false;
    return value;
}

// Offset of the first stop byte at or after `from`, or text.size(). `marker` is the single
// character record marker, or '\0' when the marker is multi-character (the caller then walks
// byte by byte, so the generic memcmp path keeps working). CSA files put a stop every ~3 bytes
// on the densest datasets, which is why this is a table lookup per byte: a word-at-a-time
// search would rarely complete a word.
inline std::size_t next_stop(
    std::string_view text, std::size_t from, char marker, bool byte_by_byte) noexcept
{
    if (byte_by_byte || scan_one_byte_at_a_time())
        return from < text.size() ? from : text.size();
    for (std::size_t i = from; i < text.size(); ++i)
        if (is_stop(text[i], marker))
            return i;
    return text.size();
}
}
