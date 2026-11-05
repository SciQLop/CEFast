#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <cefast/data/records.hpp>
#include <cefast/header/parser.hpp>
#include <cefast/input.hpp>

using namespace cefast;

namespace
{
schema load(std::string_view text, std::string source = "test.cef")
{
    auto input = input_buffer { std::string { text }, std::move(source), std::nullopt };
    const auto lexed = lex_header(input.bytes, input.source);
    auto result = parse_header(lexed, input, { });
    parse_records(result, std::string_view { input.bytes }.substr(lexed.data_offset),
        lexed.data_line, input.source);
    return result;
}

std::string header(
    std::string_view marker, std::string_view variables, std::string_view data_until = "END_DATA")
{
    std::string text { "FILE_FORMAT_VERSION = \"CEF-2.0\"\n" };
    if (!marker.empty())
        text += "END_OF_RECORD_MARKER = " + std::string { marker } + "\n";
    text += variables;
    text += "\nDATA_UNTIL = ";
    text += data_until;
    text += "\n";
    return text;
}

constexpr std::string_view one_float = R"(START_VARIABLE = v
  VALUE_TYPE = FLOAT
END_VARIABLE = v)";

std::string time_and_float(std::string_view sizes = "3")
{
    std::string text { R"(START_VARIABLE = time_tags
  VALUE_TYPE = ISO_TIME
END_VARIABLE = time_tags
START_VARIABLE = v
  VALUE_TYPE = FLOAT
  SIZES = )" };
    text += sizes;
    text += "\nEND_VARIABLE = v\n";
    return text;
}
}

TEST_CASE("records: newline marker", "[records]")
{
    const auto result = load(header("", one_float) + "1.5\n2.5\n3.5\nEND_DATA\n");
    REQUIRE(result.file.variables.size() == 1);
    CHECK(result.file.variables[0].records == 3);
    const auto& values
        = std::get<cpp_utils::containers::no_init_vector<float>>(result.file.variables[0].values);
    REQUIRE(values.size() == 3);
    CHECK(values[2] == 3.5f);
}

TEST_CASE("records: dollar marker with trailing space", "[records]")
{
    const auto result = load(header("\"$\"", time_and_float("3"))
        + "2002-02-02T00:00:00.000Z, 1, 2, 3 $\n"
          "2002-02-02T00:00:01.000Z, 4, 5, 6 $\nEND_DATA\n");
    REQUIRE(result.file.variables.size() == 2);
    CHECK(result.file.variables[0].records == 2);
    const auto& times = std::get<cpp_utils::containers::no_init_vector<std::int64_t>>(
        result.file.variables[0].values);
    REQUIRE(times.size() == 2);
    CHECK(times[0] == 1012608000000000000);
    const auto& values
        = std::get<cpp_utils::containers::no_init_vector<float>>(result.file.variables[1].values);
    REQUIRE(values.size() == 6);
    CHECK(values[5] == 6.0f);
}

TEST_CASE("records: multi-line record with a comment", "[records]")
{
    const auto result = load(header(";", time_and_float("4"))
        + "2002-02-02T00:00:00.000Z, 1, 2,\n  3, 4 ;\n"
          "! a comment between records\n"
          "2002-02-02T00:00:01.000Z,\n  5, 6,\n  7, 8 ;\nEND_DATA\n");
    CHECK(result.file.variables[0].records == 2);
    const auto& values
        = std::get<cpp_utils::containers::no_init_vector<float>>(result.file.variables[1].values);
    REQUIRE(values.size() == 8);
    CHECK(values[0] == 1.0f);
    CHECK(values[7] == 8.0f);
}

TEST_CASE("records: quoted CHAR keeps separators", "[records]")
{
    const auto result = load(header("\"$\"", R"(START_VARIABLE = c
  VALUE_TYPE = CHAR
END_VARIABLE = c)")
        + "\"a,b!c\" $\nEND_DATA\n");
    const auto& values = std::get<std::vector<std::string>>(result.file.variables[0].values);
    REQUIRE(values.size() == 1);
    CHECK(values[0] == "a,b!c");
}

TEST_CASE("records: crlf line endings", "[records]")
{
    std::string text = header("", one_float);
    text += "1.5\r\n2.5\r\nEND_DATA\r\n";
    const auto result = load(text);
    CHECK(result.file.variables[0].records == 2);
}

TEST_CASE("records: trailing comma before the marker", "[records]")
{
    const auto result = load(header("\"$\"", one_float) + "1.5, $\nEND_DATA\n");
    CHECK(result.file.variables[0].records == 1);
}

TEST_CASE("records: empty field raises", "[records]")
{
    CHECK_THROWS_AS(
        load(header("", time_and_float("2")) + "2002-02-02T00:00:00.000Z, 1,, 2\nEND_DATA\n"),
        parse_error);
}

TEST_CASE("records: short record names record and line", "[records]")
{
    try
    {
        load(header("", one_float) + "1.5\n2.5, 3.5\nEND_DATA\n");
        FAIL("expected parse_error");
    }
    catch (const parse_error& error)
    {
        const std::string message { error.what() };
        CHECK(message.find("expected 1 values, got 2") != std::string::npos);
        CHECK(message.find("record 2") != std::string::npos);
    }
}

TEST_CASE("records: long record raises", "[records]")
{
    CHECK_THROWS_AS(load(header("", one_float) + "1.5, 2.5\nEND_DATA\n"), parse_error);
}

TEST_CASE("records: multi-dimensional C order", "[records]")
{
    const auto result = load(header("\"$\"", R"(START_VARIABLE = v
  VALUE_TYPE = FLOAT
  SIZES = 2, 3
END_VARIABLE = v)")
        + "0, 1, 2, 3, 4, 5 $\nEND_DATA\n");
    const auto& values
        = std::get<cpp_utils::containers::no_init_vector<float>>(result.file.variables[0].values);
    REQUIRE(values.size() == 6);
    for (std::size_t i = 0; i < values.size(); ++i)
        CHECK(values[i] == static_cast<float>(i));
    CHECK(result.file.variables[0].sizes == std::vector<std::size_t> { 2, 3 });
}

TEST_CASE("records: empty and single record", "[records]")
{
    const auto empty = load(header("", time_and_float("3")) + "!RECORDS= 0\nEND_DATA\n");
    CHECK(empty.file.variables[0].records == 0);
    const auto single
        = load(header("", time_and_float("3")) + "2002-02-02T00:00:00.000Z, 1, 2, 3\nEND_DATA\n");
    CHECK(single.file.variables[0].records == 1);
}

TEST_CASE("records: the records hint is only a hint", "[records]")
{
    const auto high = load(header("", one_float) + "!RECORDS= 10\n1.5\n2.5\n3.5\nEND_DATA\n");
    CHECK(high.file.variables[0].records == 3);
    const auto low = load(header("", one_float) + "!RECORDS= 1\n1.5\n2.5\n3.5\nEND_DATA\n");
    CHECK(low.file.variables[0].records == 3);
}

TEST_CASE("records: data stops at DATA_UNTIL", "[records]")
{
    const auto result = load(header("", one_float) + "1.5\n2.5\nEND_DATA  ! end\n9.5\n");
    CHECK(result.file.variables[0].records == 2);
}

TEST_CASE("records: consecutive quoted values", "[records]")
{
    const auto result = load(header("\"$\"", R"(START_VARIABLE = a
  VALUE_TYPE = CHAR
END_VARIABLE = a
START_VARIABLE = b
  VALUE_TYPE = CHAR
END_VARIABLE = b)")
        + "\"A\",\"B\" $\nEND_DATA\n");
    REQUIRE(result.file.variables.size() == 2);
    CHECK(std::get<std::vector<std::string>>(result.file.variables[0].values)[0] == "A");
    CHECK(std::get<std::vector<std::string>>(result.file.variables[1].values)[0] == "B");
    CHECK(result.file.variables[0].records == 1);
}
