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
// 0x6025fc..0x602764, after body42 of ED/EE. The index identifies the effect's durable slot, even if the drawing uses
// its projection; the pose view is obtained after the body.
[[nodiscard]] Core::Expected<void, std::string>
DrawCrywolfMissileTail(Effects::ModelEffect& effect, std::uint64_t physicalSlot, Particles::Particles& particles,
                       Particles::FrameSprites& sprites, Crt::CrtRandom& random,
                       std::span<const Math::BoneTransform> globalPose, float worldClock);
} // namespace Client::Effects
