#pragma once

#include "Input/Key.h"

#include <cstdint>
#include <string>

namespace Client::Input
{
struct TextInputEvent
{
    enum class Kind : std::uint8_t
    {
        Text,
        Key,
        Composition,
    };

    Kind kind = Kind::Text;
    Key key = Key::Count;
    // Texto de SDL copiado en UTF-8, sin conversión ANSI.
    std::string text;
    bool shift = false;
    bool ctrl = false;
    // Rango IME en codepoints UTF-8, no bytes; -1 significa que SDL no lo indicó.
    std::int32_t compositionStart = 0;
    std::int32_t compositionLength = 0;
};
} // namespace Client::Input
