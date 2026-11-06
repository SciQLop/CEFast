#include <cefast/time.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>

// The fixed-width fast path may only be a faster spelling of the general one: on every input the
// two must accept the same strings and produce the same nanoseconds.
namespace
{
[[noreturn]] void report(const char* what, std::string_view text)
{
    std::fprintf(stderr, "fuzz_time: %s on \"%.*s\"\n", what, static_cast<int>(text.size()),
        text.data());
    __builtin_trap();
}
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::string_view text { reinterpret_cast<const char*>(data), size };
    const auto general = cefast::parse_iso_time_general(text);
    if (general != cefast::parse_iso_time(text))
        report("fixed path disagrees with general", text);

    if (size == cefast::detail::fixed_time_size)
    {
        const auto fixed = cefast::detail::parse_fixed_time(text);
        if (fixed != general)
            report("fixed path accepts a different set", text);
    }

    const auto range = cefast::parse_iso_time_range(text);
    const auto separator = text.find('/');
    const std::optional<std::array<std::int64_t, 2>> split
        = separator == std::string_view::npos
        ? std::nullopt
        : [&]() -> std::optional<std::array<std::int64_t, 2>>
    {
        const auto start = cefast::parse_iso_time(text.substr(0, separator));
        const auto stop = cefast::parse_iso_time(text.substr(separator + 1));
        if (!start || !stop)
            return std::nullopt;
        return std::array<std::int64_t, 2> { *start, *stop };
    }();
    if (range != split)
        report("range disagrees with its two halves", text);
    return 0;
}
