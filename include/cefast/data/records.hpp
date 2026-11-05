#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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

    // One pass over the data text: quoted tokens are consumed whole (so separators, markers and '!'
    // inside them are data), everything else is delimited by ',' and by the record marker.
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
        {
            for (std::size_t index = 0; index < result.file.variables.size(); ++index)
            {
                if (result.file.variables[index].is_nrv)
                    continue;
                varying.push_back(index);
                expected += element_count(result.file.variables[index]);
            }
            const auto hint = records_hint(data);
            for (const auto index : varying)
            {
                auto& var = result.file.variables[index];
                var.values = make_column(var.type);
                if (hint != 0)
                    std::visit([&](auto& values) { values.reserve(hint * element_count(var)); },
                        var.values);
            }
        }

        void run()
        {
            std::size_t position = 0;
            while (position < data.size())
            {
                const char c = data[position];
                if (c == '"')
                {
                    const auto close = data.find('"', position + 1);
                    if (close == std::string_view::npos)
                        throw parse_error(source, line, "unterminated quote");
                    emit(data.substr(position + 1, close - position - 1), true);
                    after_value = true;
                    position = close + 1;
                    continue;
                }
                if (c == '!')
                {
                    const auto newline = data.find('\n', position);
                    position = newline == std::string_view::npos ? data.size() : newline;
                    continue;
                }
                if (!marker.empty() && data.compare(position, marker.size(), marker) == 0)
                {
                    if (marker == "\n")
                        ++line;
                    end_record();
                    position += marker.size();
                    continue;
                }
                if (c == ',')
                {
                    if (!after_value)
                        emit(text, false);
                    text.clear();
                    after_value = false;
                    ++position;
                    continue;
                }
                if (c == '\n')
                {
                    ++line;
                    ++position;
                    continue;
                }
                if (!is_space(c))
                {
                    text.push_back(c);
                    after_value = false;
                }
                ++position;
            }
            if (!text.empty() || !tokens.empty())
                end_record();
            for (const auto index : varying)
                result.file.variables[index].records = records;
        }

    private:
        void emit(std::string_view value, bool quoted)
        {
            if (tokens.empty())
                record_line = line;
            if (!quoted && value.empty())
                throw parse_error(source, line, "empty field", records + 1);
            tokens.push_back({ std::string { value }, quoted });
        }

        void end_record()
        {
            if (!text.empty())
            {
                emit(text, false);
                text.clear();
            }
            if (!tokens.empty())
            {
                if (tokens.size() != expected)
                    throw parse_error(source, record_line,
                        "expected " + std::to_string(expected) + " values, got "
                            + std::to_string(tokens.size()),
                        records + 1);
                assign();
            }
            tokens.clear();
        }

        void assign()
        {
            std::size_t position = 0;
            for (const auto index : varying)
            {
                auto& var = result.file.variables[index];
                const auto count = element_count(var);
                for (std::size_t element = 0; element < count; ++element, ++position)
                    if (!append_value(
                            var.values, var.type, tokens[position].text, var.fillval_token))
                        throw parse_error(source, record_line,
                            "invalid " + std::string { type_name(var.type) }
                                + " value: " + tokens[position].text,
                            records + 1);
            }
            ++records;
        }

        schema& result;
        std::string_view data;
        std::size_t line;
        std::string_view source;
        std::string_view marker;
        std::vector<std::size_t> varying;
        std::size_t expected = 0;
        std::size_t records = 0;
        std::size_t record_line = 0;
        std::vector<token> tokens;
        std::string text;
        bool after_value = false;
    };
}

inline void parse_records(
    schema& result, std::string_view data, std::size_t first_line, std::string_view source)
{
    const auto body = detail::cut_at_data_until(data, result.data_until);
    detail::record_scanner { result, body, first_line, source }.run();
}
}
