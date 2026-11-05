#include <catch2/catch_test_macros.hpp>

#include <cefast/gzip.hpp>
#include <cefast/input.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

using namespace cefast;

namespace
{
const std::filesystem::path fixtures { std::string { CEFAST_TEST_RESOURCES } + "/fixtures" };

const std::string plain_text
    = "FILE_FORMAT_VERSION = \"CEF-2.0\"\n1970-01-01T00:00:00Z, 1\nEND_DATA\n";

std::string read_bytes(const std::filesystem::path& path)
{
    std::ifstream file { path, std::ios::binary };
    return std::string { std::istreambuf_iterator<char> { file },
        std::istreambuf_iterator<char> { } };
}

std::span<const char> as_span(const std::string& text)
{
    return std::span<const char> { text.data(), text.size() };
}

std::size_t count_gzip_magic(const std::string& bytes)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i + 1 < bytes.size(); ++i)
        if (static_cast<unsigned char>(bytes[i]) == 0x1f
            && static_cast<unsigned char>(bytes[i + 1]) == 0x8b)
            ++count;
    return count;
}
}

TEST_CASE("gzip roundtrip", "[input]")
{
    const auto compressed = gzip(plain_text);
    REQUIRE(compressed.size() >= 2);
    CHECK(static_cast<unsigned char>(compressed[0]) == 0x1f);
    CHECK(static_cast<unsigned char>(compressed[1]) == 0x8b);
    CHECK(gunzip(as_span(compressed), "<memory>") == plain_text);
}

TEST_CASE("plain bytes pass through", "[input]")
{
    const auto input = read_input(as_span(plain_text));
    CHECK(input.bytes == plain_text);
    CHECK(input.source == "<memory>");
    CHECK_FALSE(input.directory.has_value());
}

TEST_CASE("gzip single member", "[input]")
{
    const auto expected = read_bytes(fixtures / "hello.cef");
    const auto input = read_input(fixtures / "hello.cef.gz");
    CHECK(input.bytes == expected);
    CHECK(input.source == (fixtures / "hello.cef.gz").string());
    REQUIRE(input.directory.has_value());
    CHECK(*input.directory == fixtures);
}

TEST_CASE("two gzip members are concatenated", "[input]")
{
    const auto expected = read_bytes(fixtures / "hello.cef");
    REQUIRE(count_gzip_magic(read_bytes(fixtures / "two_members.cef.gz")) == 2);
    CHECK(read_input(fixtures / "two_members.cef.gz").bytes == expected);
}

TEST_CASE("truncated gzip raises", "[input]")
{
    REQUIRE_THROWS_AS(read_input(fixtures / "truncated.cef.gz"), parse_error);
    REQUIRE_THROWS_AS(
        gunzip(as_span(read_bytes(fixtures / "truncated.cef.gz")), "truncated"), parse_error);
}

TEST_CASE("gzip with trailing zeros", "[input]")
{
    const auto expected = read_bytes(fixtures / "hello.cef");
    CHECK(read_input(fixtures / "trailing_zeros.cef.gz").bytes == expected);
}

TEST_CASE("missing file raises", "[input]")
{
    REQUIRE_THROWS_AS(read_input(fixtures / "does_not_exist.cef"), parse_error);
}

TEST_CASE("gzip detected without extension", "[input]")
{
    const auto path = std::filesystem::temp_directory_path() / "cefast_input_no_extension.cef";
    {
        std::ofstream file { path, std::ios::binary };
        const auto compressed = gzip(plain_text);
        file.write(compressed.data(), static_cast<std::streamsize>(compressed.size()));
    }
    const auto input = read_input(path);
    CHECK(input.bytes == plain_text);
    std::filesystem::remove(path);
}
