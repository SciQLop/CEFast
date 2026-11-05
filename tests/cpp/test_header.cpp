#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

#include <cefast/header/parser.hpp>
#include <cefast/input.hpp>

using namespace cefast;

namespace
{
// Counts allocations issued while the scope is armed, to keep the header path free of per-line
// allocations: the lexer hands out views into the input text and owns only the upper-cased keys
// and the joined continuation lines.
std::size_t allocation_count = 0;
bool count_allocations = false;

struct allocation_scope
{
    allocation_scope() { reset(); }
    ~allocation_scope() { count_allocations = false; }
    void reset()
    {
        allocation_count = 0;
        count_allocations = true;
    }
    void stop() { count_allocations = false; }
    [[nodiscard]] std::size_t count() const { return allocation_count; }
};

// malloc for every form of new, free for every form of delete: mixing our replacement with the
// sanitiser's intercepted nothrow/aligned forms is an alloc-dealloc-mismatch under ASan.
void note_allocation()
{
    if (count_allocations)
        ++allocation_count;
}

void* test_allocation(std::size_t size)
{
    note_allocation();
    void* block = std::malloc(size == 0 ? 1 : size);
    if (block == nullptr)
        throw std::bad_alloc { };
    return block;
}

// MSVC has no std::aligned_alloc, and an _aligned_malloc block must go back to _aligned_free,
// never to free: pairing the two here keeps every replaced form's release matching.
void* test_aligned_allocation(std::size_t size, std::size_t alignment)
{
    note_allocation();
#if defined(_MSC_VER)
    void* block = _aligned_malloc(size == 0 ? 1 : size, alignment);
#else
    void* block = std::aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
#endif
    if (block == nullptr)
        throw std::bad_alloc { };
    return block;
}

void test_aligned_free(void* block) noexcept
{
#if defined(_MSC_VER)
    _aligned_free(block);
#else
    std::free(block);
#endif
}
}

void* operator new(std::size_t size)
{
    return test_allocation(size);
}

void* operator new[](std::size_t size)
{
    return test_allocation(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return test_allocation(size);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return test_allocation(size);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return test_aligned_allocation(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return test_aligned_allocation(size, static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try
    {
        return test_aligned_allocation(size, static_cast<std::size_t>(alignment));
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try
    {
        return test_aligned_allocation(size, static_cast<std::size_t>(alignment));
    }
    catch (...)
    {
        return nullptr;
    }
}

void operator delete(void* block) noexcept
{
    std::free(block);
}

void operator delete[](void* block) noexcept
{
    std::free(block);
}

void operator delete(void* block, const std::nothrow_t&) noexcept
{
    std::free(block);
}

void operator delete[](void* block, const std::nothrow_t&) noexcept
{
    std::free(block);
}

void operator delete(void* block, std::size_t) noexcept
{
    std::free(block);
}

void operator delete[](void* block, std::size_t) noexcept
{
    std::free(block);
}

void operator delete(void* block, std::align_val_t) noexcept
{
    test_aligned_free(block);
}

void operator delete[](void* block, std::align_val_t) noexcept
{
    test_aligned_free(block);
}

void operator delete(void* block, std::size_t, std::align_val_t) noexcept
{
    test_aligned_free(block);
}

void operator delete[](void* block, std::size_t, std::align_val_t) noexcept
{
    test_aligned_free(block);
}

namespace
{
std::filesystem::path fixture(std::string_view relative)
{
    return std::filesystem::path { CEFAST_TEST_RESOURCES } / "fixtures" / relative;
}

input_buffer memory_input(std::string text)
{
    return input_buffer { std::move(text), "test.cef", std::nullopt };
}

schema parse(std::string_view text, const load_options& options = { })
{
    auto input = memory_input(std::string { text });
    const auto lexed = lex_header(input.bytes, input.source);
    return parse_header(lexed, input, options);
}

schema parse_file(const std::filesystem::path& path, const load_options& options = { })
{
    auto input = read_input(path);
    const auto lexed = lex_header(input.bytes, input.source);
    return parse_header(lexed, input, options);
}
}

TEST_CASE("header: minimal file", "[header]")
{
    const auto result = parse(R"(FILE_NAME = "x.cef"
FILE_FORMAT_VERSION = "CEF-2.0"
START_VARIABLE = time_tags
  VALUE_TYPE = ISO_TIME
  SIZES = 1
END_VARIABLE = time_tags
START_VARIABLE = B_vec
  VALUE_TYPE = FLOAT
  SIZES = 3
END_VARIABLE = B_vec
DATA_UNTIL = END_DATA
)");
    REQUIRE(result.file.variables.size() == 2);
    CHECK(result.file.variables[0].name == "time_tags");
    CHECK(result.file.variables[0].type == value_type::ISO_TIME);
    CHECK(result.file.variables[0].sizes == std::vector<std::size_t> { 1 });
    CHECK_FALSE(result.file.variables[0].is_nrv);
    CHECK(result.file.variables[1].name == "B_vec");
    CHECK(result.file.variables[1].type == value_type::FLOAT);
    CHECK(result.file.variables[1].sizes == std::vector<std::size_t> { 3 });
    CHECK(result.record_marker == "\n");
}

TEST_CASE("header: version missing raises", "[header]")
{
    CHECK_THROWS_AS(parse("FILE_NAME = \"x.cef\"\nDATA_UNTIL = END_DATA\n"), parse_error);
}

TEST_CASE("header: CEF-1 version raises", "[header]")
{
    try
    {
        parse("FILE_FORMAT_VERSION = \"CEF-1.0\"\nDATA_UNTIL = END_DATA\n");
        FAIL("expected parse_error");
    }
    catch (const parse_error& error)
    {
        CHECK(std::string(error.what()).find("CEF-1.0") != std::string::npos);
    }
}

TEST_CASE("header: metadata block entries from a real header", "[header]")
{
    const auto result = parse_file(fixture("whi-header.cef"));
    const auto* region = find(result.file.globals, "MISSION_REGION");
    REQUIRE(region);
    REQUIRE(region->entries.size() == 11);
    CHECK(std::get<std::string>(region->entries.front()) == "Solar_Wind");
    CHECK(std::get<std::string>(region->entries.back()) == "Plasmasphere");
}

TEST_CASE("header: ISO_TIME_RANGE metadata value type", "[header]")
{
    const auto result = parse_file(fixture("whi-header.cef"));
    const auto* span = find(result.file.globals, "MISSION_TIME_SPAN");
    REQUIRE(span);
    REQUIRE(span->entries.size() == 1);
    const auto range = std::get<iso_time_range>(span->entries.front());
    CHECK(range.start == 963705600000000000);
}

TEST_CASE("header: untyped entries keep their text", "[header]")
{
    const auto result = parse(R"(FILE_FORMAT_VERSION = "CEF-2.0"
START_META = VERSION_NUMBER
  ENTRY = 090213
END_META = VERSION_NUMBER
START_META = GENERATION_DATE
  VALUE_TYPE = ISO_TIME
  ENTRY = 2026-11-05T18:55:25Z
END_META = GENERATION_DATE
START_VARIABLE = v
  VALUE_TYPE = FLOAT
  DELTA_PLUS = 11.250
END_VARIABLE = v
DATA_UNTIL = END_DATA
)");
    const auto* version = find(result.file.globals, "VERSION_NUMBER");
    REQUIRE(version);
    REQUIRE(version->entries.size() == 1);
    CHECK(std::get<std::string>(version->entries.front()) == "090213");

    const auto* generated = find(result.file.globals, "GENERATION_DATE");
    REQUIRE(generated);
    CHECK(std::holds_alternative<iso_time>(generated->entries.front()));

    REQUIRE(result.file.variables.size() == 1);
    const auto* delta = find(result.file.variables[0].attrs, "DELTA_PLUS");
    REQUIRE(delta);
    CHECK(std::get<std::string>(delta->entries.front()) == "11.250");
}

TEST_CASE("header: FILLVAL is typed by the variable type", "[header]")
{
    const auto result = parse(R"(FILE_FORMAT_VERSION = "CEF-2.0"
START_VARIABLE = count
  VALUE_TYPE = INT
  FILLVAL = 99
END_VARIABLE = count
START_VARIABLE = density
  VALUE_TYPE = FLOAT
  FILLVAL = -1e+31
END_VARIABLE = density
START_VARIABLE = time_tags
  VALUE_TYPE = ISO_TIME
  FILLVAL = "9999-99-99T99:99:99Z"
END_VARIABLE = time_tags
DATA_UNTIL = END_DATA
)");
    REQUIRE(result.file.variables.size() == 3);

    const auto* count_fill = find(result.file.variables[0].attrs, "FILLVAL");
    REQUIRE(count_fill);
    CHECK(std::get<std::int64_t>(count_fill->entries.front()) == 99);
    CHECK(result.file.variables[0].fillval_token == "99");

    const auto* density_fill = find(result.file.variables[1].attrs, "FILLVAL");
    REQUIRE(density_fill);
    CHECK(std::get<double>(density_fill->entries.front()) == -1e+31);
    CHECK(result.file.variables[1].fillval_token == "-1e+31");

    const auto* time_fill = find(result.file.variables[2].attrs, "FILLVAL");
    REQUIRE(time_fill);
    CHECK(std::get<std::string>(time_fill->entries.front()) == "9999-99-99T99:99:99Z");
    CHECK(result.file.variables[2].fillval_token == "9999-99-99T99:99:99Z");
}

TEST_CASE("header: quoted SIZES with spaces", "[header]")
{
    const auto result = parse(R"(FILE_FORMAT_VERSION = "CEF-2.0"
START_VARIABLE = spectrogram
  VALUE_TYPE = FLOAT
  SIZES = "64, 6, 32"
END_VARIABLE = spectrogram
DATA_UNTIL = END_DATA
)");
    REQUIRE(result.file.variables.size() == 1);
    CHECK(result.file.variables[0].sizes == std::vector<std::size_t> { 64, 6, 32 });
}

TEST_CASE("header: non-record-varying DATA", "[header]")
{
    const auto result = parse(R"(FILE_FORMAT_VERSION = "CEF-2.0"
START_VARIABLE = theta
  VALUE_TYPE = FLOAT
  SIZES = 8
  DATA = -78.750, -56.250, -33.750, -11.250, 11.250, 33.750, 56.250, 78.750
END_VARIABLE = theta
DATA_UNTIL = END_DATA
)");
    REQUIRE(result.file.variables.size() == 1);
    CHECK(result.file.variables[0].is_nrv);
    REQUIRE(result.nrv_data.size() == 1);
    REQUIRE(result.nrv_data[0].size() == 8);
    CHECK(result.nrv_data[0][0].text == "-78.750");
}

TEST_CASE("header: record marker forms", "[header]")
{
    const auto unquoted = parse(
        "FILE_FORMAT_VERSION = \"CEF-2.0\"\nEND_OF_RECORD_MARKER = $\nDATA_UNTIL = END_DATA\n");
    CHECK(unquoted.record_marker == "$");
    const auto quoted = parse(
        "FILE_FORMAT_VERSION = \"CEF-2.0\"\nEND_OF_RECORD_MARKER = \";\"\nDATA_UNTIL = END_DATA\n");
    CHECK(quoted.record_marker == ";");
}

TEST_CASE("header: DATA_UNTIL forms", "[header]")
{
    const auto end_data = parse("FILE_FORMAT_VERSION = \"CEF-2.0\"\nDATA_UNTIL = END_DATA\n");
    CHECK(end_data.data_until == "END_DATA");
    const auto eof = parse("FILE_FORMAT_VERSION = \"CEF-2.0\"\nDATA_UNTIL = EOF\n");
    CHECK(eof.data_until.empty());
}

TEST_CASE("header: INCLUDE from the file directory", "[header]")
{
    const auto result = parse_file(fixture("include/main.cef"));
    const auto* mission = find(result.file.globals, "MISSION");
    REQUIRE(mission);
    CHECK(std::get<std::string>(mission->entries.front()) == "Cluster");
    const auto* region = find(result.file.globals, "MISSION_REGION");
    REQUIRE(region);
    CHECK(std::get<std::string>(region->entries.front()) == "Cusp");
}

TEST_CASE("header: INCLUDE from the resolver", "[header]")
{
    load_options options;
    options.include_resolver = [](std::string_view name) -> std::optional<std::string>
    {
        if (name == "common.ceh")
            return std::string {
                "START_META = MISSION\n  ENTRY = \"Cluster\"\nEND_META = MISSION\n"
            };
        return std::nullopt;
    };
    const auto result = parse(R"(FILE_FORMAT_VERSION = "CEF-2.0"
INCLUDE = "common.ceh"
DATA_UNTIL = END_DATA
)",
        options);
    const auto* mission = find(result.file.globals, "MISSION");
    REQUIRE(mission);
    CHECK(std::get<std::string>(mission->entries.front()) == "Cluster");
}

TEST_CASE("header: nested INCLUDE", "[header]")
{
    load_options options;
    options.include_resolver = [](std::string_view name) -> std::optional<std::string>
    {
        if (name == "outer.ceh")
            return std::string { "INCLUDE = \"inner.ceh\"\n" };
        if (name == "inner.ceh")
            return std::string {
                "START_META = MISSION\n  ENTRY = \"Cluster\"\nEND_META = MISSION\n"
            };
        return std::nullopt;
    };
    const auto result = parse(
        "FILE_FORMAT_VERSION = \"CEF-2.0\"\nINCLUDE = \"outer.ceh\"\nDATA_UNTIL = END_DATA\n",
        options);
    CHECK(find(result.file.globals, "MISSION") != nullptr);
}

TEST_CASE("header: missing INCLUDE raises", "[header]")
{
    try
    {
        parse("FILE_FORMAT_VERSION = \"CEF-2.0\"\nINCLUDE = \"nope.ceh\"\nDATA_UNTIL = END_DATA\n");
        FAIL("expected parse_error");
    }
    catch (const parse_error& error)
    {
        CHECK(std::string(error.what()).find("INCLUDE file not found: nope.ceh")
            != std::string::npos);
    }
}

TEST_CASE("header: INCLUDE cycle raises", "[header]")
{
    CHECK_THROWS_AS(parse_file(fixture("include/cycle-a.cef")), parse_error);
}

TEST_CASE("header: error in an included file names it", "[header]")
{
    try
    {
        parse_file(fixture("include/broken.cef"));
        FAIL("expected parse_error");
    }
    catch (const parse_error& error)
    {
        CHECK(std::string(error.what()).find("broken.ceh:1:") == 0);
    }
}

TEST_CASE("header: END_META name mismatch raises", "[header]")
{
    CHECK_THROWS_AS(parse(R"(START_META = MISSION
  ENTRY = "Cluster"
END_META = MISSION_REGION
DATA_UNTIL = END_DATA
)"),
        parse_error);
}

TEST_CASE("header: END_VARIABLE name mismatch raises", "[header]")
{
    CHECK_THROWS_AS(parse(R"(START_VARIABLE = a
  VALUE_TYPE = FLOAT
END_VARIABLE = b
DATA_UNTIL = END_DATA
)"),
        parse_error);
}

TEST_CASE("header: START_VARIABLE inside a block raises", "[header]")
{
    CHECK_THROWS_AS(parse(R"(START_VARIABLE = a
  VALUE_TYPE = FLOAT
START_VARIABLE = b
END_VARIABLE = b
END_VARIABLE = a
DATA_UNTIL = END_DATA
)"),
        parse_error);
}

TEST_CASE("header: the WHI header is lexed and parsed without per-line allocations", "[header]")
{
    const std::string text = [&]
    {
        std::ifstream stream { fixture("whi-header.cef"), std::ios::binary };
        REQUIRE(stream);
        return std::string { std::istreambuf_iterator<char> { stream }, { } };
    }();
    const auto lines = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));

    auto input = input_buffer { text, "whi-header.cef", std::nullopt };
    allocation_scope scope;
    const auto lexed = lex_header(input.bytes, input.source);
    const auto lex_allocations = scope.count();
    scope.reset();
    const auto file = parse_header(lexed, input, { });
    const auto parse_allocations = scope.count();
    scope.stop();

    INFO("lines: " << lines << ", lex: " << lex_allocations << ", parse: " << parse_allocations);
    // 472 lines: 62 allocations while lexing (against 564 when every key and token was a
    // std::string) and 550 with the model built, against 1599 before.
    CHECK(lex_allocations < lines / 4);
    CHECK(lex_allocations + parse_allocations < 2 * lines);
    CHECK(file.file.variables.size() == 10);
}
