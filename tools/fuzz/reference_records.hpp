// Frozen copy of include/cefast/data/records.hpp and the parts of values.hpp and structural.hpp
// it calls, as of commit 27094de (round 7, before the record fast path). Used only by
// tools/fuzz/fuzz_records_diff.cpp as the oracle the fast path must match. Not shipped, not
// included by the library. Keep the copied bodies verbatim except for the namespace and the
// includes (they live inside cefast::reference so both parsers can be linked together).
#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include <fast_float/fast_float.h>

#include <cefast/errors.hpp>
#include <cefast/header/parser.hpp>
#include <cefast/model.hpp>
#include <cefast/time.hpp>

#include <array>
#include <cstddef>
#include <string_view>

namespace cefast::reference::detail
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

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fast_float/fast_float.h>


namespace cefast::reference::detail
{
    // Frozen from include/cefast/header/lexer.hpp: the only helpers records.hpp borrows from it.
    constexpr bool is_space(char c) noexcept
    {
        return c == ' ' || c == '\t' || c == '\r';
    }

    inline std::string_view trim_left(std::string_view text) noexcept
    {
        while (!text.empty() && is_space(text.front()))
            text.remove_prefix(1);
        return text;
    }
}

namespace cefast
{
namespace reference
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
}

[[nodiscard]] inline column reference_make_column(value_type type)
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
}

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>


namespace cefast
{
namespace reference
{
namespace detail
{
    [[nodiscard]] inline std::size_t element_count(const variable& var) noexcept
    {
        std::size_t count = 1;
        for (const auto size : var.sizes)
            count *= size;
        return count;
    }

    [[nodiscard]] inline std::string_view type_name(value_type type) noexcept
    {
        switch (type)
        {
            case value_type::FLOAT:
                return "FLOAT";
            case value_type::DOUBLE:
                return "DOUBLE";
            case value_type::INT:
                return "INT";
            case value_type::BYTE:
                return "BYTE";
            case value_type::ISO_TIME:
                return "ISO_TIME";
            case value_type::ISO_TIME_RANGE:
                return "ISO_TIME_RANGE";
            case value_type::CHAR:
                return "CHAR";
        }
        return "?";
    }

    // CSA writes "!RECORDS= N" right before DATA_UNTIL: a reserve hint only, the count is never
    // trusted (a wrong value must not shift or truncate anything).
    [[nodiscard]] inline std::size_t records_hint(std::string_view data) noexcept
    {
        constexpr std::size_t window = 256;
        const auto tail = data.substr(data.size() > window ? data.size() - window : 0);
        const auto at = tail.rfind("!RECORDS");
        if (at == std::string_view::npos)
            return 0;
        auto rest = tail.substr(at + 8);
        while (
            !rest.empty() && (rest.front() == '=' || rest.front() == ' ' || rest.front() == '\t'))
            rest.remove_prefix(1);
        // A record takes at least one byte, so a hint beyond the data length is wrong; saturating
        // there keeps a corrupt (or hostile) number out of the reservation arithmetic.
        const std::size_t limit = data.size();
        std::size_t value = 0;
        for (const auto c : rest)
        {
            if (c < '0' || c > '9')
                break;
            if (value > limit / 10)
                return limit;
            value = value * 10 + static_cast<std::size_t>(c - '0');
            if (value > limit)
                return limit;
        }
        return value;
    }

    // The terminator is the text starting a line (spec 2.3), not the whole line: "END_DATA ! end"
    // and trailing blanks both end the data section.
    // CEF puts the terminator at the beginning of the last line of the file (§2.4: "the text
    // starting the last line"), so scan from the end: a record whose text happens to contain the
    // terminator is ignored, and the cost is independent of how large the data section is.
    [[nodiscard]] inline std::string_view cut_at_data_until(
        std::string_view data, std::string_view until)
    {
        if (until.empty())
            return data;
        std::size_t line_end = data.size();
        while (line_end > 0)
        {
            std::size_t start = line_end;
            while (start > 0 && data[start - 1] != '\n')
                --start;
            if (trim_left(data.substr(start, line_end - start)).starts_with(until))
                return data.substr(0, start);
            line_end = start == 0 ? 0 : start - 1;
        }
        return data;
    }

    // One slot per value of one record, in record order: the column it belongs to, its type and the
    // variable's fill token. Built once, so every value is appended straight from the input text.
    struct column_state
    {
        column* values;
        value_type type;
        std::string_view fillval;
        // Typed cursor into the pre-sized buffer; the type of the pointees is `type`. null when the
        // column is not pre-sized (CHAR) or not sized yet.
        void* cursor = nullptr;
        void* limit = nullptr;
        bool has_fill = false;
        double fill_number = 0;
        std::int64_t fill_integer = 0;
        // Values this column receives per record (ranges store two per element).
        std::size_t per_record = 1;
    };

    template <typename T>
    [[nodiscard]] inline cpp_utils::containers::no_init_vector<T>& typed(column& values)
    {
        return std::get<cpp_utils::containers::no_init_vector<T>>(values);
    }

    template <typename T>
    inline void set_cursor(column_state& state, std::size_t from)
    {
        auto& values = typed<T>(*state.values);
        state.cursor = values.data() + from;
        state.limit = values.data() + values.size();
    }

    // Sizes the column for `records` records up front so no write has to check the capacity; a file
    // with more records than its hint grows by doubling.
    inline void pre_size(column_state& state, std::size_t records)
    {
        const auto total = records * state.per_record;
        if (total == 0)
            return;
        switch (state.type)
        {
            case value_type::FLOAT:
                typed<float>(*state.values).resize(total);
                set_cursor<float>(state, 0);
                break;
            case value_type::DOUBLE:
                typed<double>(*state.values).resize(total);
                set_cursor<double>(state, 0);
                break;
            case value_type::INT:
                typed<std::int32_t>(*state.values).resize(total);
                set_cursor<std::int32_t>(state, 0);
                break;
            case value_type::BYTE:
                typed<std::uint8_t>(*state.values).resize(total);
                set_cursor<std::uint8_t>(state, 0);
                break;
            case value_type::ISO_TIME:
            case value_type::ISO_TIME_RANGE:
                typed<std::int64_t>(*state.values).resize(total);
                set_cursor<std::int64_t>(state, 0);
                break;
            case value_type::CHAR:
                break;
        }
    }

    inline void finish(column_state& state, std::size_t records)
    {
        const auto total = records * state.per_record;
        switch (state.type)
        {
            case value_type::FLOAT:
                typed<float>(*state.values).resize(total);
                break;
            case value_type::DOUBLE:
                typed<double>(*state.values).resize(total);
                break;
            case value_type::INT:
                typed<std::int32_t>(*state.values).resize(total);
                break;
            case value_type::BYTE:
                typed<std::uint8_t>(*state.values).resize(total);
                break;
            case value_type::ISO_TIME:
            case value_type::ISO_TIME_RANGE:
                typed<std::int64_t>(*state.values).resize(total);
                break;
            case value_type::CHAR:
                break;
        }
    }

    template <typename T>
    inline void grow(column_state& state)
    {
        auto& values = typed<T>(*state.values);
        const auto size = values.size();
        values.resize(size == 0 ? std::max<std::size_t>(state.per_record, 8) : size * 2);
        set_cursor<T>(state, size);
    }

    template <typename T>
    inline void put(column_state& state, T value)
    {
        if (state.cursor == state.limit)
            grow<T>(state);
        *static_cast<T*>(state.cursor) = value;
        state.cursor = static_cast<T*>(state.cursor) + 1;
    }

    // Only numeric and time types short-circuit: a CHAR fill still has to be copied.
    inline void prepare_fill(column_state& state)
    {
        if (state.fillval.empty() || state.type == value_type::CHAR)
            return;
        switch (state.type)
        {
            case value_type::FLOAT:
            {
                float value = 0;
                state.has_fill = parse_float(state.fillval, value);
                state.fill_number = value;
                break;
            }
            case value_type::DOUBLE:
                state.has_fill = parse_float(state.fillval, state.fill_number);
                break;
            case value_type::INT:
            {
                std::int64_t value = 0;
                state.has_fill = parse_int(state.fillval, value)
                    && value >= std::numeric_limits<std::int32_t>::min()
                    && value <= std::numeric_limits<std::int32_t>::max();
                state.fill_integer = value;
                break;
            }
            case value_type::BYTE:
            {
                std::int64_t value = 0;
                state.has_fill = parse_int(state.fillval, value) && value >= 0 && value <= 255;
                state.fill_integer = value;
                break;
            }
            case value_type::ISO_TIME:
            case value_type::ISO_TIME_RANGE:
                // A time fill is a marker, not a number: any text equal to FILLVAL is NaT.
                state.has_fill = true;
                break;
            case value_type::CHAR:
                break;
        }
    }

    // Fill values are short, so the size check decides most tokens and the few equal-length ones
    // compare a few bytes inline instead of paying for a memcmp call.
    [[nodiscard]] inline bool same_text(std::string_view left, std::string_view right) noexcept
    {
        if (left.size() != right.size())
            return false;
        if (right.size() > 16)
            return left == right;
        for (std::size_t index = 0; index < left.size(); ++index)
            if (left[index] != right[index])
                return false;
        return true;
    }

    // One data token into its column. Returns false when the text cannot represent the type: the
    // caller owns the source/line/record context of the error.
    [[nodiscard]] inline bool store(column_state& state, std::string_view token)
    {
        if (state.has_fill && same_text(token, state.fillval))
        {
            switch (state.type)
            {
                case value_type::FLOAT:
                    put(state, static_cast<float>(state.fill_number));
                    return true;
                case value_type::DOUBLE:
                    put(state, state.fill_number);
                    return true;
                case value_type::INT:
                    put(state, static_cast<std::int32_t>(state.fill_integer));
                    return true;
                case value_type::BYTE:
                    put(state, static_cast<std::uint8_t>(state.fill_integer));
                    return true;
                case value_type::ISO_TIME:
                    put(state, nat);
                    return true;
                case value_type::ISO_TIME_RANGE:
                    put(state, nat);
                    put(state, nat);
                    return true;
                case value_type::CHAR:
                    break;
            }
        }
        switch (state.type)
        {
            case value_type::FLOAT:
            {
                float value = 0;
                if (!parse_float(token, value))
                    return false;
                put(state, value);
                return true;
            }
            case value_type::DOUBLE:
            {
                double value = 0;
                if (!parse_float(token, value))
                    return false;
                put(state, value);
                return true;
            }
            case value_type::INT:
            {
                std::int64_t value = 0;
                if (!parse_int(token, value) || value < std::numeric_limits<std::int32_t>::min()
                    || value > std::numeric_limits<std::int32_t>::max())
                    return false;
                put(state, static_cast<std::int32_t>(value));
                return true;
            }
            case value_type::BYTE:
            {
                std::int64_t value = 0;
                if (!parse_int(token, value) || value < 0 || value > 255)
                    return false;
                put(state, static_cast<std::uint8_t>(value));
                return true;
            }
            case value_type::ISO_TIME:
            {
                const auto time = parse_iso_time(token);
                if (!time)
                    return false;
                put(state, *time);
                return true;
            }
            case value_type::ISO_TIME_RANGE:
            {
                const auto range = parse_iso_time_range(token);
                if (!range)
                    return false;
                put(state, (*range)[0]);
                put(state, (*range)[1]);
                return true;
            }
            case value_type::CHAR:
                std::get<std::vector<std::string>>(*state.values).push_back(std::string { token });
                return true;
        }
        return false;
    }

    struct slot
    {
        column_state* state;
    };

    // One pass over the data text: quoted tokens are consumed whole (so separators, markers and '!'
    // inside them are data), unquoted ones are [start, end) ranges of the input, appended to their
    // column as soon as they end. Nothing per value is allocated except a CHAR's own string.
    enum class scan_state
    {
        at_value_start,
        in_unquoted,
        after_value
    };

    class record_scanner
    {
    public:
        record_scanner(schema& result, std::string_view data, std::size_t first_line,
            std::string_view source, std::size_t first_record = 0,
            std::optional<std::size_t> hint = std::nullopt)
                : first_record { first_record }
                , data { data }
                , line { first_line }
                , source { source }
                , marker { result.record_marker }
                , marker_char { result.record_marker.empty() ? '\0' : result.record_marker.front() }
                , single_marker { result.record_marker.size() == 1 }
                , byte_by_byte { result.record_marker.size() != 1 }
        {
            const auto records = hint.value_or(records_hint(data));
            columns.reserve(result.file.variables.size());
            for (auto& var : result.file.variables)
            {
                if (var.is_nrv)
                    continue;
                const auto count = element_count(var);
                var.values = reference_make_column(var.type);
                columns.push_back({ &var.values, var.type, var.fillval_token, nullptr, nullptr,
                    false, 0, 0, count * (var.type == value_type::ISO_TIME_RANGE ? 2 : 1) });
                pre_size(columns.back(), records);
                prepare_fill(columns.back());
                varying.push_back(&var);
                for (std::size_t element = 0; element < count; ++element)
                    slots.push_back({ &columns.back() });
            }
        }

        void run()
        {
            std::size_t position = 0;
            std::size_t token_start = 0;
            while (position < data.size())
            {
                const std::size_t stop
                    = next_stop(data, position, single_marker ? marker_char : '\0', byte_by_byte);
                if (stop > position)
                {
                    if (state == scan_state::after_value)
                        throw parse_error(
                            source, line, "missing separator", first_record + records + 1);
                    if (state == scan_state::at_value_start)
                        token_start = position;
                    state = scan_state::in_unquoted;
                    position = stop;
                    if (position == data.size())
                        break;
                }
                const char c = data[position];
                if (c == '"')
                {
                    if (state != scan_state::at_value_start)
                        throw parse_error(
                            source, line, "missing separator", first_record + records + 1);
                    const auto close = data.find('"', position + 1);
                    if (close == std::string_view::npos)
                        throw parse_error(source, line, "unterminated quote");
                    add(data.substr(position + 1, close - position - 1), true);
                    state = scan_state::after_value;
                    position = close + 1;
                    continue;
                }
                if (c == '!')
                {
                    close_unquoted(position, token_start);
                    const auto newline = data.find('\n', position);
                    position = newline == std::string_view::npos ? data.size() : newline;
                    continue;
                }
                if (single_marker
                        ? c == marker_char
                        : (!marker.empty() && data.compare(position, marker.size(), marker) == 0))
                {
                    close_unquoted(position, token_start);
                    if (marker == "\n")
                        ++line;
                    end_record();
                    position += marker.size();
                    on_marker = position == data.size();
                    continue;
                }
                if (c == ',')
                {
                    if (state == scan_state::at_value_start)
                        throw parse_error(source, line, "empty field", first_record + records + 1);
                    close_unquoted(position, token_start);
                    state = scan_state::at_value_start;
                    ++position;
                    continue;
                }
                if (c == '\n')
                {
                    close_unquoted(position, token_start);
                    ++line;
                    ++position;
                    continue;
                }
                if (is_space(c))
                {
                    // CSA pads columns with runs of spaces (up to a dozen per value): consume the
                    // run here instead of going round the loop once per blank. Only ' ' is
                    // tested, one compare, so single-blank files do not pay for the skip; a tab
                    // or '\r' simply goes round the loop as before.
                    close_unquoted(position, token_start);
                    do
                        ++position;
                    while (position < data.size() && data[position] == ' ');
                    continue;
                }
                if (state == scan_state::after_value)
                    throw parse_error(
                        source, line, "missing separator", first_record + records + 1);
                if (state == scan_state::at_value_start)
                    token_start = position;
                state = scan_state::in_unquoted;
                ++position;
            }
            close_unquoted(data.size(), token_start);
            if (index != 0 || surplus != 0)
                end_record();
            for (auto* var : varying)
                var->records = records;
            for (auto& column : columns)
                finish(column, records);
        }

        // True iff the bytes up to data.size() were consumed by the marker branch: a chunk that
        // ends this way handed the next chunk a clean state (not quoted, not in a comment, at the
        // start of a record), which is what lets the parallel parse trust its chunk boundaries.
        [[nodiscard]] bool ended_on_marker() const noexcept { return on_marker; }

    private:
        void add(std::string_view value, bool quoted)
        {
            if (index == 0 && surplus == 0)
                record_line = line;
            if (index == slots.size())
            {
                // Past the slot table: the record is too long. Count it (so the message stays
                // exact) but do not parse or store it, and never write past the columns.
                ++surplus;
                return;
            }
            auto& target = *slots[index].state;
            if (!store(target, value))
                throw parse_error(source, record_line,
                    "invalid " + std::string { type_name(target.type) }
                        + " value: " + std::string { value },
                    first_record + records + 1);
            ++index;
        }

        void close_unquoted(std::size_t end, std::size_t token_start)
        {
            if (state != scan_state::in_unquoted)
                return;
            add(data.substr(token_start, end - token_start), false);
            state = scan_state::after_value;
        }

        void end_record()
        {
            if (index == 0 && surplus == 0)
            {
                state = scan_state::at_value_start;
                return;
            }
            if (index != slots.size() || surplus != 0)
                throw parse_error(source, record_line,
                    "expected " + std::to_string(slots.size()) + " values, got "
                        + std::to_string(index + surplus),
                    first_record + records + 1);
            ++records;
            index = 0;
            surplus = 0;
            state = scan_state::at_value_start;
        }

        std::size_t first_record;
        std::string_view data;
        std::size_t line;
        std::string_view source;
        std::string_view marker;
        char marker_char;
        bool single_marker;
        bool byte_by_byte;
        std::vector<variable*> varying;
        std::vector<column_state> columns;
        std::vector<slot> slots;
        std::size_t index = 0;
        std::size_t surplus = 0;
        std::size_t records = 0;
        std::size_t record_line = 0;
        bool on_marker = false;
        scan_state state = scan_state::at_value_start;
    };
}

inline void parse_records(
    schema& result, std::string_view data, std::size_t first_line, std::string_view source)
{
    const auto body = detail::cut_at_data_until(data, result.data_until);
    detail::record_scanner { result, body, first_line, source }.run();
}
}
}
