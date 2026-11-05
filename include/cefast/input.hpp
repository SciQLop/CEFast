#pragma once
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include <cpp_utils/io/memory_mapped_file.hpp>

#include "cefast/errors.hpp"
#include "cefast/gzip.hpp"

namespace cefast
{
namespace detail
{
    [[nodiscard]] inline bool is_gzip(std::span<const char> bytes) noexcept
    {
        return bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0x1f
            && static_cast<unsigned char>(bytes[1]) == 0x8b;
    }

    [[nodiscard]] inline bool exists(const std::filesystem::path& path) noexcept
    {
        std::error_code error;
        return std::filesystem::exists(path, error) && !std::filesystem::is_directory(path, error);
    }

    template <typename Fail>
    [[nodiscard]] inline std::string read_file(const std::filesystem::path& path, Fail&& fail)
    {
        std::ifstream stream { path, std::ios::binary };
        if (!stream)
            throw fail();
        return { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> { } };
    }
}

// Storage for the text being parsed, with a view on it. A plain file is mapped, not read: the
// kernel's page cache is the only copy. A gzip file is mapped and then inflated into a string, the
// mapping is dropped. In-memory plain bytes are borrowed from the caller, who must keep them alive
// for the duration of load() (the Python wrapper exports the buffer for exactly that long).
struct input_buffer
{
    // memory_mapped_file is move-only (cpp_utils >= 4ac71f8), so the variant owns the mapping
    // directly: copying it would unmap the same mapping twice.
    using mapped_file = cpp_utils::io::memory_mapped_file;
    using storage_type = std::variant<std::monostate, mapped_file, std::string>;

    storage_type storage;
    std::string_view text;
    // Same view under its pre-mmap name, kept so existing callers and tests read the same text.
    std::string_view bytes;
    std::string source;
    // Directory the text came from, for INCLUDE resolution; empty for in-memory input.
    std::optional<std::filesystem::path> directory;

    input_buffer() = default;
    input_buffer(const input_buffer&) = delete;
    input_buffer& operator=(const input_buffer&) = delete;

    // Moving has to rebuild the view only for owned text (a short string lives inside the
    // variant's storage); borrowed and mapped views point at memory that does not move.
    input_buffer(input_buffer&& other) noexcept
            : storage(std::move(other.storage))
            , text(std::holds_alternative<std::string>(storage) ? view_of(storage) : other.text)
            , bytes(text)
            , source(std::move(other.source))
            , directory(std::move(other.directory))
    {
    }

    input_buffer& operator=(input_buffer&& other) noexcept
    {
        storage = std::move(other.storage);
        text = std::holds_alternative<std::string>(storage) ? view_of(storage) : other.text;
        bytes = text;
        source = std::move(other.source);
        directory = std::move(other.directory);
        return *this;
    }

    input_buffer(std::string owned, std::string source_name,
        std::optional<std::filesystem::path> source_directory = { })
            : storage(std::move(owned))
            , text(std::get<std::string>(storage))
            , bytes(text)
            , source(std::move(source_name))
            , directory(std::move(source_directory))
    {
    }

    [[nodiscard]] static input_buffer mapped(mapped_file file, std::string source_name,
        std::optional<std::filesystem::path> source_directory = { })
    {
        input_buffer result;
        result.storage = std::move(file);
        result.text = view_of(result.storage);
        result.bytes = result.text;
        result.source = std::move(source_name);
        result.directory = std::move(source_directory);
        return result;
    }

private:
    [[nodiscard]] static std::string_view view_of(const storage_type& storage) noexcept
    {
        if (const auto* owned = std::get_if<std::string>(&storage))
            return *owned;
        if (const auto* file = std::get_if<mapped_file>(&storage);
            file != nullptr && file->is_valid())
            return std::string_view { file->data(), file->size() };
        return { };
    }
};

// simplify: a file that is rewritten or truncated under the mapping is undefined; a file that
// shrinks below its header is caught by the lexer, not by the mapping.
[[nodiscard]] inline input_buffer read_input(const std::filesystem::path& path)
{
    const auto source = path.string();
    const auto fail = [&source] { return parse_error(source, 0, "cannot read file"); };

    if (!detail::exists(path))
        throw fail();

    std::error_code error;
    if (std::filesystem::file_size(path, error) == 0)
        return input_buffer { std::string { }, source, path.parent_path() };

    auto mapping = cpp_utils::io::memory_mapped_file { source };
    // WebAssembly has no mmap: fall back to reading the file, which is what a Pyodide user has
    // anyway (the bytes are already in the browser's in-memory filesystem).
    if (!mapping.is_valid())
    {
        auto text = detail::read_file(path, fail);
        if (!detail::is_gzip(std::span<const char> { text }))
            return input_buffer { std::move(text), source, path.parent_path() };
        return input_buffer { gunzip(text, source), source, path.parent_path() };
    }

    const std::span<const char> mapped { mapping.data(), mapping.size() };
    if (!detail::is_gzip(mapped))
        return input_buffer::mapped(std::move(mapping), source, path.parent_path());

    return input_buffer { gunzip(mapped, source), source, path.parent_path() };
}

// Plain bytes are borrowed: text.data() points into the caller's buffer.
[[nodiscard]] inline input_buffer read_input(std::span<const char> bytes)
{
    if (detail::is_gzip(bytes))
        return input_buffer { gunzip(bytes, "<memory>"), "<memory>", std::nullopt };

    input_buffer result;
    result.text = std::string_view { bytes.data(), bytes.size() };
    result.bytes = result.text;
    result.source = "<memory>";
    return result;
}
}
