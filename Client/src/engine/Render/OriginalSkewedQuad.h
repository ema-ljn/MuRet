#pragma once

#include "Core/PixelSize.h"
#include "EngineApi.h"
#include "Render/QuadCorner.h"
#include "Render/TextureSlot.h"

// 0x622000's quad, the login's running sand (06 § Primitivas 2D and § El login, el dibujo): 0x621b60's, always scaled
// from 640 × 480, without alpha, and with the left side's v shifted a quarter of dv, so the texture slants.
namespace Client::Render
{
// 0x622000's nine arguments in order: 0x622000(slot, x, y, w, h, u, v, du, dv), with x, y, w and h in 640 × 480.
struct OriginalSkewedQuad
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
};

// 0x622000's corners: 0x621b60's positions with both flags on, and UVs (u, v + 0.25 dv), (u, v + 0.75 dv),
// (u + du, v + dv) and (u + du, v). Bottom left comes from the unrounded v + dv minus 0.25 dv (0x6220b7..0x6220c2),
// not from v + 0.75 dv.
[[nodiscard]] ENGINE_API QuadCorners SkewedFanCorners(const OriginalSkewedQuad& quad, Core::PixelSize screen);
} // namespace Client::Render
