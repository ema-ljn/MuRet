#pragma once

#include "Core/Expected.h"
#include "Tables/TextTable.h"

#include <cstdint>
#include <string>

namespace Client::Ui
{
struct GameHudContext;
} // namespace Client::Ui

// The game's tooltips (06 § Los tooltips, CL8 milestone 5): 0x5f7bc0, what the game's drawing calls at 0x63769b (the
// item under the mouse, the bar's skill and the inventory's set window), a set's label (0x40eca0, at 0x6376c6) and the
// box they all draw (0x5af090). The lines go in HudState::tooltip, which lasts from one frame to the next like the .bss
// arrays.
namespace Client::Tooltips
{
struct TooltipLines;

// 0x5af090(x, y, lines, width, mode, frame): the box of the first `count` lines of HudState::tooltip, up to the first
// empty one. It measures each one with its font (bold with bold non-zero) and builds, in 640 × 480:
//   - the width, the largest cx / rx (or 2 · width / rx with a width above 0) + 4, centered on x, without passing the
//     left or the right edge (W / rx − width − 1);
//   - the height, (cy · wholes + cy · 0.5 · halves) / (ry · 0.90909088), where a line that starts with '\n' is a half
//     and cy is the last measured one's;
//   - with the frame at 1, the four black edges of 1 and the black background with 0.8 alpha (0x621aa0);
//   - each line with its color (the glColor3f and the background of 0x6c8e5c and 0x6c8e64), with the 0x467060 mode in
//     a box of (width − 2) · rx pixels from x + 1, with the background over all of it (ITextDrawer::DrawColumns): 1
//     puts the text against the left edge (the cards' portrait help, 0x497f3f), 2 centers it
//     (Render::Text::CenterInBox) and 3 puts it against the right edge; one with '\n' or " " only moves down half a
//     line or one, without drawing.
// Each line moves down (cy / ry) · 1.1. It ends with the color white and no blending. Modes other than 1, 2 or 3 cut.
[[nodiscard]] Core::Expected<void, std::string> DrawTheTooltipBox(Ui::GameHudContext& context, std::int32_t x,
                                                                  std::int32_t y, std::int32_t count,
                                                                  std::int32_t widthOverride, std::int32_t textMode,
                                                                  std::int32_t frame);

// 0x40d530(line, option, value): a set's option with its value (the low byte) in its Text.bmd format text: 950 +
// option up to 6, 632 for 7, 949 + option from 8 to 0x14, 970, 984 and 983 for 0x15 to 0x17 and 947 + option from 0x18
// to 0x23. With another, the line stays as it was.
[[nodiscard]] Core::Expected<void, std::string> WriteTheSetOption(const Tables::TextTable* texts, std::string& line,
                                                                  std::uint8_t option, std::uint8_t value);

// 0x5f7bc0: no blending and 0x77f3b66 at 0; a non-zero repair mode (0x78abb0c) becomes 1. With an item under the mouse
// (0x78abaa8), with the own record open around it, 0x5b8150 (repairing, with the mouse over the inventory) or
// 0x5b2aa0; with the bar's skill (0x6c9320), 0x5b90b0; with the inventory open, the set window (0x40e850). The bodies
// MuRet does not have cut with their address.
[[nodiscard]] Core::Expected<void, std::string> DrawTheTooltips(Ui::GameHudContext& context);

// 0x40eca0: with +0x9305, the label of set +0x9305 − 1 of ItemSetOption.bmd at the top left: its name (with 1089 and
// 159) in gold and each option of its pairs, its two extras and the full set's five in blue. An unused set (+0x3c at
// 0xff) turns it off.
[[nodiscard]] Core::Expected<void, std::string> DrawTheSetLabel(Ui::GameHudContext& context);
} // namespace Client::Tooltips
