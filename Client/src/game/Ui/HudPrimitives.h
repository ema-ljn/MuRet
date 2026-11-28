#pragma once

#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "Render/Frame/ITextDrawer.h"

#include <string_view>

// The game HUD's shared primitives (06 § HUD y ventanas, CL8 milestone 2): the FontTest digits (0x6218f0), the small
// gauges (0x5a6af0) and the framed help that shows on hover (0x554340). All in 640 × 480 and with the GL state they
// leave, as in the original.
namespace Client::Ui
{
// Interface\FontTest.tga, the digits' atlas (Assets::DamageTextureSet): 16 columns of 16 pixels in a 32-pixel row of
// the 64 in height.
inline constexpr Render::TextureSlot DigitsSlot{1};

// 0x6218f0(x, y, n, w, h): `number` in decimal (_itoa base 10) centered on `x`: shifts the x by half the digits' width
// (len · w · 0.5) and draws each character c with 0x621b60(1, x, y, w, h, (c − '0') · 0.0625, 0, 0.0625, 0.5, 1, 1,
// 0), scaled and without alpha, with whatever color and blend are set; after each one it adds w · 0.7 as float. A
// negative's '-' is the digit −3, as in the original. Returns the final x.
float DrawNumber(Render::SpriteBatch& sprites, float x, float y, int number, float width, float height);

// 0x5a6af0(x, y, w, h, fill, red, clamp): the small gauge of the helper and of the summon's life. With `keepOnScreen`
// it fits it in (0..640, 0..433): a negative x or y goes to 0, and if x + w + 4 passes 640 the x is 640 − (w + 5) (and
// the same with 433 and the height). Then the shadow (0x620a00(1), black with alpha 0.5, (x + 1, y + 1, w + 4,
// h + 4)), and with the additive blend the frame (0.2, 0, 0) or (0, 0.2, 0.2) at (x, y, w + 4, h + 4), the background
// (50, 10, 0)/255 or (0, 50, 50)/255 at (x + 2, y + 2, w, h) and the fill, (200, 50, 0)/255 or (0, 200, 50)/255, at
// (x + 2, y + 2, fill, h). Ends with 0x620980, without blend.
void DrawGauge(Render::SpriteBatch& sprites, float x, float y, float width, float height, float fill, bool red,
               bool keepOnScreen);

// 0x554340(x, y, text): `help` with the normal font, white on a transparent background (0x6c8e5c at −1 and 0x6c8e64
// at 0, left that way), inside a one-point black frame (x − 2, y − 3, cx/rx + 4, cy/ry + 4) and a black background
// with alpha 0.8 at (x − 1, y − 2, cx/rx + 2, cy/ry + 2), with cx and cy in pixels and rx and ry the screen's. Saves
// the previous blend (0x7d366dc) and restores it at the end; the color stays white.
void DrawHelp(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, int x, int y, std::string_view help);
} // namespace Client::Ui
