#pragma once
#include <array>
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
#include <cefast/values.hpp>

namespace cefast
{
struct load_options
{
    std::vector<std::filesystem::path> include_paths;
    std::function<std::optional<std::string>(std::string_view)> include_resolver;
    // 0: pick from the hardware, 1: serial (the reference path), N: at most N threads.
    std::size_t threads = 0;
};

// The header is lexed into views, but a non-record-varying variable's DATA values stay in the
// schema and the schema outlives the text it was parsed from: these own their text.
struct owned_token
{
    std::string text;
    bool quoted;
};

struct schema
{
    cef_file file;
    std::string record_marker = "\n";
    std::string data_until;
    // DATA tokens per variable, same indices as file.variables (empty for record-varying ones).
    std::vector<std::vector<owned_token>> nrv_data;
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
        text = without_plus(text);
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
        std::span<const token> values, std::string_view source, std::size_t line)
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

    // Owns the text and the lexed views of every included file: header_line only holds views, so
    // the buffer an INCLUDE was read into has to outlive the lines spliced from it.
    struct include_arena
    {
        std::deque<std::string> texts;
        std::deque<lexed_header> headers;
    };

    // Included lines are spliced in place, so an INCLUDE inside a metadata block keeps extending
    // that block. Included files are reported by the name written in the INCLUDE line.
    inline void expand_includes(std::vector<header_line>& lines,
        const std::vector<header_line>& source_lines, const input_buffer& input,
        const load_options& options, std::size_t depth, include_arena& arena)
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
                throw parse_error(*line.source, line.line,
                    "INCLUDE nesting too deep (cycle?): " + std::string { name });
            const auto text = resolve_include(name, input, options);
            if (!text)
                throw parse_error(
                    *line.source, line.line, "INCLUDE file not found: " + std::string { name });
            arena.texts.push_back(std::move(*text));
            arena.headers.emplace_back(lex_header(arena.texts.back(), name, false));
            expand_includes(lines, arena.headers.back().lines, input, options, depth + 1, arena);
        }
    }
}

namespace detail
{
    enum class keyword_family
    {
        meta,
        variable,
        value_type,
        file_level,
        attribute,
        ignore
    };

    struct keyword_entry
    {
        std::string_view key;
        keyword_family family;
    };

    // One table decides what is structural; every other key is an attribute of the current block.
    constexpr std::array keyword_table { keyword_entry { "DATA", keyword_family::variable },
        keyword_entry { "DATA_UNTIL", keyword_family::file_level },
        keyword_entry { "END_META", keyword_family::meta },
        keyword_entry { "END_OF_RECORD_MARKER", keyword_family::file_level },
        keyword_entry { "END_VARIABLE", keyword_family::variable },
        keyword_entry { "ENTRY", keyword_family::meta },
        keyword_entry { "INCLUDE", keyword_family::ignore },
        keyword_entry { "SIZES", keyword_family::variable },
        keyword_entry { "START_META", keyword_family::meta },
        keyword_entry { "START_VARIABLE", keyword_family::variable },
        keyword_entry { "VALUE_TYPE", keyword_family::value_type } };

    [[nodiscard]] constexpr keyword_family family_of(std::string_view key) noexcept
    {
        for (const auto& entry : keyword_table)
            if (entry.key == key)
                return entry.family;
        return keyword_family::attribute;
    }

    [[nodiscard]] inline std::string_view source_of(const header_line& line) noexcept
    {
        return *line.source;
    }

    inline void check_version(const cef_file& file, std::string_view source)
    {
        const auto* version = find(file.globals, "FILE_FORMAT_VERSION");
        if (version == nullptr || version->entries.empty())
            throw parse_error(source, 0, "FILE_FORMAT_VERSION missing");
        const auto& text = std::get<std::string>(version->entries.front());
        if (text.rfind("CEF-2", 0) != 0)
            throw parse_error(source, 0, "unsupported CEF version: " + text);
    }

    // Header keywords are handled one family at a time; each handler owns the few keys of its
    // family and the state it needs, so no function has to know the whole grammar.
    class parser_state
    {
    public:
        void handle(const header_line& line);
        void finish(std::string_view input_source);
        [[nodiscard]] schema take_result() { return std::move(result); }

    private:
        enum class block
        {
            none,
            meta,
            variable
        };

        void handle_meta(const header_line& line);
        void handle_variable(const header_line& line);
        void handle_value_type(const header_line& line);
        void handle_file_level(const header_line& line);
        void handle_attribute(const header_line& line);

        void start_meta(const header_line& line);
        void add_meta_entry(const header_line& line);
        void end_meta(const header_line& line);
        void start_variable(const header_line& line);
        void set_sizes(const header_line& line);
        void set_data(const header_line& line);
        void end_variable(const header_line& line);
        void set_record_marker(const header_line& line);
        void set_data_until(const header_line& line);
        void remember_fillval(const header_line& line);
        void fill_nrv_columns();

        schema result;
        block state = block::none;
        attribute meta;
        variable var;
        std::vector<owned_token> pending_data;
        std::optional<value_type> meta_value_type;
        std::shared_ptr<const std::string> last_source;
        std::shared_ptr<const std::string> data_source;
        std::size_t data_line = 0;
        std::vector<std::pair<std::shared_ptr<const std::string>, std::size_t>> nrv_locations;
    };

    inline void parser_state::handle(const header_line& line)
    {
        last_source = line.source;
        switch (family_of(line.key))
        {
            case keyword_family::meta:
                handle_meta(line);
                return;
            case keyword_family::variable:
                handle_variable(line);
                return;
            case keyword_family::value_type:
                handle_value_type(line);
                return;
            case keyword_family::file_level:
                handle_file_level(line);
                return;
            case keyword_family::attribute:
                handle_attribute(line);
                return;
            case keyword_family::ignore:
                return;
        }
    }

    inline void parser_state::handle_meta(const header_line& line)
    {
        if (line.key == "START_META")
            start_meta(line);
        else if (line.key == "END_META")
            end_meta(line);
        else
            add_meta_entry(line);
    }

    inline void parser_state::start_meta(const header_line& line)
    {
        if (state != block::none)
            throw parse_error(source_of(line), line.line, "START_META inside an open block");
        if (line.values.empty())
            throw parse_error(source_of(line), line.line, "START_META needs a name");
        state = block::meta;
        meta = attribute { std::string { line.values.front().text }, { } };
        meta_value_type.reset();
    }

    inline void parser_state::add_meta_entry(const header_line& line)
    {
        if (state != block::meta)
            throw parse_error(source_of(line), line.line, "ENTRY outside a metadata block");
        for (const auto& value : line.values)
            meta.entries.push_back(meta_value_type
                    ? convert_entry(value.text, *meta_value_type, source_of(line), line.line)
                    : scalar { std::string { value.text } });
    }

    inline void parser_state::end_meta(const header_line& line)
    {
        if (state != block::meta)
            throw parse_error(source_of(line), line.line, "END_META without START_META");
        if (line.values.empty() || !equal_ignore_case(line.values.front().text, meta.name))
            throw parse_error(source_of(line), line.line,
                "END_META name does not match START_META: " + meta.name);
        result.file.globals.push_back(std::move(meta));
        state = block::none;
    }

    inline void parser_state::handle_variable(const header_line& line)
    {
        if (line.key == "START_VARIABLE")
            start_variable(line);
        else if (line.key == "END_VARIABLE")
            end_variable(line);
        else if (line.key == "SIZES")
            set_sizes(line);
        else
            set_data(line);
    }

    inline void parser_state::start_variable(const header_line& line)
    {
        if (state != block::none)
            throw parse_error(source_of(line), line.line, "START_VARIABLE inside an open block");
        if (line.values.empty())
            throw parse_error(source_of(line), line.line, "START_VARIABLE needs a name");
        state = block::variable;
        var = variable { };
        var.name = std::string { line.values.front().text };
        pending_data.clear();
        data_source.reset();
        data_line = line.line;
    }

    inline void parser_state::set_sizes(const header_line& line)
    {
        if (state != block::variable)
            throw parse_error(source_of(line), line.line, "SIZES outside a variable block");
        var.sizes = parse_sizes(line.values, source_of(line), line.line);
    }

    inline void parser_state::set_data(const header_line& line)
    {
        if (state != block::variable)
            throw parse_error(source_of(line), line.line, "DATA outside a variable block");
        var.is_nrv = true;
        pending_data.clear();
        for (const auto& value : line.values)
            pending_data.push_back({ std::string { value.text }, value.quoted });
        data_source = line.source;
        data_line = line.line;
    }

    inline void parser_state::end_variable(const header_line& line)
    {
        if (state != block::variable)
            throw parse_error(source_of(line), line.line, "END_VARIABLE without START_VARIABLE");
        if (line.values.empty() || line.values.front().text != var.name)
            throw parse_error(source_of(line), line.line,
                "END_VARIABLE name does not match START_VARIABLE: " + var.name);
        type_fillval(var);
        const bool is_nrv = var.is_nrv;
        result.file.variables.push_back(std::move(var));
        result.nrv_data.push_back(is_nrv ? std::move(pending_data) : std::vector<owned_token> { });
        nrv_locations.emplace_back(data_source, data_line);
        state = block::none;
    }

    inline void parser_state::handle_value_type(const header_line& line)
    {
        if (line.values.empty())
            throw parse_error(source_of(line), line.line, "VALUE_TYPE needs a value");
        const auto type = parse_value_type(line.values.front().text, source_of(line), line.line);
        if (state == block::meta)
            meta_value_type = type;
        else if (state == block::variable)
            var.type = type;
        else
            throw parse_error(source_of(line), line.line, "VALUE_TYPE outside a block");
    }

    inline void parser_state::handle_file_level(const header_line& line)
    {
        if (line.key == "END_OF_RECORD_MARKER")
            set_record_marker(line);
        else
            set_data_until(line);
    }

    inline void parser_state::set_record_marker(const header_line& line)
    {
        if (line.values.empty())
            throw parse_error(source_of(line), line.line, "END_OF_RECORD_MARKER needs a value");
        result.record_marker = std::string { line.values.front().text };
    }

    inline void parser_state::set_data_until(const header_line& line)
    {
        const std::string_view text = line.values.empty()
            ? std::string_view { }
            : std::string_view { line.values.front().text };
        if (upper(text) == "EOF")
            result.data_until.clear();
        else
            result.data_until = std::string { text };
    }

    inline void parser_state::remember_fillval(const header_line& line)
    {
        if (line.key == "FILLVAL" && !line.values.empty())
            var.fillval_token = line.values.front().text;
    }

    inline void parser_state::handle_attribute(const header_line& line)
    {
        attribute attr { std::string { line.key }, { } };
        for (const auto& value : line.values)
            attr.entries.push_back(scalar { std::string { value.text } });

        if (state == block::variable)
        {
            remember_fillval(line);
            var.attrs.push_back(std::move(attr));
            return;
        }
        if (state == block::meta)
            throw parse_error(source_of(line), line.line,
                "unexpected " + std::string { line.key } + " inside a metadata block");
        result.file.globals.push_back(std::move(attr));
    }

    inline void parser_state::fill_nrv_columns()
    {
        for (std::size_t index = 0; index < result.file.variables.size(); ++index)
        {
            auto& var = result.file.variables[index];
            if (!var.is_nrv)
                continue;
            std::size_t expected = 1;
            for (const auto size : var.sizes)
                expected *= size;
            const auto& tokens = result.nrv_data[index];
            const auto* source = nrv_locations[index].first.get();
            const std::string_view where { source != nullptr ? *source : std::string_view { } };
            const auto line = nrv_locations[index].second;
            if (tokens.size() != expected)
                throw parse_error(where, line,
                    "variable " + var.name + ": expected " + std::to_string(expected)
                        + " values, got " + std::to_string(tokens.size()));
            var.records = 1;
            var.values = make_column(var.type);
            for (const auto& token : tokens)
                if (!append_value(var.values, var.type, token.text, var.fillval_token))
                    throw parse_error(where, line,
                        "variable " + var.name + ": invalid value: " + std::string { token.text });
        }
    }

    inline void parser_state::finish(std::string_view input_source)
    {
        const auto source = last_source ? std::string_view { *last_source } : input_source;
        if (state == block::meta)
            throw parse_error(source, 0, "unclosed START_META: " + meta.name);
        if (state == block::variable)
            throw parse_error(source, 0, "unclosed START_VARIABLE: " + var.name);
        fill_nrv_columns();
        check_version(result.file, input_source);
    }
}

inline schema parse_header(
    const lexed_header& header, const input_buffer& input, const load_options& options = { })
{
    std::vector<header_line> lines;
    detail::include_arena arena;
    detail::expand_includes(lines, header.lines, input, options, 0, arena);

    detail::parser_state state;
    for (const auto& line : lines)
        state.handle(line);
    state.finish(input.source);
    return state.take_result();
}
}
