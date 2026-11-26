#pragma once

#include "EngineApi.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// On Windows path(std::string) and path::string() use the system code page (ANSI), not UTF-8: a letter missing there
// ("ñ" in another page, a Japanese name) becomes "?" or throws. These go through char8_t, which path reads and writes
// as UTF-8 everywhere.
namespace Client::Core
{
[[nodiscard]] ENGINE_API std::string PathText(const std::filesystem::path& path);

// Empty if the text is not well-formed UTF-8: no stray bytes, no overlong forms, no UTF-16 halves (U+D800 to U+DFFF),
// nothing past U+10FFFF. That is what MultiByteToWideChar rejects with MB_ERR_INVALID_CHARS, which path uses on
// Windows: a bad byte would throw. An embedded NUL is UTF-8 and passes; the caller rejects it if it matters.
[[nodiscard]] ENGINE_API std::optional<std::filesystem::path> PathFromUtf8(std::string_view text);
} // namespace Client::Core
