#pragma once

#include "EngineApi.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Core
{
// Un codeunit BMP pasado a WideCharToMultiByte(CP1252, 0) en Windows XP; el fallback es '?'.
[[nodiscard]] ENGINE_API std::uint8_t Windows1252XpCodeUnit(char16_t codeUnit);
// Convierte UTF-8 con ese mapeo por codeunit; falla sin devolver prefijo si el UTF-8 es inválido.
[[nodiscard]] ENGINE_API std::optional<std::string> EncodeWindows1252Xp(std::string_view utf8);
} // namespace Client::Core
