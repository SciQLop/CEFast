#include <cefast/cefast.hpp>

#include "compare.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

// cefast::load must either return a file or throw parse_error: anything else (std::bad_alloc from
// a crafted size, an out_of_range, a crash) is a bug. The serial and parallel paths must agree on
// every byte of the result and on the exact error message.
namespace
{
struct outcome
{
    bool ok = false;
    std::string error;
    cefast::cef_file file;
};

outcome attempt(std::span<const char> bytes, std::size_t threads)
{
    cefast::load_options options;
    options.threads = threads;
    try
    {
        return { true, { }, cefast::load(bytes, options) };
    }
    catch (const cefast::parse_error& error)
    {
        return { false, error.what(), { } };
    }
}

[[noreturn]] void report(const char* what, std::string_view serial, std::string_view parallel)
{
    std::fprintf(stderr,
        "fuzz_load: %s\n--- serial ---\n%.*s\n--- parallel ---\n%.*s\n", what,
        static_cast<int>(serial.size()), serial.data(), static_cast<int>(parallel.size()),
        parallel.data());
    __builtin_trap();
}

// Lowers the parallel threshold so a small input takes the parallel path too.
struct threshold_guard
{
    std::size_t saved = cefast::detail::parallel_threshold;

    explicit threshold_guard(std::size_t value) { cefast::detail::parallel_threshold = value; }
    ~threshold_guard() { cefast::detail::parallel_threshold = saved; }
};
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::span<const char> bytes { reinterpret_cast<const char*>(data), size };
    const threshold_guard lowered { 0 };
    const auto serial = attempt(bytes, 1);
    const auto parallel = attempt(bytes, 4);
    if (serial.ok != parallel.ok)
        report("accept/reject mismatch", serial.error, parallel.error);
    if (!serial.ok)
    {
        if (serial.error != parallel.error)
            report("error message mismatch", serial.error, parallel.error);
    }
    else if (!fuzz::same_file(serial.file, parallel.file))
        report(fuzz::describe_difference(serial.file, parallel.file).c_str(), { }, { });
    return 0;
}
