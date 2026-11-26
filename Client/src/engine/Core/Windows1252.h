#pragma once

#include "EngineApi.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Client::Core
{
// The conversion observed on XP (12-codificacion.md).
[[nodiscard]] ENGINE_API char32_t Windows1252CodePoint(std::uint8_t byte);
// To UTF-8, without normalizing.
[[nodiscard]] ENGINE_API std::string DecodeWindows1252(std::string_view bytes);
} // namespace Client::Core
