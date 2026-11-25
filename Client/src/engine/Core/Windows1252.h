#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Client::Core
{
// Decodifica un byte Windows-1252 según la conversión observada en XP (12-codificacion.md).
[[nodiscard]] char32_t Windows1252CodePoint(std::uint8_t byte);
// Decodifica una secuencia de bytes CP1252 a UTF-8 sin normalizar.
[[nodiscard]] std::string DecodeWindows1252(std::string_view bytes);
} // namespace Client::Core
