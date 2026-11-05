#pragma once
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cefast/errors.hpp>

namespace cefast
{
// Views, not copies: the header is a text format and every token is a slice of the input (or of a
// joined continuation line), so lexing allocates nothing per token.
struct token
{
    std::string_view text;
    bool quoted;
};

struct header_line
{
    std::string_view key;
    std::span<const token> values;
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
    // Storage the views above point into. A deque, so that pushing a key or a joined line never
    // moves the ones already handed out as views.
    std::vector<token> tokens;
    std::deque<std::string> keys;
    std::deque<std::string> joined;
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
            tokens.push_back({ piece.substr(1, piece.size() - 2), true });
            return;
        }
        tokens.push_back({ piece, false });
    }

    // Values are comma-separated outside quotes; quoted text is verbatim (CEF has no escape
    // syntax).
    inline void split_values(std::vector<token>& tokens, std::string_view text,
        std::string_view source, std::size_t line_number)
    {
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

    // Offset of the '=' that separates key and values, ignoring the ones inside quotes.
    [[nodiscard]] inline std::size_t find_equals(std::string_view text) noexcept
    {
        bool quoted = false;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '"')
                quoted = !quoted;
            else if (!quoted && text[i] == '=')
                return i;
        }
        return std::string_view::npos;
    }
}

// The header ends right after its DATA_UNTIL line (CEFLIB CEF.c:1395-1402): the data section is
// everything past it. An included .ceh has no DATA_UNTIL of its own, hence the flag.
inline lexed_header lex_header(
    std::string_view text, std::string_view source, bool require_data_until = true)
{
    struct pending_line
    {
        std::string_view key;
        std::size_t first;
        std::size_t count;
        std::size_t line;
    };

    const auto source_name = std::make_shared<const std::string>(source);
    lexed_header result;
    std::vector<pending_line> pending;
    std::string_view logical;
    std::size_t logical_line = 0;
    std::size_t line_number = 1;
    std::size_t next_line = 1;
    std::size_t offset = 0;
    bool continued = false;
    bool found = false;

    while (offset < text.size())
    {
        line_number = next_line;
        const auto newline = text.find('\n', offset);
        const auto end = newline == std::string_view::npos ? text.size() : newline;
        const auto scanned
            = detail::scan_line(text.substr(offset, end - offset), source, line_number);
        if (!continued)
        {
            logical_line = line_number;
            if (scanned.continued)
            {
                result.joined.emplace_back(scanned.text);
                logical = result.joined.back();
            }
            else
                logical = scanned.text;
        }
        else
        {
            result.joined.back().append(scanned.text);
            logical = result.joined.back();
        }
        continued = scanned.continued;
        if (!continued)
        {
            auto visible = detail::trim_left(logical);
            if (!visible.empty())
            {
                const auto equals = detail::find_equals(visible);
                const auto key = detail::trim(visible.substr(0, equals));
                if (equals == std::string_view::npos || key.empty())
                    throw parse_error(source, logical_line, "expected KEY = value");
                result.keys.emplace_back(detail::upper(key));
                const auto first = result.tokens.size();
                detail::split_values(
                    result.tokens, visible.substr(equals + 1), source, logical_line);
                pending.push_back(
                    { result.keys.back(), first, result.tokens.size() - first, logical_line });
                found = pending.back().key == "DATA_UNTIL";
                next_line = line_number + 1;
                if (found)
                {
                    result.data_offset = newline == std::string_view::npos ? text.size() : end + 1;
                    result.data_line = next_line;
                    break;
                }
            }
        }
        if (newline == std::string_view::npos)
            break;
        next_line = line_number + 1;
        offset = end + 1;
    }

    if (continued && !logical.empty())
    {
        const auto equals = detail::find_equals(logical);
        const auto key = detail::trim(logical.substr(0, equals));
        if (equals != std::string_view::npos && !key.empty())
        {
            result.keys.emplace_back(detail::upper(key));
            const auto first = result.tokens.size();
            detail::split_values(result.tokens, logical.substr(equals + 1), source, logical_line);
            pending.push_back(
                { result.keys.back(), first, result.tokens.size() - first, logical_line });
            found = found || pending.back().key == "DATA_UNTIL";
        }
    }

    result.lines.reserve(pending.size());
    const std::span<const token> all { result.tokens };
    for (const auto& line : pending)
        result.lines.push_back(
            { line.key, all.subspan(line.first, line.count), line.line, source_name });

    if (found)
        return result;

    result.data_offset = text.size();
    result.data_line = next_line;
    if (require_data_until)
        throw parse_error(source, line_number, "DATA_UNTIL missing");
    return result;
}
}
