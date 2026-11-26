#pragma once

#include "Core/PixelSize.h"
#include "EngineApi.h"

// 640 × 480 coordinates to screen pixels, each axis on its own: 0x620d50 and 0x620d80 (06 § Primitivas 2D), used by
// 0x621b60's quad when its flags ask and always by 0x622000 and 0x621aa0. A non-4:3 screen stretches, as in the
// original. They do not divide: the binary loads W or H as an integer, multiplies by the coordinate and then by the
// float inverse of 640 or 480 (0x3acccccd at 0x6ba070, 0x3b088889 at 0x6ba06c) with the x87 at Windows' 53 bits, and
// the caller stores a float. So 25 scaled at 480 gives 25.000002, not 25.
namespace Client::Render
{
// 0x620d50: W · x · (1/640 as float).
[[nodiscard]] ENGINE_API float ScaleX(float x, Core::PixelSize screen);

// 0x620d80: H · y · (1/480 as float).
[[nodiscard]] ENGINE_API float ScaleY(float y, Core::PixelSize screen);
} // namespace Client::Render
