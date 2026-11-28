#pragma once

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "Ui/Widget.h"

// The game cursor (0x5ac2d0): the original hides the Windows one (ShowCursor(FALSE) in WinMain, 0x4ba238, and in the
// WndProc, 0x4b3327) and draws its own at the end of each scene's UI: login (0x635560), selection (0x634980) and
// game (0x6376d7). Contract: 06 § Lo demás del login.
namespace Client::Ui
{
// Interface\Cursor.tga and Interface\CursorPush.tga, uploaded by 0x61ccb0 (0x61ccf9 and 0x61cd12) with GL_LINEAR and
// GL_CLAMP.
inline constexpr Render::TextureSlot CursorSlot{2};
inline constexpr Render::TextureSlot CursorPushSlot{3};

// The mouse position the WndProc writes (0x4b358f..0x4b35c8) and the cursor reads: x at 0x7d3682c and y at
// 0x7d36828, LOWORD · 640 / W and HIWORD · 480 / H, unsigned and unclamped (Q-C47). From `input`, in framebuffer
// pixels: the pixel below (−0.5 is −1) and its low 16 bits, like the lParam's LOWORD and HIWORD. Outside the window
// the original gets no movement (it does not capture the mouse); SDL does, with a button down, and then the cursor
// follows outside (08, P-C27 g). A position that is not a number or an empty screen gives 0.
[[nodiscard]] Point MouseIn640x480(const Input::InputState& input, Core::PixelSize screen);

// 0x5ac7da: the 24 × 24 cursor at (x − 2, y − 2) of 640 × 480, scaled in size and position, whole and without alpha;
// with the left button down (0x7d36874, which in login and selection only WM_LBUTTONDOWN and WM_LBUTTONUP write), the
// CursorPush (0x5ac76e). The other cursors (4 to 0xa, 0x65a and 0x68d) are the game's: chosen by 0x6c8e28 to
// 0x6c8e34, 0x78abb0c, 0x78abb20, 0x7d3a10c and 0x77f3b5c, which in login and selection are −1 or 0, and
// 0x659290(0x200) at 0x5ac61c, which is false in selection and unverified in login (08, P-C27 h).
[[nodiscard]] Render::OriginalQuad CursorQuad(Point mouse, bool leftDown);

// 0x620a00(1), glColor3f(1, 1, 1) and CursorQuad's quad with `input`'s mouse and button.
void DrawGameCursor(Render::SpriteBatch& sprites, const Input::InputState& input, Core::PixelSize screen);
} // namespace Client::Ui
