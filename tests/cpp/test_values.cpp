#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <cefast/header/parser.hpp>
#include <cefast/input.hpp>
#include <cefast/values.hpp>

using namespace cefast;

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

schema parse(std::string_view text)
{
    auto input = memory_input(std::string { text });
    const auto lexed = lex_header(input.bytes, input.source);
    return parse_header(lexed, input, { });
}

schema parse_file(const std::filesystem::path& path)
{
    auto input = read_input(path);
    const auto lexed = lex_header(input.bytes, input.source);
    return parse_header(lexed, input, { });
}

std::string one_variable(value_type type, std::string_view extra, std::string_view data)
{
    std::string text { "FILE_FORMAT_VERSION = \"CEF-2.0\"\nSTART_VARIABLE = v\n" };
    text += "  VALUE_TYPE = ";
    text += type == value_type::FLOAT        ? "FLOAT"
        : type == value_type::DOUBLE         ? "DOUBLE"
        : type == value_type::INT            ? "INT"
        : type == value_type::BYTE           ? "BYTE"
        : type == value_type::ISO_TIME       ? "ISO_TIME"
        : type == value_type::ISO_TIME_RANGE ? "ISO_TIME_RANGE"
                                             : "CHAR";
    text += '\n';
    text += extra;
    text += "  DATA = ";
    text += data;
    text += "\nEND_VARIABLE = v\nDATA_UNTIL = END_DATA\n";
    return text;
}
}

TEST_CASE("values: float", "[values]")
{
    column values = make_column(value_type::FLOAT);
    REQUIRE(append_value(values, value_type::FLOAT, "-1.00000e+31", ""));
    REQUIRE(std::get<cpp_utils::containers::no_init_vector<float>>(values).size() == 1);
    CHECK(std::get<cpp_utils::containers::no_init_vector<float>>(values)[0] == -1e31f);
}

TEST_CASE("values: double keeps full precision", "[values]")
{
    column values = make_column(value_type::DOUBLE);
    REQUIRE(append_value(values, value_type::DOUBLE, "1.2345678901234567", ""));
    CHECK(std::get<cpp_utils::containers::no_init_vector<double>>(values)[0] == 1.2345678901234567);
}

TEST_CASE("values: INT accepts leading zero and plus", "[values]")
{
    column values = make_column(value_type::INT);
    REQUIRE(append_value(values, value_type::INT, "05", ""));
    REQUIRE(append_value(values, value_type::INT, "+7", ""));
    const auto& ints = std::get<cpp_utils::containers::no_init_vector<std::int32_t>>(values);
    REQUIRE(ints.size() == 2);
    CHECK(ints[0] == 5);
    CHECK(ints[1] == 7);
}

TEST_CASE("values: INT rejects garbage", "[values]")
{
    column values = make_column(value_type::INT);
    CHECK_FALSE(append_value(values, value_type::INT, "5x", ""));
}

TEST_CASE("values: INT rejects values outside int32", "[values]")
{
    column values = make_column(value_type::INT);
    CHECK_FALSE(append_value(values, value_type::INT, "2147483648", ""));
}

TEST_CASE("values: BYTE accepts 0..255", "[values]")
{
    column values = make_column(value_type::BYTE);
    for (const auto* token : { "0", "1", "127", "255" })
        REQUIRE(append_value(values, value_type::BYTE, token, ""));
    const auto& bytes = std::get<cpp_utils::containers::no_init_vector<std::uint8_t>>(values);
    REQUIRE(bytes.size() == 4);
    CHECK(bytes[0] == 0);
    CHECK(bytes[3] == 255);
}

TEST_CASE("values: BYTE rejects 256", "[values]")
{
    column values = make_column(value_type::BYTE);
    CHECK_FALSE(append_value(values, value_type::BYTE, "256", ""));
}

TEST_CASE("values: time fill becomes NaT", "[values]")
{
    column values = make_column(value_type::ISO_TIME);
    REQUIRE(
        append_value(values, value_type::ISO_TIME, "9999-99-99T99:99:99Z", "9999-99-99T99:99:99Z"));
    CHECK(std::get<cpp_utils::containers::no_init_vector<std::int64_t>>(values)[0] == nat);
}

TEST_CASE("values: invalid time that is not the fill fails", "[values]")
{
    column values = make_column(value_type::ISO_TIME);
    CHECK_FALSE(
        append_value(values, value_type::ISO_TIME, "9999-99-99T99:99:99Z", "9999-12-31T23:59:59Z"));
}

TEST_CASE("values: time range stores start and stop", "[values]")
{
    column values = make_column(value_type::ISO_TIME_RANGE);
    REQUIRE(append_value(
        values, value_type::ISO_TIME_RANGE, "2000-07-16T00:00:00Z/2026-12-31T23:59:59Z", ""));
    const auto& ints = std::get<cpp_utils::containers::no_init_vector<std::int64_t>>(values);
    REQUIRE(ints.size() == 2);
    CHECK(ints[0] == 963705600000000000);
}

TEST_CASE("values: CHAR keeps quoted spaces", "[values]")
{
    column values = make_column(value_type::CHAR);
    REQUIRE(append_value(values, value_type::CHAR, " a b ", ""));
    CHECK(std::get<std::vector<std::string>>(values)[0] == " a b ");
}

TEST_CASE("values: CODIF theta data", "[values]")
{
    const auto result = parse_file(fixture("codif-header.cef"));
    REQUIRE(result.file.variables.size() > 0);
    const variable* theta = nullptr;
    for (const auto& var : result.file.variables)
        if (var.name == "theta__C1_CP_CIS-CODIF_HS_H1_CS")
            theta = &var;
    REQUIRE(theta != nullptr);
    REQUIRE(theta->is_nrv);
    CHECK(theta->records == 1);
    const auto& floats = std::get<cpp_utils::containers::no_init_vector<float>>(theta->values);
    REQUIRE(floats.size() == 8);
    CHECK(floats.front() == -78.75f);
    CHECK(floats.back() == 78.75f);
}

TEST_CASE("values: NRV count mismatch raises", "[values]")
{
    CHECK_THROWS_AS(parse(one_variable(value_type::FLOAT, "  SIZES = 8\n", "1, 2, 3, 4, 5, 6, 7")),
        parse_error);
}

TEST_CASE("values: NRV data continuation", "[values]")
{
    const auto result = parse(one_variable(value_type::FLOAT, "  SIZES = 3\n", "1, 2, \\\n  3"));
    const auto& floats
        = std::get<cpp_utils::containers::no_init_vector<float>>(result.file.variables[0].values);
    REQUIRE(floats.size() == 3);
    CHECK(floats[2] == 3.0f);
}

TEST_CASE("values: NRV scalar keeps its single element", "[values]")
{
    const auto result = parse(one_variable(value_type::INT, "  SIZES = 1\n", "7"));
    CHECK(result.file.variables[0].sizes == std::vector<std::size_t> { 1 });
    CHECK(std::get<cpp_utils::containers::no_init_vector<std::int32_t>>(
              result.file.variables[0].values)
              .size()
        == 1);
}

namespace
{
// fast_float is the reference: the integer shortcut must accept the same text and produce the
// same number, or fall through.
template <typename T>
void require_same_parse(std::string_view text)
{
    T shortcut = 0;
    T reference = 0;
    const auto result = fast_float::from_chars(text.data(), text.data() + text.size(), reference);
    const bool reference_ok = result.ec == std::errc { } && result.ptr == text.data() + text.size();
    INFO(text);
    REQUIRE(detail::parse_float(text, shortcut) == reference_ok);
    // Bit patterns, not values: nan never equals itself.
    if (reference_ok)
        CHECK(std::memcmp(&shortcut, &reference, sizeof(T)) == 0);
}
}

TEST_CASE("number parsing matches fast_float", "[values]")
{
    for (const auto* text : { "", "0", "00", "007", "40", "1", "9", "1234567", "12345678",
             "123456789", "1234567890123456789", "-0", "-1", "+1", "1.", ".5", "1e3", "1E3",
             "1e+3", "1e-3", " 1", "1 ", "0x10", "inf", "nan", "1,5", "05", "99999999",
             "100000000", "-1e+31", "15462.2", "0.00239", "5e-324", "1.7976931348623157e308" })
    {
        require_same_parse<float>(text);
        require_same_parse<double>(text);
    }
    for (std::uint32_t value = 0; value < 2000; ++value)
    {
        const std::string text = std::to_string(value);
        require_same_parse<float>(text);
        require_same_parse<double>(text);
    }
}
