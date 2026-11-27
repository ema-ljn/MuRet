#pragma once
#include "Core/Expected.h"
#include <cstdint>
#include <string>
namespace Client::Crt
{
class CrtRandom;
}
namespace Client::World
{
class Particles;
}
namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Scenes
{
// 42afe0→42ddb0: sólo el caller decide mapa, guerra y visibilidad. El helper conserva el radio y el frame.
[[nodiscard]] Core::Expected<void, std::string>
DrawTheCastleBlessing(const Kernel::ActorState& actor, std::uint16_t heroGuildIndex, float markX, float markY,
                      std::int32_t logicSteps, World::Particles& particles, Crt::CrtRandom& random);
} // namespace Client::Scenes
