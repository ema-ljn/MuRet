#pragma once

#include "EngineApi.h"

#include <cstddef>
#include <optional>
#include <string_view>

namespace Client::Core
{
// Advances `offset` only if the scalar is well-formed.
[[nodiscard]] ENGINE_API std::optional<char32_t> ReadUtf8CodePoint(std::string_view text, std::size_t& offset);
} // namespace Client::Core
