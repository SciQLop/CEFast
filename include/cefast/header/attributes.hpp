#pragma once
#include <string_view>

#include <cefast/model.hpp>

namespace cefast
{
// First attribute with that name; CEF blocks rarely repeat a keyword, and when they do the first
// one is the one readers use (SIZES, FILLVAL, ...).
[[nodiscard]] inline const attribute* find(const attributes& attrs, std::string_view name) noexcept
{
    for (const auto& attr : attrs)
        if (attr.name == name)
            return &attr;
    return nullptr;
}
}
