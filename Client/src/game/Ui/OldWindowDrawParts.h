#pragma once

#include "Core/PixelSize.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindows.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace Client::Render
{
class SpriteBatch;
}

// The old windows' draw parts their classes share (CL8 milestone 7 and CL9 milestone 9; 06 § HUD y ventanas): the
// primitives of 0x621aa0 and 0x621b60 with 0x491960's palette, 0x5542f0's text, the button, the tab headers, the
// checkbox, the template list (0x47a810 and its equals) with its bar and its chosen row, and the EDIT. Used by
// Ui/OldWindowDraw.cpp (the frame, the box and the window list) and Messenger/FriendWindowsDraw.cpp (the friends
// window), and meant for the chat-with-a-friend and letter windows (milestone 10). All in 640 × 480.
namespace Client::Ui::OldDraw
{
// The re-placing messages can create an EDIT and change the focus within the same draw pass.
[[nodiscard]] OldWindowDrawView WithCurrentFocus(const OldWindowManager& manager, const OldWindowDrawView& view);

// _ftol (0x69fd3c): toward 0.
[[nodiscard]] std::int32_t Truncate(double value);

// rx and ry (0x5762ea8 and 0x5762eac): W / 640 and H / 480 as float. Without a screen, 1.
[[nodiscard]] double RatioX(Core::PixelSize screen);
[[nodiscard]] double RatioY(Core::PixelSize screen);

[[nodiscard]] float ToFloat(std::int32_t value);

// glColor4f(g, g, g, 1).
void Gray(Render::SpriteBatch& sprites, float gray);

// 0x459ec0: glColor4f(1, 1, 1, 1) and glEnable(GL_TEXTURE_2D), after the untextured quads.
void WhiteWithTexture(Render::SpriteBatch& sprites);

// 0x491960(index, alpha) with OldWindowPalette: an index outside the 19 does not change the color.
void SetThePalette(Render::SpriteBatch& sprites, std::int32_t index, float alpha);

// 0x621b60(slot, x, y, w, h, u, v, du, dv, 1, 1, 0): scaled in size and position, without alpha.
void Quad(Render::SpriteBatch& sprites, std::uint16_t slot, float x, float y, float width, float height, float u,
          float v, float du, float dv);

// 0x5542f0(x, y, text, 0, 1, 0) → 0x465950: the text with the DC's font and 0x6c8e5c's colors; an empty one is not
// drawn. Returns what 0x467060 leaves on the x87: the text's height in 640 × 480 (cy / ry), half if it starts with
// '\n', or 0 without text.
double DrawText(const OldWindowDrawContext& context, std::int32_t x, std::int32_t y, std::string_view text);

// 0x459f00(x, y, w, h, 1) with the draw's mouse.
[[nodiscard]] bool MouseIn(const OldWindowDrawView& view, std::int32_t x, std::int32_t y, std::int32_t width,
                           std::int32_t height);

// 0x45b2e0, the button's vtable + 0x14: win_button.tga in the mouse's gray and the text centered. Disabled, in red and
// without going back to the opaque blend.
void DrawTheButton(const OldWindowDrawContext& context, const OldButton& button, const OldWindowDrawView& view);

// 0x4a2460(x, y, w, h, count, current): `count` w × h headers, side by side from x. The current one, with the top
// line and the sides (palette 2) and the fill of the 5, one point inside; the others, one point lower, with the top
// line, the right side and the bottom one, and the fill of the 6 two lower.
void DrawTheTabHeaders(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y, std::int32_t width,
                       std::int32_t height, std::int32_t count, std::int32_t current);

// 0x463090(x, y, checked): without blend, the 9 × 9 frame with palette 0; with the normal blend, the 5 × 5 mark
// (win_check.tga, 0x64a) at (x + 2, y + 2). Leaves the normal blend and white with texture.
void DrawTheCheckbox(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y, bool checked);

// 0x461890 and 0x462620, the vtable + 0x7c of the friend list and of the window list (identical byte for byte):
// places the bar (+0x70, 0x47b9c0), the win_scrollbar.jpg arrows (0x649) on the right edge, flipped with the click on
// them, the track's two edges (palette 2) and the thumb in three quads: with fewer than the rows, the whole track.
void DrawTheListBar(Render::SpriteBatch& sprites, OldList& list, std::size_t size, const OldWindowDrawView& view);

// A list instance's vtable + 0x7c, + 0x80 and + 0x84, as its draw calls them.
struct OldListDrawing
{
    // +0x7c: before the rows (the bar, in the CL9 ones).
    std::function<void()> head;
    // +0x80(row): row `row` (from 0, the first shown) with the deque's element `at`. What it returns moves the rows as
    // in the step: negative skips, 0 repeats the row and positive goes on.
    std::function<std::int32_t(std::int32_t row, std::size_t at)> row;
    // +0x84: after (0x472110 and 0x4722f0, a ret, in the CL9 ones).
    std::function<void()> tail;
};

// 0x47a810, the list's vtable + 0x14 (the window list's 0x47d730 and the letter list's 0x47eed0 are the same except
// for the element size): +0x7c, the first shown (+0x74, 0x47bb00), glColor3f(1, 1, 1), the normal font in the DC
// (0x5763554) and the rows up to the ones it shows or to the end, and +0x84.
void DrawTheOldList(const OldWindowDrawContext& context, const OldList& list, std::size_t size,
                    const OldListDrawing& drawing);

// The start of 0x461cd0 and 0x462a60 (vtable + 0x80): with the normal blend, the chosen row (+0x68 equal to the
// scroll + row + 1) goes over a white band 13 high, 3 above its y, with alpha 0.7 if the list has the arrow keys
// (0x5762ec0) and 0.3 otherwise, and the text in black; the others, in 0xffc8dce6. The text background at 0.
void DrawTheChosenRow(const OldWindowDrawContext& context, const OldList& list, std::size_t size, std::int32_t row,
                      std::int32_t focusedList);

// 0x468320, the EDIT's vtable + 0x14, with what MuRet has of it (P-C181): the EDIT's bitmap box in +0xa0's color
// (without +0xc4, the text width plus +0xa8 + 1.5, up to the EDIT's, and its height; with +0xc4, the whole width and,
// with fewer rows than +0xd0, the rows' height), the text in +0x9c's with the normal font, 3 pixels inside (with
// +0xc4, the rows of Ui/OldEditLines.h from the first in view, each as tall as "Q"), and the caret: GetCaretPos's
// column (0x468805..0x46881d: with 1.5, a single one) in the caret's row, with the text's ink in the background and
// the rest in 0xffc8c8c8, on 12 of every 24 draws with the Windows focus; with +0xc4 at 1, the bar (0x468970) with
// `view`'s mouse; +0x80 counts the draws. With +0x4c & 8, before, the black of its width and of the height of "Q".
// With the password (+0xb0, TextField::Masked: the dialogs' second EDIT, CL9 milestone 8), one '*' per byte, the
// width measured in bold (0x5763558) and the DC with the normal one after. The bitmap over 256 pixels, which the
// original splits into two textures, is the same draw.
void DrawTheOldEdit(const OldWindowDrawContext& context, OldEdit& edit, bool focused,
                    const OldWindowDrawView& view = {});

// 0x468970: the multiline EDIT's bar. Sets +0xd4..+0xe4 (13 wide, the track from y + 9 to y + h − 9 and the thumb of
// track · min(+0xd0 / rows, 1) at track · min(GetScrollPos / rows, 1)); the win_scrollbar.jpg arrows at x + w − 15,
// y − 4 and y + h − 9, flipped with the click on them; with the normal blend, the track's two edges (palette 2); and
// without blend, the thumb in three quads.
void DrawTheEditBar(const OldWindowDrawContext& context, OldEdit& edit, std::int32_t lineCount,
                    const OldWindowDrawView& view);
} // namespace Client::Ui::OldDraw
