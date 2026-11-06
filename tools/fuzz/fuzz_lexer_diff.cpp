#include <cefast/errors.hpp>
#include <cefast/header/lexer.hpp>

#include "reference_lexer.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The rewritten lexer must be indistinguishable from the frozen one (reference_lexer.hpp): same
// lines, same keys, same tokens (text and quoted), same line numbers, data offset and data line,
// or the same parse_error message. Anything else is a behaviour change, not a speed-up.
namespace
{
struct value_record
{
    std::string text;
    bool quoted;

    friend bool operator==(const value_record& left, const value_record& right)
    {
        return left.text == right.text && left.quoted == right.quoted;
    }
};

struct line_record
{
    std::string key;
    std::vector<value_record> values;
    std::size_t line = 0;
    std::string source;
    // Where the line's values start in the header's token array: catches a lexer that keeps the
    // right tokens but slices them from the wrong place.
    std::size_t token_index = 0;
    std::size_t token_count = 0;

    friend bool operator==(const line_record& left, const line_record& right)
    {
        return left.key == right.key && left.values == right.values && left.line == right.line
            && left.source == right.source && left.token_index == right.token_index
            && left.token_count == right.token_count;
    }
};

struct header_record
{
    std::vector<line_record> lines;
    std::vector<value_record> tokens;
    std::string keys; // joined, so a differing key list shows up as one string
    std::size_t data_offset = 0;
    std::size_t data_line = 0;

    friend bool operator==(const header_record& left, const header_record& right)
    {
        return left.lines == right.lines && left.tokens == right.tokens && left.keys == right.keys
            && left.data_offset == right.data_offset && left.data_line == right.data_line;
    }
};

template <typename Header>
header_record normalize(const Header& header)
{
    header_record record;
    record.data_offset = header.data_offset;
    record.data_line = header.data_line;
    for (const auto& token : header.tokens)
        record.tokens.push_back({ std::string { token.text }, token.quoted });
    for (const auto& key : header.keys)
    {
        record.keys += key;
        record.keys.push_back('\0');
    }
    for (const auto& line : header.lines)
    {
        line_record entry;
        entry.key = std::string { line.key };
        for (const auto& value : line.values)
            entry.values.push_back({ std::string { value.text }, value.quoted });
        entry.line = line.line;
        entry.source = line.source ? *line.source : std::string { "<null>" };
        entry.token_index = line.values.empty()
            ? 0
            : static_cast<std::size_t>(line.values.data() - header.tokens.data());
        entry.token_count = line.values.size();
        record.lines.push_back(std::move(entry));
    }
    return record;
}

struct outcome
{
    bool ok = false;
    std::string error;
    header_record header;
};

template <typename Call>
outcome attempt(Call&& call)
{
    try
    {
        outcome result;
        result.ok = true;
        result.header = normalize(call());
        return result;
    }
    catch (const cefast::parse_error& error)
    {
        return { false, error.what(), { } };
    }
}

void dump(const char* side, const outcome& result)
{
    std::fprintf(stderr, "--- %s ---\n", side);
    if (!result.ok)
    {
        std::fprintf(stderr, "throws: %s\n", result.error.c_str());
        return;
    }
    std::fprintf(stderr, "lines=%zu tokens=%zu data_offset=%zu data_line=%zu\n",
        result.header.lines.size(), result.header.tokens.size(), result.header.data_offset,
        result.header.data_line);
    for (const auto& line : result.header.lines)
    {
        std::fprintf(stderr, "  %s @%zu [%zu..%zu] values=", line.key.c_str(), line.line,
            line.token_index, line.token_index + line.token_count);
        for (const auto& value : line.values)
            std::fprintf(stderr, "%s\"%s\"", value.quoted ? "q:" : "", value.text.c_str());
        std::fprintf(stderr, "\n");
    }
}

void compare(std::string_view text, bool require_data_until)
{
    const auto ours = attempt([&] { return cefast::lex_header(text, "f", require_data_until); });
    const auto reference = attempt(
        [&] { return cefast::reference::lex_header(text, "f", require_data_until); });
    if (ours.ok == reference.ok && ours.error == reference.error && ours.header == reference.header)
        return;

    std::fprintf(stderr, "fuzz_lexer_diff: mismatch (require_data_until=%d)\n",
        require_data_until ? 1 : 0);
    dump("cefast", ours);
    dump("reference", reference);
    std::fprintf(stderr, "--- input ---\n%.*s\n", static_cast<int>(text.size()), text.data());
    __builtin_trap();
}
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::string_view text { reinterpret_cast<const char*>(data), size };
    compare(text, true);
    compare(text, false);
    return 0;
}
