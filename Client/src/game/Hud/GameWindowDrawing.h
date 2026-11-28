#pragma once

#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"

#include <cstdint>
#include <string_view>

// The primitives shared by the step's framed windows (CL8 milestone 6, 08 P-C92): the party (0x5f0be0) and the
// commands (0x40a650). In 640 × 480, with the GL state they leave and the text globals (0x6c8e5c, 0x6c8e64, the DC's
// font and 0x77f31b8/0x77f31bc) in HudState.
namespace Client::Hud
{
struct GameHudContext;
}

namespace Client::Hud::WindowDrawing
{
// 0x621b60(slot, x, y, w, h, 0, 0, du, dv, 1, 1, 0): scaled in size and position, no alpha, with whatever color and
// blend are set.
void Quad(Render::SpriteBatch& sprites, std::uint16_t slot, float x, float y, float width, float height, float du,
          float dv);

// 0x471520(x, y, w, h, fill): the frame's four 2-wide lines: top and bottom with 0x117 (u 0.0390625, v 0.3125, du
// 0.6640625, dv 0.125; the bottom one 1 wider), the sides with 0x104 (u 0.00390625, v 0.3125, du 0.0078125, dv
// 0.48828125). With the fill (1), first, the untextured glColor4ub(0x92, 0x90, 0x8d, 0xc8) box (0x621aa0) and
// 0x459ec0 (the white and the texture); only the positions window asks for it (0x64f1c0).
void DrawTheFrame(Render::SpriteBatch& sprites, float x, float y, float width, float height, bool filled);

// 0x5ee220(x, y, 0), and the copy 0x5f0be0 has: Interface\Item_Back01.jpg (0x104), 190 × 256 at (x, y), and
// Item_Back02.jpg (0x105), 190 × 177 at y + 256 (0x6b8840, the x87 sum stored as float), both with du = 0.7421875 and
// dv = 1 and 0.69140625.
void DrawTheWindowBack(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y);

// SelectObject on the text DC (0x57634f4): the font the following texts measure and draw with.
void SelectFont(GameHudContext& context, Render::Text::FontKind font);

// 0x554180(x, y, text, 0, 0, 0): mode 1 from x, with the DC's font and the colors of 0x6c8e5c and 0x6c8e64. An empty
// text is not drawn but is measured all the same: its size in 640 × 480 stays in 0x77f31b8 and 0x77f31bc.
void DrawText(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text);

// The width of a 0x554180 box in pixels: W · k / 640, the unsigned division of 0x5f0cd6..0x5f0cef.
[[nodiscard]] std::int32_t BoxWidth(const GameHudContext& context, std::uint32_t designWidth);

// 0x554180(x, y, text, box, 1, ·): 0x467060 with mode 2 centers the text in a box of `box` pixels starting at
// _ftol(x · rx), with the background over the whole box (Render::Text::CenterInBox and ITextDrawer::DrawColumns,
// Q-C710), and leaves in 0x77f31b8 the width clipped to the box. An empty text measures "0" and draws only the
// background; empty and with no box, nothing.
void DrawInBox(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text, std::int32_t box);

// 0x554180(x, y, text, width, 0, ·): 0x467060 with mode 1 and a width, the box of that width from x
// (0x467102..0x467119); if the text is wider, it shows from column cx − width. Leaves the measure clipped to the width,
// like DrawInBox.
void DrawClipped(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text, std::int32_t width);

// 0x554180(x, y, text, 120 · W / 640, 1, 3), a window's title: DrawInBox with the 120 box.
void DrawTitle(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text);

// 0x554180(x, y, text, width · W / 640, 1, p6), the same as DrawTitle with another box (CL9 milestone 2): the quest
// name goes in a 150 one (0x41bedc and 0x41c322), and the event NPC windows (0x5f58f0, CL9 milestone 11) use 116, 120
// or 130. The sixth argument reaches 0x467060, which does not read it.
void DrawInBox(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text,
               std::uint32_t designWidth);

// 0x623690(x, y, text): x − (cx · 640 / W) / 2 and 0x554180(…, 0, 0, 0).
void DrawCenteredText(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text);

// The windows' close button (0x5f11c2 in the party, 0x40a78e in the commands and 0x5f74a3 in the character):
// glColor3f(1, 1, 1) and Interface\exit_01.jpg (0x118), 24 × 24 at (x + 25, y + 395), x87 sums stored as float, with
// du = dv = 0.75. Returns whether the mouse is inside (x87: left ≤ mouse < left + 24).
[[nodiscard]] bool DrawTheCloseButton(GameHudContext& context, std::int32_t x, std::int32_t y);

// What follows with the mouse over the close button: the normal font, the colors −1 and 0xff000000 and the help
// (0x554340) at (_ftol(x + 25), _ftol(y + 395) − 13), which leaves the normal font and the colors −1 and 0.
void DrawTheCloseHelp(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view help);
} // namespace Client::Hud::WindowDrawing
