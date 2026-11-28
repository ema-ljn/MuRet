#pragma once

#include "Core/Expected.h"
#include "Math/HalfLifeMath.h"

#include <cstdint>
#include <span>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Particles
{
class Particles;
class FrameSprites;
} // namespace Client::Particles

namespace Client::Effects
{
struct ModelEffect;
} // namespace Client::Effects
namespace Client::Effects
{
// 6025fc..602764, después del cuerpo42 de ED/EE. El índice identifica el lugar durable del efecto,
// incluso si el dibujo usa su proyección; la vista de pose se obtiene después del cuerpo.
[[nodiscard]] Core::Expected<void, std::string>
DrawCrywolfMissileTail(Effects::ModelEffect& effect, std::uint64_t physicalSlot, Particles::Particles& particles,
                       Particles::FrameSprites& sprites, Crt::CrtRandom& random,
                       std::span<const Math::BoneTransform> globalPose, float worldClock);
} // namespace Client::Effects
