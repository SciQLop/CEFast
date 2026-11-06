#include <catch2/catch_test_macros.hpp>

#include <cefast/errors.hpp>
#include <cefast/header/lexer.hpp>

using namespace cefast;

TEST_CASE("lexer: key and quoted value", "[lexer]")
{
    const auto header
        = lex_header("FILE_FORMAT_VERSION = \"CEF-2.0\"\nDATA_UNTIL = EOF\n", "file.cef");
    REQUIRE(header.lines.size() == 2);
    CHECK(header.lines[0].key == "FILE_FORMAT_VERSION");
    REQUIRE(header.lines[0].values.size() == 1);
    CHECK(header.lines[0].values[0].text == "CEF-2.0");
    CHECK(header.lines[0].values[0].quoted);
    CHECK(header.lines[0].line == 1);
    REQUIRE(header.lines[0].source);
    CHECK(*header.lines[0].source == "file.cef");
}

TEST_CASE("lexer: unquoted marker", "[lexer]")
{
    const auto header = lex_header("END_OF_RECORD_MARKER = $\nDATA_UNTIL = EOF\n", "file.cef");
    REQUIRE(header.lines[0].values.size() == 1);
    CHECK(header.lines[0].values[0].text == "$");
    CHECK_FALSE(header.lines[0].values[0].quoted);
}

TEST_CASE("lexer: comment after value", "[lexer]")
{
    const auto header
        = lex_header("FILE_NAME = x.cef ! trailing comment\nDATA_UNTIL = EOF\n", "file.cef");
    REQUIRE(header.lines[0].values.size() == 1);
    CHECK(header.lines[0].values[0].text == "x.cef");
}

TEST_CASE("lexer: bang and comma inside quotes", "[lexer]")
{
    const auto header = lex_header("ENTRY = \"a, b ! c\"\nDATA_UNTIL = EOF\n", "file.cef");
    REQUIRE(header.lines[0].values.size() == 1);
    CHECK(header.lines[0].values[0].text == "a, b ! c");
    CHECK(header.lines[0].values[0].quoted);
}

TEST_CASE("lexer: continuation", "[lexer]")
{
    const auto header = lex_header("DATA = 1, 2, \\\n   3\nDATA_UNTIL = EOF\n", "file.cef");
    REQUIRE(header.lines.size() == 2);
    REQUIRE(header.lines[0].values.size() == 3);
    CHECK(header.lines[0].values[0].text == "1");
    CHECK(header.lines[0].values[1].text == "2");
    CHECK(header.lines[0].values[2].text == "3");
    CHECK(header.lines[0].line == 1);
    CHECK(header.lines[1].line == 3);
}

TEST_CASE("lexer: commented attribute skipped", "[lexer]")
{
    const auto header = lex_header("! VALIDMIN = -90.0\nDATA_UNTIL = EOF\n", "file.cef");
    REQUIRE(header.lines.size() == 1);
    CHECK(header.lines[0].key == "DATA_UNTIL");
    CHECK(header.lines[0].line == 2);
}

TEST_CASE("lexer: crlf", "[lexer]")
{
    const auto header
        = lex_header("FILE_FORMAT_VERSION = \"CEF-2.0\"\r\nDATA_UNTIL = EOF\r\n", "file.cef");
    REQUIRE(header.lines.size() == 2);
    CHECK(header.lines[0].key == "FILE_FORMAT_VERSION");
    REQUIRE(header.lines[0].values.size() == 1);
    CHECK(header.lines[0].values[0].text == "CEF-2.0");
    CHECK(header.lines[1].key == "DATA_UNTIL");
    REQUIRE(header.lines[1].values.size() == 1);
    CHECK(header.lines[1].values[0].text == "EOF");
}

TEST_CASE("lexer: unterminated quote raises", "[lexer]")
{
    CHECK_THROWS_AS(lex_header("ENTRY = \"abc\n", "file.cef"), parse_error);
    try
    {
        lex_header("ENTRY = \"abc\n", "file.cef");
    }
    catch (const parse_error& error)
    {
        CHECK(std::string(error.what()).find(":1:") != std::string::npos);
    }
}

TEST_CASE("lexer: data offset after DATA_UNTIL", "[lexer]")
{
    const std::string text
        = "FILE_NAME = x.cef\nDATA_UNTIL = END_DATA\n"
          "2002-02-02T00:00:06.267Z, 8.031,\n";
    const auto header = lex_header(text, "file.cef");
    REQUIRE(header.lines.size() == 2);
    CHECK(text.compare(header.data_offset, 4, "2002") == 0);
    CHECK(header.data_line == 3);
}

TEST_CASE("lexer: line without equals raises", "[lexer]")
{
    CHECK_THROWS_AS(lex_header("NOEQUALS\nDATA_UNTIL = EOF\n", "file.cef"), parse_error);
}

TEST_CASE("lexer: DATA_UNTIL missing raises", "[lexer]")
{
    CHECK_THROWS_AS(lex_header("FILE_NAME = x.cef\n", "file.cef"), parse_error);
    const std::string text = "FILE_NAME = x.cef\n";
    const auto included = lex_header(text, "inc.ceh", false);
    REQUIRE(included.lines.size() == 1);
    CHECK(included.data_offset == text.size());
}

TEST_CASE("lexer: SIZES quoted with spaces", "[lexer]")
{
    const auto header = lex_header("SIZES = \"64, 6, 32\"\nDATA_UNTIL = EOF\n", "file.cef");
    REQUIRE(header.lines[0].values.size() == 1);
    CHECK(header.lines[0].values[0].text == "64, 6, 32");
    CHECK(header.lines[0].values[0].quoted);
}

TEST_CASE("lexer: quotes inside an unquoted value", "[lexer]")
{
    // The quotes toggle the comma rule even though the value is not quoted as a whole.
    const auto header = lex_header("FILE_NAME = a\"b,c\"d\nDATA_UNTIL = EOF\n", "file.cef");
    REQUIRE(header.lines[0].values.size() == 1);
    CHECK(header.lines[0].values[0].text == "a\"b,c\"d");
    CHECK_FALSE(header.lines[0].values[0].quoted);
}

TEST_CASE("lexer: equals inside a quoted value", "[lexer]")
{
    const auto header = lex_header("ENTRY = \"a=b\"\nDATA_UNTIL = EOF\n", "file.cef");
    REQUIRE(header.lines[0].values.size() == 1);
    CHECK(header.lines[0].values[0].text == "a=b");
    CHECK(header.lines[0].values[0].quoted);
}

TEST_CASE("lexer: equals inside a quoted key", "[lexer]")
{
    const auto header = lex_header("\"K=E\" = v\nDATA_UNTIL = EOF\n", "file.cef");
    // The quoted key keeps its quotes: only the '=' outside quotes separates it from the values.
    CHECK(header.lines[0].key == "\"K=E\"");
    REQUIRE(header.lines[0].values.size() == 1);
    CHECK(header.lines[0].values[0].text == "v");
}

TEST_CASE("lexer: continuation joins quoted values", "[lexer]")
{
    const auto header = lex_header("DATA = \"a\", \\\n   \"b\"\nDATA_UNTIL = EOF\n", "file.cef");
    REQUIRE(header.lines.size() == 2);
    REQUIRE(header.lines[0].values.size() == 2);
    CHECK(header.lines[0].values[0].text == "a");
    CHECK(header.lines[0].values[0].quoted);
    CHECK(header.lines[0].values[1].text == "b");
    CHECK(header.lines[0].values[1].quoted);
    CHECK(header.lines[0].line == 1);
    CHECK(header.lines[1].line == 3);
}

TEST_CASE("lexer: quote open on a continued line raises", "[lexer]")
{
    // A quoted value cannot span the join: the quote has to close on its own physical line.
    CHECK_THROWS_AS(lex_header("ENTRY = \"abc\\\ndef\"\nDATA_UNTIL = EOF\n", "file.cef"),
        parse_error);
    try
    {
        lex_header("ENTRY = \"abc\\\ndef\"\nDATA_UNTIL = EOF\n", "file.cef");
    }
    catch (const parse_error& error)
    {
        CHECK(std::string(error.what()).find(":1:") != std::string::npos);
    }
}

TEST_CASE("lexer: spaces around the continuation marker", "[lexer]")
{
    // Spaces after the '\' are trailing whitespace; spaces before it are trimmed from the values.
    const auto header = lex_header("DATA = 1, \\   \n  2\nDATA_UNTIL = EOF\n", "file.cef");
    REQUIRE(header.lines.size() == 2);
    REQUIRE(header.lines[0].values.size() == 2);
    CHECK(header.lines[0].values[0].text == "1");
    CHECK(header.lines[0].values[1].text == "2");
    CHECK(header.lines[0].line == 1);

    const auto padded = lex_header("DATA = 1,   \\\n  2\nDATA_UNTIL = EOF\n", "file.cef");
    REQUIRE(padded.lines[0].values.size() == 2);
    CHECK(padded.lines[0].values[1].text == "2");
}

TEST_CASE("lexer: crlf continuation", "[lexer]")
{
    const auto header = lex_header("DATA = 1, \\\r\n  2\r\nDATA_UNTIL = EOF\r\n", "file.cef");
    REQUIRE(header.lines.size() == 2);
    REQUIRE(header.lines[0].values.size() == 2);
    CHECK(header.lines[0].values[1].text == "2");
    CHECK(header.lines[1].key == "DATA_UNTIL");
}

TEST_CASE("lexer: empty values between commas", "[lexer]")
{
    const auto header = lex_header("DATA = 1,,2,\nDATA_UNTIL = EOF\n", "file.cef");
    REQUIRE(header.lines[0].values.size() == 2);
    CHECK(header.lines[0].values[0].text == "1");
    CHECK(header.lines[0].values[1].text == "2");
}
