#pragma once
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cefast
{
namespace detail
{
    inline std::string error_message(std::string_view source, std::size_t line,
        std::string_view what, std::optional<std::size_t> record)
    {
        std::string message { source };
        message += ':';
        message += std::to_string(line);
        message += ": ";
        message += what;
        if (record)
        {
            message += " (record ";
            message += std::to_string(*record);
            message += ')';
        }
        return message;
    }
}

// Every malformed input names the file and line it came from, and the record when it is in the
// data section: a reader that cannot say where it failed is a reader that silently shifts columns.
struct parse_error : std::runtime_error
{
    parse_error(std::string_view source, std::size_t line, std::string_view what,
        std::optional<std::size_t> record = { })
            : std::runtime_error(detail::error_message(source, line, what, record))
    {
    }
};
}
