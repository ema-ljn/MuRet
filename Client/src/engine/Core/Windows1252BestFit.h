#pragma once

#include "EngineApi.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Core
{
// A BMP code unit passed to WideCharToMultiByte(CP1252, 0) on Windows XP; the fallback is '?'.
[[nodiscard]] ENGINE_API std::uint8_t Windows1252XpCodeUnit(char16_t codeUnit);
// Invalid UTF-8 fails without returning a prefix.
[[nodiscard]] ENGINE_API std::optional<std::string> EncodeWindows1252Xp(std::string_view utf8);
} // namespace Client::Core
