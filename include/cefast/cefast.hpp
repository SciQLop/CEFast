#pragma once
#include <cefast/data/records.hpp>
#include <cefast/header/lexer.hpp>
#include <cefast/header/parser.hpp>
#include <cefast/input.hpp>

#include <filesystem>
#include <span>
#include <string_view>
#include <utility>

namespace cefast
{
namespace detail
{
    [[nodiscard]] inline cef_file parse_input(
        const input_buffer& input, const load_options& options)
    {
        const auto lexed = lex_header(input.text, input.source);
        auto schema = parse_header(lexed, input, options);
        parse_records(schema, input.text.substr(lexed.data_offset), lexed.data_line, input.source);
        return std::move(schema.file);
    }
}

// A path (.cef or .cef.gz, gzip is sniffed from the bytes) or an in-memory file; INCLUDE entries
// resolve through the options' resolver, the file's directory, include_paths, then CEFPATH.
[[nodiscard]] inline cef_file load(
    const std::filesystem::path& path, const load_options& options = { })
{
    const auto input = read_input(path);
    return detail::parse_input(input, options);
}

[[nodiscard]] inline cef_file load(std::span<const char> bytes, const load_options& options = { })
{
    const auto input = read_input(bytes);
    return detail::parse_input(input, options);
}
}
