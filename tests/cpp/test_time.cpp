#include <catch2/catch_test_macros.hpp>

#include <cefast/time.hpp>

#include <cstdint>
#include <limits>

using namespace cefast;

TEST_CASE("ISO 8601 time parsing", "[time]")
{
    CHECK(parse_iso_time("1970-01-01T00:00:00Z") == 0);
    CHECK(parse_iso_time("2002-02-02T11:57:09.292Z") == 1012651029292000000);
    CHECK(parse_iso_time("2002-02-02t11:57:09.292z") == 1012651029292000000);
    CHECK(parse_iso_time("1958-01-01T00:00:00.000000001Z") == -378691200000000000 + 1);
    CHECK(parse_iso_time("2000-01-01T00:00:00.1234567891234Z") == 946684800123456789);
    CHECK(parse_iso_time("2016-12-31T23:59:60Z") == parse_iso_time("2017-01-01T00:00:00Z"));
    CHECK(parse_iso_time("2000-02-29T00:00:00Z").has_value());
    CHECK_FALSE(parse_iso_time("2001-02-29T00:00:00Z"));
    CHECK_FALSE(parse_iso_time("9999-99-99T99:99:99Z"));
    CHECK_FALSE(parse_iso_time("9999-12-31T23:59:59Z")); // valid date, outside ns range
    CHECK_FALSE(parse_iso_time("2000-08-09T11:13Z")); // seconds mandatory
    CHECK_FALSE(parse_iso_time("2000-08-09T11:13:00+02:00"));
    CHECK_FALSE(parse_iso_time("2000-08-09T11:13:00"));

    auto range = parse_iso_time_range("2000-07-16T00:00:00Z/2026-12-31T23:59:59Z");
    REQUIRE(range);
    CHECK((*range)[0] == 963705600000000000);
    CHECK_FALSE(parse_iso_time_range("2000-07-16T00:00:00Z"));

    // The last second of the int64 nanosecond range only takes part of its fraction.
    CHECK(parse_iso_time("2262-04-11T23:47:16.854775807Z")
        == std::numeric_limits<std::int64_t>::max());
    CHECK_FALSE(parse_iso_time("2262-04-11T23:47:16.854775808Z"));
    CHECK_FALSE(parse_iso_time("2262-04-11T23:47:16.9Z"));
}

namespace
{
// Puts the general ISO 8601 path back for the lifetime of the guard.
struct general_time_path
{
    general_time_path() { detail::fixed_time_path() = false; }
    ~general_time_path() { detail::fixed_time_path() = true; }
    general_time_path(const general_time_path&) = delete;
    general_time_path& operator=(const general_time_path&) = delete;
};

[[nodiscard]] std::optional<std::int64_t> via_general_path(std::string_view text)
{
    const general_time_path guard;
    return parse_iso_time(text);
}
}

TEST_CASE("ISO 8601 fixed-width fast path matches the general path", "[time]")
{
    const std::string base = "2002-02-02T00:00:06.267Z";
    for (std::size_t at = 0; at < base.size(); ++at)
        for (char value = '!'; value < '~'; ++value)
        {
            std::string text = base;
            text[at] = value;
            INFO(text);
            CHECK(parse_iso_time(text) == via_general_path(text));
        }
    for (std::size_t length = 0; length <= base.size() + 2; ++length)
    {
        const std::string text = base.substr(0, std::min(length, base.size()));
        INFO(text);
        CHECK(parse_iso_time(text) == via_general_path(text));
    }
}

TEST_CASE("ISO 8601 fixed-width fast path keeps the calendar rules", "[time]")
{
    const auto same = [](std::string_view text) {
        INFO(text);
        CHECK(parse_iso_time(text) == via_general_path(text));
    };
    for (const auto* text : { "2000-02-29T23:59:59.999Z", "1900-02-29T00:00:00.000Z",
             "2001-02-29T00:00:00.000Z", "2002-04-30T00:00:00.000Z",
             "2002-04-31T00:00:00.000Z", "2002-00-10T00:00:00.000Z",
             "2002-13-01T00:00:00.000Z", "2002-01-00T00:00:00.000Z",
             "2002-01-01T24:00:00.000Z", "2002-01-01T00:60:00.000Z",
             "2002-01-01T00:00:61.000Z", "2016-12-31T23:59:60.500Z",
             "0000-01-01T00:00:00.000Z", "9999-12-31T23:59:59.999Z",
             "2262-04-11T23:47:16.854Z", "2262-04-11T23:47:16.855Z",
             "1677-09-21T00:12:43.145Z", "1677-09-21T00:12:43.144Z",
             "2002-02-02t00:00:06.267z", "2002-02-02T00:00:06.267+",
             "2002-02-02 00:00:06.267Z", " 002-02-02T00:00:06.267Z" })
        same(text);
}
