#pragma once

#include "Math/OriginalRotation.h"

#include <cstdint>

namespace Client::Models
{
class IModelLibrary;
} // namespace Client::Models

namespace Client::Render
{
class ModelBatch;
} // namespace Client::Render

// The frame's sprites in the eye pass (0x549440).
namespace Client::Particles
{
class FrameSprites;

// 0x549440(0), in the eye pass (after 0x6213e0, with the modelview at identity): each live sprite, with the blend of
// its +4 (0 additive, 1 that of 0x620b10, 2 the normal one, 3 that of 0x620b90: ONE_MINUS_SRC_COLOR/ONE; texture
// 0x637, the normal one), the alpha of 0x549290 and the square of 0x621400 with its texture's size (`textures`) times
// the scale and the alpha, with the view `view` (0x7d366f0). After drawing it, it turns off in modes 0/2. Mode 1
// retains and skips z>350; mode 2 turns off z<=300/NaN without drawing.
void DrawFrameSprites(Render::ModelBatch& batch, FrameSprites& sprites, const Models::IModelLibrary& textures,
                      const Math::OriginalMatrix& view, std::uint8_t mode = 0);
} // namespace Client::Particles
