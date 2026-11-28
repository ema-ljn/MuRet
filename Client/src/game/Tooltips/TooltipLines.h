#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// The tooltip lines (06 § Los tooltips, CL8 milestone 5): three .bss arrays that the bodies (0x5b2aa0, 0x5b8150,
// 0x5b90b0), the set window (0x40e850), its label (0x40eca0) and the F1 helps (0x5b0550) fill, and that 0x5af090
// draws. Nobody resets them to 0 between frames: each body clears its own and what it does not write stays from the
// previous one.
namespace Client::Tooltips
{
// 0x78793c8: 30 lines of 100 bytes, up to 0x7879f80 (where the cursor's item starts).
inline constexpr std::size_t TooltipLineCount = 30;
// What each line takes, with its '\0'. One that sprintf writes longer overwrites the next one (Q-C591).
inline constexpr std::size_t TooltipLineBytes = 100;
// The bodies and the label clear 20 colors (0x14 dwords with rep stosd) of the 30.
inline constexpr std::size_t TooltipClearedColors = 20;

// The colors of 0x5af090 (the switch at 0x5af368): the line's glColor3f and, with 5, 7, 8 and 9, the background.
enum TooltipColor : std::int32_t
{
    TooltipWhite = 0,
    TooltipBlue = 1,
    TooltipRed = 2,
    TooltipGold = 3,
    TooltipGreen = 4,
    TooltipWhiteOnBlue = 5,
    TooltipPurple = 6,
    TooltipWhiteOnRed = 7,
    TooltipWhiteOnTeal = 8,
    TooltipGreenOnRed = 9,
    TooltipGray = 10,
};

struct TooltipLines
{
    // 0x78793c8 + 100 · i: the text up to its '\0'. An empty line cuts the box there (0x5af0c5).
    std::array<std::string, TooltipLineCount> text{};
    // 0x787a560 + 4 · i: the color of 0x5af090.
    std::array<std::int32_t, TooltipLineCount> colors{};
    // 0x78a9230 + 4 · i: with anything non-zero, the bold font (0x5763558). No body clears them.
    std::array<std::int32_t, TooltipLineCount> bold{};
    // 0x78abb30: the next line, and 0x78abb34: how many "\n" it carries (the half lines of the height). The item body
    // (0x5b2aa0) and the repair one (0x5b8150) set them to 0 at the start; 0x5af590 continues them. The F1 helps
    // (0x5b057b) set only the first to 0.
    std::int32_t next = 0;
    std::int32_t halves = 0;

    // memset of the colors the bodies clear (rep stosd of 0x14).
    void ClearColors()
    {
        for (std::size_t i = 0; i < TooltipClearedColors; ++i)
            colors[i] = 0;
    }
    // The first byte of each line at 0.
    void ClearTexts()
    {
        for (std::string& line : text)
            line.clear();
    }
};
} // namespace Client::Tooltips
