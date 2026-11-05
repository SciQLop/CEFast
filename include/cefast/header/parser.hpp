#pragma once
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fast_float/fast_float.h>

#include <cefast/errors.hpp>
#include <cefast/header/attributes.hpp>
#include <cefast/header/lexer.hpp>
#include <cefast/input.hpp>
#include <cefast/model.hpp>
#include <cefast/time.hpp>

namespace cefast
{
struct load_options
{
    std::vector<std::filesystem::path> include_paths;
    std::function<std::optional<std::string>(std::string_view)> include_resolver;
};

struct schema
{
    cef_file file;
    std::string record_marker = "\n";
    std::string data_until;
    // DATA tokens per variable, same indices as file.variables (empty for record-varying ones).
    std::vector<std::vector<token>> nrv_data;
};

namespace detail
{
    constexpr std::size_t max_include_depth = 16;

#if defined(_WIN32)
    constexpr char path_list_separator = ';';
#else
    constexpr char path_list_separator = ':';
#endif

    inline bool equal_ignore_case(std::string_view left, std::string_view right)
    {
        return upper(left) == upper(right);
    }

    [[nodiscard]] inline std::optional<std::int64_t> try_integer(std::string_view text) noexcept
    {
        if (!text.empty() && text.front() == '+')
            text.remove_prefix(1);
        std::int64_t value = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc { } || result.ptr != text.data() + text.size())
            return std::nullopt;
        return value;
    }

    [[nodiscard]] inline std::optional<double> try_double(std::string_view text) noexcept
    {
        double value = 0;
        const auto result = fast_float::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc { } || result.ptr != text.data() + text.size())
            return std::nullopt;
        return value;
    }

    [[nodiscard]] inline std::int64_t parse_integer(
        std::string_view text, std::string_view source, std::size_t line)
    {
        const auto value = try_integer(text);
        if (!value)
            throw parse_error(source, line, "invalid integer: " + std::string { text });
        return *value;
    }

    [[nodiscard]] inline double parse_double(
        std::string_view text, std::string_view source, std::size_t line)
    {
        const auto value = try_double(text);
        if (!value)
            throw parse_error(source, line, "invalid number: " + std::string { text });
        return *value;
    }

    [[nodiscard]] inline value_type parse_value_type(
        std::string_view text, std::string_view source, std::size_t line)
    {
        const auto name = upper(text);
        if (name == "FLOAT")
            return value_type::FLOAT;
        if (name == "DOUBLE")
            return value_type::DOUBLE;
        if (name == "INT")
            return value_type::INT;
        if (name == "BYTE")
            return value_type::BYTE;
        if (name == "ISO_TIME")
            return value_type::ISO_TIME;
        if (name == "ISO_TIME_RANGE")
            return value_type::ISO_TIME_RANGE;
        if (name == "CHAR")
            return value_type::CHAR;
        throw parse_error(source, line, "unknown VALUE_TYPE: " + std::string { text });
    }

    [[nodiscard]] inline scalar convert_entry(
        std::string_view text, value_type type, std::string_view source, std::size_t line)
    {
        switch (type)
        {
            case value_type::CHAR:
                return std::string { text };
            case value_type::INT:
            case value_type::BYTE:
                return parse_integer(text, source, line);
            case value_type::FLOAT:
            case value_type::DOUBLE:
                return parse_double(text, source, line);
            case value_type::ISO_TIME:
            {
                const auto time = parse_iso_time(text);
                if (!time)
                    throw parse_error(source, line, "invalid ISO time: " + std::string { text });
                return iso_time { *time };
            }
            case value_type::ISO_TIME_RANGE:
            {
                const auto range = parse_iso_time_range(text);
                if (!range)
                    throw parse_error(
                        source, line, "invalid ISO time range: " + std::string { text });
                return iso_time_range { (*range)[0], (*range)[1] };
            }
        }
        throw parse_error(source, line, "unknown VALUE_TYPE");
    }

    // A FILLVAL is typed by its variable, but a value the type cannot hold (a time variable filled
    // with 9999-99-99T99:99:99Z) stays text instead of failing the load.
    inline void type_fillval(variable& var)
    {
        auto* fill = const_cast<attribute*>(find(var.attrs, "FILLVAL"));
        if (fill == nullptr || fill->entries.size() != 1)
            return;
        auto* text = std::get_if<std::string>(&fill->entries.front());
        if (text == nullptr)
            return;
        const auto value = std::string_view { *text };
        switch (var.type)
        {
            case value_type::INT:
            case value_type::BYTE:
                if (const auto number = try_integer(value))
                    fill->entries.front() = *number;
                break;
            case value_type::FLOAT:
            case value_type::DOUBLE:
                if (const auto number = try_double(value))
                    fill->entries.front() = *number;
                break;
            case value_type::ISO_TIME:
                if (const auto time = parse_iso_time(value))
                    fill->entries.front() = iso_time { *time };
                break;
            case value_type::ISO_TIME_RANGE:
                if (const auto range = parse_iso_time_range(value))
                    fill->entries.front() = iso_time_range { (*range)[0], (*range)[1] };
                break;
            case value_type::CHAR:
                break;
        }
    }

    [[nodiscard]] inline std::size_t parse_size(
        std::string_view text, std::string_view source, std::size_t line)
    {
        const auto value = try_integer(text);
        if (!value || *value < 1)
            throw parse_error(source, line, "invalid SIZES value: " + std::string { text });
        return static_cast<std::size_t>(*value);
    }

    // A single quoted token such as "64, 6, 32" is one value in the lexer but several dimensions
    // (common in CSA headers, not in the spec).
    [[nodiscard]] inline std::vector<std::size_t> parse_sizes(
        const std::vector<token>& values, std::string_view source, std::size_t line)
    {
        std::vector<std::string_view> pieces;
        if (values.size() == 1 && values.front().quoted)
        {
            const std::string_view text = values.front().text;
            std::size_t start = 0;
            while (true)
            {
                const auto comma = text.find(',', start);
                pieces.push_back(trim(text.substr(start, comma - start)));
                if (comma == std::string_view::npos)
                    break;
                start = comma + 1;
            }
        }
        else
        {
            for (const auto& value : values)
                pieces.push_back(value.text);
        }

        std::vector<std::size_t> sizes;
        for (const auto piece : pieces)
            if (!piece.empty())
                sizes.push_back(parse_size(piece, source, line));
        if (sizes.empty())
            throw parse_error(source, line, "SIZES needs at least one value");
        return sizes;
    }

    [[nodiscard]] inline std::optional<std::string> read_whole_file(
        const std::filesystem::path& path)
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error))
            return std::nullopt;
        std::ifstream stream { path, std::ios::binary };
        if (!stream)
            return std::nullopt;
        return std::string { std::istreambuf_iterator<char> { stream },
            std::istreambuf_iterator<char> { } };
    }

    // Search order: user resolver, the including file's directory, include_paths, $CEFPATH, then
    // the name as given (CEFLIB's CWD-relative first try, kept last so files win over the working
    // dir).
    [[nodiscard]] inline std::optional<std::string> resolve_include(
        std::string_view name, const input_buffer& input, const load_options& options)
    {
        const std::filesystem::path given { name };
        if (options.include_resolver)
            if (auto text = options.include_resolver(name))
                return text;
        if (input.directory)
            if (auto text = read_whole_file(*input.directory / given))
                return text;
        for (const auto& directory : options.include_paths)
            if (auto text = read_whole_file(directory / given))
                return text;
        if (const char* cefpath = std::getenv("CEFPATH"))
        {
            const std::string_view paths { cefpath };
            std::size_t start = 0;
            while (start <= paths.size())
            {
                const auto separator = paths.find(path_list_separator, start);
                const auto piece = paths.substr(start,
                    separator == std::string_view::npos ? std::string_view::npos
                                                        : separator - start);
                if (!piece.empty())
                    if (auto text = read_whole_file(std::filesystem::path { piece } / given))
                        return text;
                if (separator == std::string_view::npos)
                    break;
                start = separator + 1;
            }
        }
        return read_whole_file(given);
    }

    // Included lines are spliced in place, so an INCLUDE inside a metadata block keeps extending
    // that block. Included files are reported by the name written in the INCLUDE line.
    inline void expand_includes(std::vector<header_line>& lines,
        const std::vector<header_line>& source_lines, const input_buffer& input,
        const load_options& options, std::size_t depth)
    {
        for (const auto& line : source_lines)
        {
            if (line.key != "INCLUDE")
            {
                lines.push_back(line);
                continue;
            }
            if (line.values.empty())
                throw parse_error(*line.source, line.line, "INCLUDE needs a file name");
            const auto name = line.values.front().text;
            if (depth >= max_include_depth)
                throw parse_error(
                    *line.source, line.line, "INCLUDE nesting too deep (cycle?): " + name);
            const auto text = resolve_include(name, input, options);
            if (!text)
                throw parse_error(*line.source, line.line, "INCLUDE file not found: " + name);
            const auto included = lex_header(*text, name, false);
            expand_includes(lines, included.lines, input, options, depth + 1);
        }
    }
}

inline schema parse_header(
    const lexed_header& header, const input_buffer& input, const load_options& options = { })
{
    std::vector<header_line> lines;
    detail::expand_includes(lines, header.lines, input, options, 0);

    schema result;
    enum class block
    {
        none,
        meta,
        variable
    };
    block state = block::none;
    attribute meta;
    variable var;
    std::vector<token> pending_data;
    std::optional<value_type> meta_value_type;
    std::shared_ptr<const std::string> last_source;

    for (const auto& line : lines)
    {
        const auto& key = line.key;
        const auto& source = *line.source;
        last_source = line.source;

        if (key == "START_META")
        {
            if (state != block::none)
                throw parse_error(source, line.line, "START_META inside an open block");
            if (line.values.empty())
                throw parse_error(source, line.line, "START_META needs a name");
            state = block::meta;
            meta = attribute { line.values.front().text, { } };
            meta_value_type.reset();
        }
        else if (key == "END_META")
        {
            if (state != block::meta)
                throw parse_error(source, line.line, "END_META without START_META");
            if (line.values.empty()
                || !detail::equal_ignore_case(line.values.front().text, meta.name))
                throw parse_error(
                    source, line.line, "END_META name does not match START_META: " + meta.name);
            result.file.globals.push_back(std::move(meta));
            state = block::none;
        }
        else if (key == "ENTRY")
        {
            if (state != block::meta)
                throw parse_error(source, line.line, "ENTRY outside a metadata block");
            for (const auto& value : line.values)
                meta.entries.push_back(meta_value_type
                        ? detail::convert_entry(value.text, *meta_value_type, source, line.line)
                        : scalar { value.text });
        }
        else if (key == "VALUE_TYPE")
        {
            if (line.values.empty())
                throw parse_error(source, line.line, "VALUE_TYPE needs a value");
            const auto type = detail::parse_value_type(line.values.front().text, source, line.line);
            if (state == block::meta)
                meta_value_type = type;
            else if (state == block::variable)
                var.type = type;
            else
                throw parse_error(source, line.line, "VALUE_TYPE outside a block");
        }
        else if (key == "START_VARIABLE")
        {
            if (state != block::none)
                throw parse_error(source, line.line, "START_VARIABLE inside an open block");
            if (line.values.empty())
                throw parse_error(source, line.line, "START_VARIABLE needs a name");
            state = block::variable;
            var = variable { };
            var.name = line.values.front().text;
            pending_data.clear();
        }
        else if (key == "END_VARIABLE")
        {
            if (state != block::variable)
                throw parse_error(source, line.line, "END_VARIABLE without START_VARIABLE");
            if (line.values.empty() || line.values.front().text != var.name)
                throw parse_error(source, line.line,
                    "END_VARIABLE name does not match START_VARIABLE: " + var.name);
            detail::type_fillval(var);
            const bool is_nrv = var.is_nrv;
            result.file.variables.push_back(std::move(var));
            result.nrv_data.push_back(is_nrv ? std::move(pending_data) : std::vector<token> { });
            state = block::none;
        }
        else if (key == "SIZES")
        {
            if (state != block::variable)
                throw parse_error(source, line.line, "SIZES outside a variable block");
            var.sizes = detail::parse_sizes(line.values, source, line.line);
        }
        else if (key == "DATA")
        {
            if (state != block::variable)
                throw parse_error(source, line.line, "DATA outside a variable block");
            var.is_nrv = true;
            pending_data = line.values;
        }
        else if (key == "END_OF_RECORD_MARKER")
        {
            if (line.values.empty())
                throw parse_error(source, line.line, "END_OF_RECORD_MARKER needs a value");
            result.record_marker = line.values.front().text;
        }
        else if (key == "DATA_UNTIL")
        {
            const std::string_view text = line.values.empty()
                ? std::string_view { }
                : std::string_view { line.values.front().text };
            if (detail::upper(text) == "EOF")
                result.data_until.clear();
            else
                result.data_until = std::string { text };
        }
        else
        {
            attribute attr { key, { } };
            for (const auto& value : line.values)
                attr.entries.push_back(scalar { value.text });
            if (state == block::variable)
            {
                if (key == "FILLVAL" && !line.values.empty())
                    var.fillval_token = line.values.front().text;
                var.attrs.push_back(std::move(attr));
            }
            else if (state == block::meta)
                throw parse_error(
                    source, line.line, "unexpected " + key + " inside a metadata block");
            else
                result.file.globals.push_back(std::move(attr));
        }
    }

    if (state == block::meta)
        throw parse_error(*last_source, 0, "unclosed START_META: " + meta.name);
    if (state == block::variable)
        throw parse_error(*last_source, 0, "unclosed START_VARIABLE: " + var.name);

    const auto* version = find(result.file.globals, "FILE_FORMAT_VERSION");
    if (version == nullptr || version->entries.empty())
        throw parse_error(input.source, 0, "FILE_FORMAT_VERSION missing");
    const auto& text = std::get<std::string>(version->entries.front());
    if (text.rfind("CEF-2", 0) != 0)
        throw parse_error(input.source, 0, "unsupported CEF version: " + text);

    return result;
}
}
