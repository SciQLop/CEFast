#include <cefast/header/lexer.hpp>
#include <cefast/header/parser.hpp>
#include <cefast/input.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// The header path must only ever throw parse_error, and INCLUDE must stay bounded however the
// included files include each other. The resolver derives the included text from the fuzz input
// itself: "a.ceh" is the input, "b.ceh" its second half, so a file can recurse into itself and
// the depth limit is exercised on every interesting input.
namespace
{
std::optional<std::string> resolve(std::string_view name, std::string_view text)
{
    if (name == "a.ceh")
        return std::string { text };
    if (name == "b.ceh")
        return std::string { text.substr(text.size() / 2) };
    return std::nullopt;
}
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::string_view text { reinterpret_cast<const char*>(data), size };
    try
    {
        auto input = cefast::input_buffer { std::string { text }, "fuzz.cef", std::nullopt };
        const auto lexed = cefast::lex_header(input.bytes, input.source);
        cefast::load_options options;
        options.include_resolver = [&text](std::string_view name) { return resolve(name, text); };
        const auto schema = cefast::parse_header(lexed, input, options);
        (void)schema;
    }
    catch (const cefast::parse_error&)
    {
    }
    return 0;
}
