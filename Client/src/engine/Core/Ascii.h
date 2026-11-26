#pragma once

#include "EngineApi.h"

#include <string_view>

// Only A to Z, as Windows compares an ASCII file name (outside ASCII Windows folds more letters; this does not).
// std::tolower does not fit: it depends on the C locale and is undefined behavior with a negative char, as any byte
// >= 0x80 of UTF-8 or Windows code page text is. Those bytes compare as they are.
namespace Client::Core
{
[[nodiscard]] ENGINE_API char ToLowerAscii(char c);

// An embedded '\0' is one more byte.
[[nodiscard]] ENGINE_API bool EqualsIgnoringAsciiCase(std::string_view a, std::string_view b);
} // namespace Client::Core
