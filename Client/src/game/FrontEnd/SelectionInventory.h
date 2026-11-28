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

// 0x633f91 -> 0x5bbe10: vacía el equipo, la mochila y las tres grillas de NPC antes del reset parcial de selección.
// Conserva los demás bytes de cada ítem y no toca la grilla de 0x78ab0b8.
void ClearSelectionInventory(Kernel::WorldState& world, Crt::CrtRandom& random);
} // namespace Client::FrontEnd
