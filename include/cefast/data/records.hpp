#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <cefast/data/structural.hpp>
#include <cefast/errors.hpp>
#include <cefast/header/lexer.hpp>
#include <cefast/header/parser.hpp>
#include <cefast/model.hpp>
#include <cefast/values.hpp>

namespace cefast
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
                var.values = make_column(var.type);
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
