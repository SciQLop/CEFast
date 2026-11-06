#pragma once
#include <algorithm>
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

    // Whether append_value accepts `piece`: a piece that opens with a quote has to close on it.
    [[nodiscard]] inline bool is_value(std::string_view piece) noexcept
    {
        return piece.empty() || piece.front() != '"'
            || (piece.size() >= 2 && piece.back() == '"');
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
        // Comment dropped, trailing whitespace and a trailing continuation marker removed.
        std::string_view text;
        bool continued;
        // Offset of the '=' that separates key from values in `text`, npos when there is none.
        std::size_t equals = std::string_view::npos;
        // Offset just past the last value separator: where the trailing value starts.
        std::size_t values_from = 0;
    };

    // One merged pass over the line: the bytes the grammar cares about ('"', '!', '=' before the
    // key is known, ',' after it) are located with memchr-backed find() calls that each resume
    // where the previous one stopped. Text between them is skipped wholesale and quoted spans are
    // never walked byte by byte.
    //
    // The comment ends the line (a '!' outside quotes), the '=' outside quotes ends the key, and a
    // quote left open at the end of the line is an error: CEF only continues lines with '\'.
    //
    // When `values` is given the value tokens are appended as their commas are met, which is only
    // valid for a line that is not continued: the caller passes it for a line that starts a
    // logical line and rewinds if that line turns out to continue.
    inline scanned_line scan_line(std::string_view line, std::string_view source,
        std::size_t line_number, std::vector<token>* values = nullptr)
    {
        constexpr auto npos = std::string_view::npos;
        bool quoted = false;
        std::size_t comment = npos;
        std::size_t equals = npos;
        std::size_t value_start = 0;
        std::size_t values_from = 0;
        // A value the caller will reject. Deferred: the key check and the end-of-line quote check
        // come first in the old lexer, so they have to come first here too.
        bool bad_value = false;

        // Cached next positions, one per byte of interest.
        std::size_t next_quote = line.find('"');
        std::size_t next_bang = line.find('!');
        std::size_t next_equals = line.find('=');
        std::size_t next_comma = npos;

        while (true)
        {
            const std::size_t next = std::min(
                { next_quote, next_bang, equals == npos ? next_equals : next_comma });
            if (next == npos)
                break;
            if (line[next] == '"')
            {
                quoted = !quoted;
                next_quote = line.find('"', next + 1);
            }
            else if (line[next] == '!')
            {
                if (quoted)
                    next_bang = line.find('!', next + 1);
                else
                {
                    comment = next;
                    break;
                }
            }
            else if (line[next] == '=')
            {
                if (quoted)
                    next_equals = line.find('=', next + 1);
                else
                {
                    equals = next;
                    value_start = next + 1;
                    values_from = value_start;
                    next_comma = line.find(',', value_start);
                }
            }
            else if (quoted)
                next_comma = line.find(',', next + 1);
            else
            {
                if (values != nullptr)
                {
                    const auto piece = trim(line.substr(value_start, next - value_start));
                    if (is_value(piece))
                        append_value(*values, piece, source, line_number);
                    else
                        bad_value = true;
                }
                value_start = next + 1;
                values_from = value_start;
                next_comma = line.find(',', next + 1);
            }
        }

        if (quoted)
            throw parse_error(source, line_number, "unterminated quote");
        auto visible = trim_right(line.substr(0, comment == npos ? line.size() : comment));
        const bool continued = !visible.empty() && visible.back() == '\\';
        if (continued)
            visible = trim_right(visible.substr(0, visible.size() - 1));
        else if (values != nullptr && !visible.empty())
        {
            if (equals == npos || trim(visible.substr(0, equals)).empty())
                throw parse_error(source, line_number, "expected KEY = value");
            if (bad_value)
                throw parse_error(source, line_number, "unterminated quote");
        }
        return { visible, continued, equals, values_from };
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
        const bool starts_line = !continued;
        const auto first_token = result.tokens.size();
        const auto scanned = detail::scan_line(text.substr(offset, end - offset), source,
            line_number, starts_line ? &result.tokens : nullptr);
        if (!starts_line)
        {
            result.joined.back().append(scanned.text);
            logical = result.joined.back();
        }
        else
        {
            logical_line = line_number;
            if (scanned.continued)
            {
                // The tokens of this physical line are dropped: the values are split from the
                // joined logical line once it is complete.
                result.tokens.resize(first_token);
                result.joined.emplace_back(scanned.text);
                logical = result.joined.back();
            }
            else
                logical = scanned.text;
        }
        continued = scanned.continued;
        if (!continued)
        {
            // A line that is not continued is the whole logical line: scan_line already found its
            // key separator and its value tokens. Only a joined line needs the two-step split.
            bool recorded = false;
            if (starts_line)
            {
                // scan_line has already rejected an empty or missing key for a line it could lex
                // in one pass, with the same message the joined path raises below.
                if (!scanned.text.empty())
                {
                    result.keys.emplace_back(detail::upper(
                        detail::trim(scanned.text.substr(0, scanned.equals))));
                    detail::append_value(result.tokens,
                        detail::trim(scanned.text.substr(scanned.values_from)), source,
                        logical_line);
                    pending.push_back({ result.keys.back(), first_token,
                        result.tokens.size() - first_token, logical_line });
                    found = pending.back().key == "DATA_UNTIL";
                    recorded = true;
                }
            }
            else
            {
                const auto visible = detail::trim_left(logical);
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
                    recorded = true;
                }
            }
            if (recorded)
                next_line = line_number + 1;
            if (found)
            {
                result.data_offset = newline == std::string_view::npos ? text.size() : end + 1;
                result.data_line = next_line;
                break;
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
