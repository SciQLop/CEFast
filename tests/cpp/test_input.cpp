#include <catch2/catch_test_macros.hpp>

#include <cefast/cefast.hpp>
#include <cefast/gzip.hpp>
#include <cefast/input.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

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

namespace
{
void require_same_column(const cefast::variable& left, const cefast::variable& right)
{
    REQUIRE(left.type == right.type);
    REQUIRE(left.sizes == right.sizes);
    REQUIRE(left.records == right.records);
    REQUIRE(left.values.index() == right.values.index());
    std::visit(
        [&right](const auto& values)
        {
            const auto& other = std::get<std::decay_t<decltype(values)>>(right.values);
            REQUIRE(values.size() == other.size());
            for (std::size_t index = 0; index < values.size(); ++index)
                REQUIRE(values[index] == other[index]);
        },
        left.values);
}
}

TEST_CASE("a mapped file equals the same bytes in memory", "[input]")
{
    const auto path = std::filesystem::path { std::string { CEFAST_TEST_RESOURCES } } / "csa"
        / "C1_CP_FGM_SPIN__20050610_000000_20050610_002000_V140305.cef";
    REQUIRE(std::filesystem::exists(path));
    const auto text = read_bytes(path);

    const auto from_path = load(path);
    const auto from_bytes = load(std::span<const char> { text.data(), text.size() });

    REQUIRE(from_path.variables.size() == from_bytes.variables.size());
    for (std::size_t index = 0; index < from_path.variables.size(); ++index)
        require_same_column(from_path.variables[index], from_bytes.variables[index]);
}

TEST_CASE("an empty file raises instead of crashing", "[input]")
{
    const auto path = std::filesystem::temp_directory_path() / "cefast_input_empty.cef";
    {
        std::ofstream file { path, std::ios::binary };
    }
    CHECK(read_input(path).text.empty());
    try
    {
        load(path);
        FAIL("an empty file was accepted");
    }
    catch (const parse_error& error)
    {
        CHECK(std::string { error.what() }.find("DATA_UNTIL") != std::string_view::npos);
    }
    std::filesystem::remove(path);
}

TEST_CASE("in-memory input is borrowed, not copied", "[input]")
{
    const auto input = read_input(as_span(plain_text));
    CHECK(input.text.data() == plain_text.data());
    CHECK(input.text.size() == plain_text.size());
}
