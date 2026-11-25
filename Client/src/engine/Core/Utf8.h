#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace Client::Core
{
// Lee un escalar UTF-8 desde `offset` y avanza si está bien formado; si no, deja el cursor intacto.
[[nodiscard]] std::optional<char32_t> ReadUtf8CodePoint(std::string_view text, std::size_t& offset);
} // namespace Client::Core
