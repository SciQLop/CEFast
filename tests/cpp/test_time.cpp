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
