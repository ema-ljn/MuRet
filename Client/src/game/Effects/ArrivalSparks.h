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

namespace Client::Effects
{

// 0x403ae7..0x403d02 and 0x4c98a0..0x4c9ac1: four 0x615/7, three 0x5f6/1 and four 0x614/17 without an owner. The four
// drawn widths interleave with the constructors, even with the pool full.
[[nodiscard]] Core::Expected<void, std::string> CreateArrivalSparks(Kernel::ActorState& actor,
                                                                    Kernel::WorldState& world, Crt::CrtRandom& random);
} // namespace Client::Effects
