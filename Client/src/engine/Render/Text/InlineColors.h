#pragma once

#include "EngineApi.h"
#include "Render/Text/TextStyle.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Color changes inside a text: 0x02 followed by a code. 0x4667a0 splits them, and only runs for cached text (0x466000,
// TextOut at 1); the login's 0x467060 draws them as letters (06 § Texto). The byte after 0x02 is the code even if it is
// another 0x02 or the final 0. Up to nine runs (0x4667e9: stops at 18 bytes of pairs), but every pair is removed, also
// past nine (0x46687b..0x466894), so the text from the tenth on keeps the ninth's color. *Static*.
namespace Client::Render::Text
{
inline constexpr char InlineColorMarker = '\x02';
inline constexpr std::size_t MaxInlineColors = 9;

// What 0x4665d0 reads from globals: the text colors (DAT_006c8e5c and DAT_006c8e64), which 0xf0 uses as is and 0xf1
// swaps, and DAT_006c7a88, which decides 0xf8's background. That flag is 1 in .data and UI windows write it (0x45b76e,
// 0x45d900, 0x45d922 and 0x45d93a); what it means was not read.
struct InlineColorContext
{
    TextColors scene;
    bool f8WithoutBackground = true;
};

struct ColorRun
{
    // In the clean text: the first byte in this color.
    std::size_t start = 0;
    // The byte after the 0x02, or 0 if the 0x02 is last.
    std::uint8_t code = 0;
    TextColors colors;

    friend bool operator==(const ColorRun&, const ColorRun&) = default;
};

struct InlineText
{
    // Without the pairs.
    std::string text;
    // Up to MaxInlineColors, in text order. What comes before the first uses the scene's colors.
    std::vector<ColorRun> runs;
};

// 0x4665d0: a code's color. 0x01 and 0xf4, red without background; 0xf0, the scene's; 0xf1, the scene's swapped; 0xf2
// to 0xf8, one from the table (0xf8 with background 0x96000000 if `context`'s flag is off); any other, white without
// background.
[[nodiscard]] ENGINE_API TextColors InlineColor(std::uint8_t code, const InlineColorContext& context);

// 0x4667a0: the clean text and its runs.
[[nodiscard]] ENGINE_API InlineText ParseInlineColors(std::string_view text, const InlineColorContext& context);
} // namespace Client::Render::Text
