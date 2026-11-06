#include <cefast/cefast.hpp>

#include "compare.hpp"
#include "reference_records.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

// The record fast path must be indistinguishable from the frozen scanner (reference_records.hpp):
// same values, same record counts, or the same parse_error message. The input is split into a tiny
// schema description (slots, types, marker, whether FILLVAL is set) and a data body; anything the
// description does not consume is handed to both scanners verbatim.
namespace
{
constexpr std::array<cefast::value_type, 6> slot_types { cefast::value_type::FLOAT,
    cefast::value_type::DOUBLE, cefast::value_type::INT, cefast::value_type::BYTE,
    cefast::value_type::ISO_TIME, cefast::value_type::CHAR };

constexpr std::array<const char*, 6> type_names { "FLOAT", "DOUBLE", "INT", "BYTE", "ISO_TIME",
    "CHAR" };

constexpr std::array<const char*, 3> markers { "$", "$$", "" };

// A FILLVAL the type can hold, so the fill branch is reachable: a float fill on a float column,
// 255 on an integer column, the usual CSA time marker on a time column.
[[nodiscard]] std::string fill_token(cefast::value_type type)
{
    switch (type)
    {
        case cefast::value_type::FLOAT:
        case cefast::value_type::DOUBLE:
            return "-1.0e+31";
        case cefast::value_type::INT:
        case cefast::value_type::BYTE:
            return "255";
        case cefast::value_type::ISO_TIME:
        case cefast::value_type::ISO_TIME_RANGE:
            return "9999-99-99T99:99:99Z";
        case cefast::value_type::CHAR:
            return "fill";
    }
    return "0";
}

struct input_description
{
    std::string header;
    std::string body;
};

[[nodiscard]] std::uint8_t byte_at(const std::uint8_t* data, std::size_t size, std::size_t index)
{
    return index < size ? data[index] : static_cast<std::uint8_t>(0);
}

// Prologue: [variable count][marker][fill?][type + elements]... The body starts right after the type bytes.
[[nodiscard]] input_description describe(const std::uint8_t* data, std::size_t size)
{
    const std::size_t slots = 1 + (byte_at(data, size, 0) % 6);
    const auto marker = markers[byte_at(data, size, 1) % markers.size()];
    const bool with_fill = byte_at(data, size, 2) % 2 == 1;

    std::string header { "FILE_FORMAT_VERSION = \"CEF-2.0\"\n" };
    if (marker[0] != '\0')
    {
        header += "END_OF_RECORD_MARKER = ";
        header += marker;
        header += '\n';
    }
    for (std::size_t slot = 0; slot < slots; ++slot)
    {
        const auto type_byte = byte_at(data, size, 3 + slot);
        const auto type_index = type_byte % slot_types.size();
        // The high bits give the variable 1-4 elements, so a record has columns of several values.
        const auto elements = 1 + (type_byte / 16) % 4;
        const auto type = slot_types[type_index];
        const auto name = "v" + std::to_string(slot);
        header += "START_VARIABLE = " + name + "\n";
        header += "  VALUE_TYPE = ";
        header += type_names[type_index];
        header += "\n  SIZES = " + std::to_string(elements) + "\n";
        if (with_fill)
            header += "  FILLVAL = " + fill_token(type) + "\n";
        header += "END_VARIABLE = " + name + "\n";
    }
    header += "DATA_UNTIL = END_DATA\n";

    const auto body_start = std::min(size, 3 + slots);
    return { std::move(header),
        std::string { reinterpret_cast<const char*>(data + body_start), size - body_start } };
}

struct outcome
{
    bool ok = false;
    std::string error;
    cefast::cef_file file;
};

// The candidate: the full public path, serial so the thread count cannot affect the comparison.
[[nodiscard]] outcome attempt_new(const std::string& text)
{
    cefast::load_options options;
    options.threads = 1;
    try
    {
        return { true, { },
            cefast::load(std::span<const char> { text.data(), text.size() }, options) };
    }
    catch (const cefast::parse_error& error)
    {
        return { false, error.what(), { } };
    }
}

// The oracle: today's header parse (unchanged by this round) feeding the frozen record scanner.
[[nodiscard]] outcome attempt_reference(const std::string& text)
{
    cefast::input_buffer input { std::string { text }, "<memory>", std::nullopt };
    try
    {
        const auto lexed = cefast::lex_header(input.bytes, input.source);
        auto schema = cefast::parse_header(lexed, input, { });
        cefast::reference::parse_records(schema,
            std::string_view { input.bytes }.substr(lexed.data_offset), lexed.data_line,
            input.source);
        return { true, { }, std::move(schema.file) };
    }
    catch (const cefast::parse_error& error)
    {
        return { false, error.what(), { } };
    }
}

void compare(const std::string& text)
{
    const auto ours = attempt_new(text);
    const auto reference = attempt_reference(text);
    if (ours.ok == reference.ok && ours.error == reference.error
        && (!ours.ok || fuzz::same_file(ours.file, reference.file)))
        return;

    std::fprintf(stderr, "fuzz_records_diff: mismatch\n");
    if (ours.ok != reference.ok)
        std::fprintf(stderr, "accept/reject mismatch\n");
    else if (!ours.ok)
        std::fprintf(stderr, "error mismatch:\n--- cefast ---\n%s\n--- reference ---\n%s\n",
            ours.error.c_str(), reference.error.c_str());
    else
        std::fprintf(stderr, "value mismatch: %s\n",
            fuzz::describe_difference(ours.file, reference.file).c_str());
    std::fprintf(stderr, "--- input ---\n%.*s\n", static_cast<int>(text.size()), text.data());
    __builtin_trap();
}
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    auto description = describe(data, size);
    std::string text = std::move(description.header);
    text += description.body;
    compare(text);
    return 0;
}
