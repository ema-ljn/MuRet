#pragma once

#include "Core/Expected.h"
#include "Render/Models/HalfLifeMath.h"

#include <cstdint>
#include <span>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::World
{
struct ModelEffect;
class Particles;
class FrameSprites;
} // namespace Client::World
namespace Client::Scenes
{
// 6025fc..602764, después del cuerpo42 de ED/EE. El índice identifica el lugar durable del efecto,
// incluso si el dibujo usa su proyección; la vista de pose se obtiene después del cuerpo.
[[nodiscard]] Core::Expected<void, std::string>
DrawCrywolfMissileTail(World::ModelEffect& effect, std::uint64_t physicalSlot, World::Particles& particles,
                       World::FrameSprites& sprites, Crt::CrtRandom& random,
                       std::span<const Render::Models::BoneTransform> globalPose, float worldClock);
} // namespace Client::Scenes
