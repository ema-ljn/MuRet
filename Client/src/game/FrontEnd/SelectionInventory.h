#pragma once

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::FrontEnd
{

// 0x633f91 -> 0x5bbe10: empties the equipment, the backpack and the three NPC grids before the partial selection
// reset. It keeps the other bytes of each item and does not touch the grid of 0x78ab0b8.
void ClearSelectionInventory(Kernel::WorldState& world, Crt::CrtRandom& random);
} // namespace Client::FrontEnd
