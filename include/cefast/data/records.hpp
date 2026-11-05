#pragma once
#include <cstddef>
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
        std::size_t value = 0;
        std::size_t digits = 0;
        for (const auto c : rest)
        {
            if (c < '0' || c > '9')
                break;
            value = value * 10 + static_cast<std::size_t>(c - '0');
            ++digits;
        }
        return digits == 0 ? 0 : value;
    }

    // The terminator is the text starting a line (spec 2.3), not the whole line: "END_DATA ! end"
    // and trailing blanks both end the data section.
    [[nodiscard]] inline std::string_view cut_at_data_until(
        std::string_view data, std::string_view until)
    {
        if (until.empty())
            return data;
        std::size_t start = 0;
        while (start < data.size())
        {
            const auto newline = data.find('\n', start);
            const auto end = newline == std::string_view::npos ? data.size() : newline;
            if (trim_left(data.substr(start, end - start)).starts_with(until))
                return data.substr(0, start);
            if (newline == std::string_view::npos)
                break;
            start = end + 1;
        }
        return data;
    }

    // One slot per value of one record, in record order: the column it belongs to, its type and the
    // variable's fill token. Built once, so every value is appended straight from the input text.
    struct slot
    {
        column* values;
        value_type type;
        std::string_view fillval;
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
        record_scanner(
            schema& result, std::string_view data, std::size_t first_line, std::string_view source)
                : result { result }
                , data { data }
                , line { first_line }
                , source { source }
                , marker { result.record_marker }
                , marker_char { result.record_marker.empty() ? '\0' : result.record_marker.front() }
                , single_marker { result.record_marker.size() == 1 }
                , byte_by_byte { result.record_marker.size() != 1 }
        {
            for (auto& var : result.file.variables)
            {
                if (var.is_nrv)
                    continue;
                varying.push_back(&var);
                const auto count = element_count(var);
                const auto hint = records_hint(data);
                var.values = make_column(var.type);
                if (hint != 0)
                    std::visit([&](auto& values) { values.reserve(hint * count); }, var.values);
                for (std::size_t element = 0; element < count; ++element)
                    slots.push_back({ &var.values, var.type, var.fillval_token });
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
                        throw parse_error(source, line, "missing separator", records + 1);
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
                        throw parse_error(source, line, "missing separator", records + 1);
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
                    continue;
                }
                if (c == ',')
                {
                    if (state == scan_state::at_value_start)
                        throw parse_error(source, line, "empty field", records + 1);
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
                    close_unquoted(position, token_start);
                    ++position;
                    continue;
                }
                if (state == scan_state::after_value)
                    throw parse_error(source, line, "missing separator", records + 1);
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
        }

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
            const auto& target = slots[index];
            if (!append_value(*target.values, target.type, value, target.fillval))
                throw parse_error(source, record_line,
                    "invalid " + std::string { type_name(target.type) }
                        + " value: " + std::string { value },
                    records + 1);
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
                    records + 1);
            ++records;
            index = 0;
            surplus = 0;
            state = scan_state::at_value_start;
        }

        schema& result;
        std::string_view data;
        std::size_t line;
        std::string_view source;
        std::string_view marker;
        char marker_char;
        bool single_marker;
        bool byte_by_byte;
        std::vector<variable*> varying;
        std::vector<slot> slots;
        std::size_t index = 0;
        std::size_t surplus = 0;
        std::size_t records = 0;
        std::size_t record_line = 0;
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
