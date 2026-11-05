// Loads a CEF file (or a directory of them) repeatedly and reports the median wall time, the
// throughput and the nanoseconds spent per parsed value. Used for the round-1 profile and to give
// `perf record` a workload with a clean parse-only hot loop.
#include <cefast/cefast.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <variant>
#include <vector>

namespace
{
namespace fs = std::filesystem;

[[nodiscard]] std::vector<fs::path> inputs_of(const fs::path& path)
{
    std::vector<fs::path> files;
    if (fs::is_directory(path))
    {
        for (const auto& entry : fs::directory_iterator { path })
            if (entry.path().extension() == ".cef")
                files.push_back(entry.path());
        std::ranges::sort(files);
    }
    else
        files.push_back(path);
    return files;
}

[[nodiscard]] std::size_t value_count(const cefast::cef_file& file)
{
    std::size_t count = 0;
    for (const auto& var : file.variables)
        count += std::visit([](const auto& values) { return values.size(); }, var.values);
    return count;
}
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <file-or-directory> [iterations] [threads]\n", argv[0]);
        return 2;
    }
    const auto files = inputs_of(argv[1]);
    const int iterations = argc > 2 ? std::atoi(argv[2]) : 200;
    // 0 = one thread per core (the library default), 1 = the serial reference parser.
    cefast::load_options options;
    options.threads = argc > 3 ? static_cast<std::size_t>(std::atoi(argv[3])) : 0;
    if (files.empty() || iterations <= 0)
    {
        std::fprintf(stderr, "nothing to load\n");
        return 2;
    }

    std::size_t bytes = 0;
    for (const auto& path : files)
        bytes += static_cast<std::size_t>(fs::file_size(path));

    std::vector<double> timings;
    std::size_t values = 0;
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        const auto start = std::chrono::steady_clock::now();
        for (const auto& path : files)
        {
            const auto parsed = cefast::load(path, options);
            // Counting walks every value: do it once, outside the timed loop's steady state.
            if (iteration == 0)
                values += value_count(parsed);
        }
        timings.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
    }

    // Short benchmarks live in turbo range: the minimum is steadier than the median.
    const double fastest = *std::ranges::min_element(timings);
    std::ranges::sort(timings);
    const double median = timings[static_cast<std::size_t>(timings.size()) / 2];
    const double megabytes = static_cast<double>(bytes) / 1048576.0;
    const double values_per_run = static_cast<double>(std::max<std::size_t>(values, 1));
    std::printf(
        "%zu file(s), %.2f MB, %d iterations: min %.2f ms (median %.2f), %.1f MB/s, "
        "%.2f ns/value, %zu values\n",
        files.size(), megabytes, iterations, fastest, median, megabytes / (fastest / 1000.0),
        fastest * 1e6 / values_per_run, values);
    return 0;
}
