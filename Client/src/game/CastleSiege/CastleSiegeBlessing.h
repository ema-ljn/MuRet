#pragma once
#include "Core/Expected.h"
#include <cstdint>
#include <string>
namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Particles
{
class Particles;
}
namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::CastleSiege
{
// 0x42afe0 → 0x42ddb0: only the caller decides map, war and visibility. The helper keeps the radius and the frame.
[[nodiscard]] Core::Expected<void, std::string>
DrawTheCastleBlessing(const Kernel::ActorState& actor, std::uint16_t heroGuildIndex, float markX, float markY,
                      std::int32_t logicSteps, Particles::Particles& particles, Crt::CrtRandom& random);
} // namespace Client::CastleSiege
