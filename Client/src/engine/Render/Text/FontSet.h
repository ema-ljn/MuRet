#pragma once

#include "EngineApi.h"

#include <cstdint>

// WinMain's four fonts (02 § Resolución, fuentes y texto): four CreateFontA without antialiasing
// (NONANTIALIASED_QUALITY) at 0x4ba737, 0x4ba777, 0x4ba7b9 and 0x4ba813, with a height that grows one point with the
// screen width. MuRet takes the heights and the weight from here; the face comes from the config ([Video] FontFile and
// BoldFontFile), because the original asks the system for it by name.
namespace Client::Render::Text
{
enum class FontKind : std::uint8_t
{
    // DAT_05763554, weight 400 and height n − 1, with the face of Text.bmd text 0: the login's.
    Normal,
    // DAT_05763558, weight 700 and height n − 1: the notices' (0x554810).
    Bold,
    // DAT_0576355c, weight 700 and height 2 (n − 1).
    BigBold,
    // DAT_05763560, weight 400 and fixed height 12, with another face (Text.bmd text 18). MuRet uses [Video]
    // FontFile's, like the others.
    OtherFace12,
    // +0x60c of the credits (0x63f31e): weight 700, with text 0's face, and the height by the exact screen width: 14 at
    // 800, 18 at 1024, 24 at 1280 and 10 at any other (640 too).
    Credits,
};

// n, the letter step (DAT_07758568): WinMain writes it at 0x4ba6af..0x4ba6d3, 12 for a width of 640, 13 for 800, 14 for
// 1024 and 15 for 1280. For any other width it is not written and, being in .bss, stays 0: at 1600 the letters ask for
// heights -1 and -2 (Q-C59). MuRet gives the step of the largest of those four widths not above `screenWidth`, and 12
// below 800: at 1600 or a wider monitor mode, 15.
[[nodiscard]] ENGINE_API int FontStepFor(int screenWidth);

// The cell height CreateFontA asks for (positive: the cell, not the letter): n − 1 for normal and bold, 2 (n − 1) for
// big bold, 12 for the other face, and the credits' by width.
[[nodiscard]] ENGINE_API int FontHeightFor(FontKind font, int screenWidth);

// Whether CreateFontA asks for weight 700 (FW_BOLD) rather than 400 (FW_NORMAL).
[[nodiscard]] ENGINE_API bool IsBold(FontKind font);
} // namespace Client::Render::Text
