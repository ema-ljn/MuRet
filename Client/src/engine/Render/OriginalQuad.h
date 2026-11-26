#pragma once

#include "Core/PixelSize.h"
#include "EngineApi.h"
#include "Render/QuadCorner.h"
#include "Render/TextureSlot.h"

// 0x621b60's quad, the original UI's atom (06 § Primitivas 2D): a textured rectangle with y flipped (H − y), position
// and size scaled from 640 × 480 if its flags ask, and an alpha that changes the vertex color.
namespace Client::Render
{
// 0x621b60's arguments in order: 0x621b60(slot, x, y, w, h, u, v, du, dv, scale size, scale position, alpha). (x, y) is
// the top-left corner, y from the top, in 640 × 480 if scalePosition, else in pixels.
struct OriginalQuad
{
    TextureSlot slot{};
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float du = 1.0f;
    float dv = 1.0f;
    bool scaleSize = false;
    bool scalePosition = false;
    // Above 0 (DAT_006b86a0), each vertex gets (1, 1, 1, alpha) and the GL color is left white (Q-C57); at 0, the quad
    // uses the current color.
    float alpha = 0.0f;
};

// 0x621b60's corners: (x, H − y), (x, H − y − h), (x + w, H − y − h) and (x + w, H − y), with UVs (u, v), (u, v + dv),
// (u + du, v + dv) and (u + du, v); the top v is texture row 0. The bottom edge is subtracted from the unrounded top,
// as on the x87 (FST and FLD ST0 at 0x621beb..0x621bf3): at 1280 × 1024 the login background 0x15 ends at 287.99997,
// not 288. 0x621aa0, the untextured quad, does the same math with both flags on.
[[nodiscard]] ENGINE_API QuadCorners FanCorners(const OriginalQuad& quad, Core::PixelSize screen);
} // namespace Client::Render
