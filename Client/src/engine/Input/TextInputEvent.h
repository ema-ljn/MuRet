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
    // Copied in UTF-8, with no ANSI conversion.
    std::string text;
    bool shift = false;
    bool ctrl = false;
    // In UTF-8 code points, not bytes; -1 means SDL did not give it.
    std::int32_t compositionStart = 0;
    std::int32_t compositionLength = 0;
};
} // namespace Client::Input
