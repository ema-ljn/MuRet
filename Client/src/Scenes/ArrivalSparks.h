#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
struct ActorState;
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{

// 0x403ae7..0x403d02 y 0x4c98a0..0x4c9ac1: cuatro 615/7, tres 5f6/1 y cuatro 614/17 sin dueño.
// Los cuatro anchos sorteados se intercalan con los constructores, incluso si el pool está lleno.
[[nodiscard]] Core::Expected<void, std::string> CreateArrivalSparks(Kernel::ActorState& actor,
                                                                    Kernel::WorldState& world, Crt::CrtRandom& random);
} // namespace Client::Scenes
