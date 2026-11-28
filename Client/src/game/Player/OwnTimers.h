#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"

#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Player
{
// 0x4fc450(c) from 0x4fd6fe, with the published hero ([0x743a170]; with another it leaves at 0x4fc464): the three
// WORDs of [0x7681eb0], with the own record open (Q-C246). `items` is Item.bmd, which the words read.
[[nodiscard]] Core::Expected<void, std::string>
StepTheOwnTimers(Kernel::WorldState& world, const Tables::ItemTable* items, Crt::CrtRandom& random);
} // namespace Client::Player
