#pragma once
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cefast/errors.hpp>

namespace cefast
{
struct token
{
    std::string text;
    bool quoted;
};

struct header_line
{
    std::string key;
    std::vector<token> values;
    std::size_t line;
    // Shared by every line of one lex_header call, so an error raised while parsing an included
    // .ceh names that file and not the .cef that included it.
    std::shared_ptr<const std::string> source;
};

struct lexed_header
{
    std::vector<header_line> lines;
    std::size_t data_offset = 0;
    std::size_t data_line = 0;
};

namespace detail
{
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

    inline std::string_view trim_right(std::string_view text) noexcept
    {
        while (!text.empty() && is_space(text.back()))
            text.remove_suffix(1);
        return text;
    }

    inline std::string_view trim(std::string_view text) noexcept
    {
        return trim_left(trim_right(text));
    }

    inline std::string upper(std::string_view text)
    {
        std::string result { text };
        for (auto& c : result)
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
        return result;
    }

    inline void append_value(std::vector<token>& tokens, std::string_view piece,
        std::string_view source, std::size_t line_number)
    {
        if (piece.empty())
            return;
        if (piece.front() == '"')
        {
            if (piece.size() < 2 || piece.back() != '"')
                throw parse_error(source, line_number, "unterminated quote");
            tokens.push_back({ std::string { piece.substr(1, piece.size() - 2) }, true });
            return;
        }
        tokens.push_back({ std::string { piece }, false });
    }

    // Values are comma-separated outside quotes; quoted text is verbatim (CEF has no escape
    // syntax).
    inline std::vector<token> split_values(
        std::string_view text, std::string_view source, std::size_t line_number)
    {
        std::vector<token> tokens;
        bool quoted = false;
        std::size_t start = 0;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[i];
            if (c == '"')
            {
                quoted = !quoted;
                continue;
            }
            if (c == ',' && !quoted)
            {
                append_value(tokens, trim(text.substr(start, i - start)), source, line_number);
                start = i + 1;
            }
        }
        append_value(tokens, trim(text.substr(start)), source, line_number);
        return tokens;
    }

    struct scanned_line
    {
        std::string_view text;
        bool continued;
    };

    // Drops the comment (a '!' outside quotes), the trailing whitespace and a trailing continuation
    // marker. A quote left open at the end of the line is an error: CEF only continues lines with
    // '\'.
    inline scanned_line scan_line(
        std::string_view line, std::string_view source, std::size_t line_number)
    {
        bool quoted = false;
        std::size_t comment = std::string_view::npos;
        for (std::size_t i = 0; i < line.size(); ++i)
        {
            if (line[i] == '"')
                quoted = !quoted;
            else if (!quoted && line[i] == '!')
            {
                comment = i;
                break;
            }
        }
        if (quoted)
            throw parse_error(source, line_number, "unterminated quote");
        auto visible
            = trim_right(line.substr(0, comment == std::string_view::npos ? line.size() : comment));
        if (!visible.empty() && visible.back() == '\\')
            return { trim_right(visible.substr(0, visible.size() - 1)), true };
        return { visible, false };
    }

    inline std::optional<header_line> parse_line(std::string_view text, std::size_t line_number,
        const std::shared_ptr<const std::string>& source)
    {
        text = trim_left(text);
        if (text.empty())
            return std::nullopt;

        std::size_t equals = std::string_view::npos;
        bool quoted = false;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '"')
                quoted = !quoted;
            else if (!quoted && text[i] == '=')
            {
                equals = i;
                break;
            }
        }
        if (equals == std::string_view::npos || trim(text.substr(0, equals)).empty())
            throw parse_error(*source, line_number, "expected KEY = value");

        header_line line;
        line.key = upper(trim(text.substr(0, equals)));
        line.values = split_values(text.substr(equals + 1), *source, line_number);
        line.line = line_number;
        line.source = source;
        return line;
    }
}

// The header ends right after its DATA_UNTIL line (CEFLIB CEF.c:1395-1402): the data section is
// everything past it. An included .ceh has no DATA_UNTIL of its own, hence the flag.
inline lexed_header lex_header(
    std::string_view text, std::string_view source, bool require_data_until = true)
{
    const auto source_name = std::make_shared<const std::string>(source);
    lexed_header result;
    std::string logical;
    std::size_t logical_line = 0;
    std::size_t line_number = 1;
    std::size_t next_line = 1;
    std::size_t offset = 0;
    bool pending = false;
    bool found = false;

    while (offset < text.size())
    {
        line_number = next_line;
        const auto newline = text.find('\n', offset);
        const auto end = newline == std::string_view::npos ? text.size() : newline;
        const auto scanned
            = detail::scan_line(text.substr(offset, end - offset), source, line_number);
        if (!pending)
        {
            logical.clear();
            logical_line = line_number;
        }
        logical.append(scanned.text);
        pending = scanned.continued;
        if (!pending)
        {
            if (auto line = detail::parse_line(logical, logical_line, source_name))
            {
                found = line->key == "DATA_UNTIL";
                result.lines.push_back(std::move(*line));
                next_line = line_number + 1;
                if (found)
                {
                    result.data_offset = newline == std::string_view::npos ? text.size() : end + 1;
                    result.data_line = next_line;
                    return result;
                }
            }
        }
        if (newline == std::string_view::npos)
            break;
        next_line = line_number + 1;
        offset = end + 1;
    }

    if (pending && !logical.empty())
    {
        if (auto line = detail::parse_line(logical, logical_line, source_name))
        {
            found = line->key == "DATA_UNTIL";
            result.lines.push_back(std::move(*line));
        }
    }
    result.data_offset = text.size();
    result.data_line = next_line;
    if (!found && require_data_until)
        throw parse_error(source, line_number, "DATA_UNTIL missing");
    return result;
}
}
