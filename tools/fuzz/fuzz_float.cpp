#include <cefast/values.hpp>

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

// parse_float must accept exactly the C++ floating grammar (plus a leading '+', which CEF writes)
// and produce the correctly rounded value. Two independent references: std::from_chars for the
// grammar and the digits, strtod/strtof for the rounding.
namespace
{
template <typename T>
bool same_value(T left, T right)
{
    if (std::isnan(left) || std::isnan(right))
        return std::isnan(left) && std::isnan(right);
    return std::memcmp(&left, &right, sizeof(T)) == 0;
}

// strtod and strtof report ERANGE both for overflow and for underflow, while the C++ grammar only
// rejects a value that cannot be represented at all (a denormal is representable).
template <typename T>
bool strtod_accepts(std::string_view text, T& value)
{
    std::string buffer { text };
    errno = 0;
    char* end = nullptr;
    if constexpr (std::is_same_v<T, float>)
        value = std::strtof(buffer.c_str(), &end);
    else
        value = std::strtod(buffer.c_str(), &end);
    if (end != buffer.data() + buffer.size() || buffer.empty())
        return false;
    return !(errno == ERANGE && (value == 0 || std::isinf(value)));
}

template <typename T>
void check(std::string_view text)
{
    // The one spelling of the accepted grammar that std::from_chars lacks is the leading '+',
    // and only one sign is a sign: "+-1" is not a number.
    const bool double_sign = text.size() > 1 && (text.front() == '+' || text.front() == '-')
        && (text[1] == '+' || text[1] == '-');
    const std::string_view stripped
        = !text.empty() && text.front() == '+' ? text.substr(1) : text;
    T reference = 0;
    const auto result
        = std::from_chars(stripped.data(), stripped.data() + stripped.size(), reference);
    const bool reference_ok = !double_sign && !stripped.empty() && result.ec == std::errc { }
        && result.ptr == stripped.data() + stripped.size();

    T parsed = 0;
    const bool parsed_ok = cefast::detail::parse_float(text, parsed);
    if (parsed_ok != reference_ok)
    {
        std::fprintf(stderr, "fuzz_float<%s>: grammar differs from std::from_chars on \"%.*s\"\n",
            std::is_same_v<T, float> ? "float" : "double", static_cast<int>(text.size()),
            text.data());
        __builtin_trap();
    }
    if (!parsed_ok)
        return;
    if (!same_value(parsed, reference))
    {
        std::fprintf(stderr, "fuzz_float<%s>: value differs from std::from_chars on \"%.*s\"\n",
            std::is_same_v<T, float> ? "float" : "double", static_cast<int>(text.size()),
            text.data());
        __builtin_trap();
    }
    // A nan payload spelling ("nan(ind)") is read whole by std::from_chars but not by strtod.
    T rounded = 0;
    if (text.find('(') == std::string_view::npos && strtod_accepts(text, rounded)
        && !same_value(parsed, rounded))
    {
        std::fprintf(stderr, "fuzz_float<%s>: value differs from strtod on \"%.*s\"\n",
            std::is_same_v<T, float> ? "float" : "double", static_cast<int>(text.size()),
            text.data());
        __builtin_trap();
    }
}

// The record fast path's one-loop parsers must agree with the general ones whenever they accept:
// same end byte and the same bits. Declining (null) is always allowed.
template <typename T>
void check_plain_decimal(std::string_view text)
{
    const char* first = text.data();
    const char* last = text.data() + text.size();
    T plain = 0;
    const char* end = cefast::detail::parse_plain_decimal(first, last, plain);
    if (end == nullptr)
        return;
    T reference = 0;
    const auto result = fast_float::from_chars(first, last, reference, cefast::detail::float_format);
    if (result.ec != std::errc { } || result.ptr != end || !same_value(plain, reference))
    {
        std::fprintf(stderr, "fuzz_float<%s>: parse_plain_decimal differs from fast_float on \"%.*s\"\n",
            std::is_same_v<T, float> ? "float" : "double", static_cast<int>(text.size()),
            text.data());
        __builtin_trap();
    }
}

void check_plain_int(std::string_view text)
{
    std::int64_t plain = 0;
    const char* end = cefast::detail::parse_plain_int(text.data(), text.data() + text.size(), plain);
    if (end == nullptr)
        return;
    std::int64_t reference = 0;
    const char* reference_end = cefast::detail::parse_int_prefix(text, reference);
    if (reference_end != end || reference != plain)
    {
        std::fprintf(stderr, "fuzz_float: parse_plain_int differs from std::from_chars on \"%.*s\"\n",
            static_cast<int>(text.size()), text.data());
        __builtin_trap();
    }
}
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::string_view text { reinterpret_cast<const char*>(data), size };
    check<float>(text);
    check<double>(text);
    check_plain_decimal<float>(text);
    check_plain_decimal<double>(text);
    check_plain_int(text);
    return 0;
}
