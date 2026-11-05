#pragma once
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "cefast/errors.hpp"
#include "cefast/gzip.hpp"

namespace cefast
{
struct input_buffer
{
    std::string bytes;
    std::string source;
    // Directory the bytes came from, for INCLUDE resolution; empty for in-memory input.
    std::optional<std::filesystem::path> directory;
};

namespace detail
{
    inline bool is_gzip(std::span<const char> bytes)
    {
        return bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0x1f
            && static_cast<unsigned char>(bytes[1]) == 0x8b;
    }

    inline std::string read_file(const std::filesystem::path& path)
    {
        std::ifstream file { path, std::ios::binary };
        const auto failure = [&path] { return parse_error(path.string(), 0, "cannot read file"); };
        if (!file)
            throw failure();
        std::string bytes { std::istreambuf_iterator<char> { file },
            std::istreambuf_iterator<char> { } };
        if (file.bad())
            throw failure();
        return bytes;
    }

    inline std::span<const char> as_span(const std::string& bytes)
    {
        return std::span<const char> { bytes.data(), bytes.size() };
    }
}

// simplify: the whole file is read into memory; mmap/streaming is the performance plan's job.
[[nodiscard]] inline input_buffer read_input(const std::filesystem::path& path)
{
    auto bytes = detail::read_file(path);
    const auto source = path.string();
    if (detail::is_gzip(detail::as_span(bytes)))
        bytes = gunzip(detail::as_span(bytes), source);
    return input_buffer { std::move(bytes), source, path.parent_path() };
}

[[nodiscard]] inline input_buffer read_input(std::span<const char> bytes)
{
    auto owned = detail::is_gzip(bytes) ? gunzip(bytes, "<memory>")
                                        : std::string { bytes.data(), bytes.size() };
    return input_buffer { std::move(owned), "<memory>", std::nullopt };
}
}
