#pragma once

#include "Render/Text/FontSet.h"

#include <cstdint>

// What the original leaves in globals before drawing a text with 0x554180: the font chosen with SelectObject, the text
// color (DAT_006c8e5c) and the background (DAT_006c8e64). MuRet passes them in each call (06 § Texto). The box width
// the login also writes (DAT_006c8e70 = 0x100) is left out: 0x467060, the login's text, does not read it; only
// 0x553ae0 does (at 0x553bef), which the login does not call.
namespace Client::Render::Text
{
// Colors in the original's 0xAABBGGRR format: the low byte is red, as 0x465a00 leaves it in 0x554000's RGBA buffer (one
// dword per pixel). 0x80000000 is black with alpha 0x80.
struct TextColors
{
    std::uint32_t text = 0xffffffff;
    std::uint32_t background = 0;

    friend bool operator==(const TextColors&, const TextColors&) = default;
};

struct TextStyle
{
    FontKind font = FontKind::Normal;
    TextColors colors;

    friend bool operator==(const TextStyle&, const TextStyle&) = default;
};
} // namespace Client::Render::Text
