#pragma once

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt
namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Combat
{
// 0x4f3f80: Player.bmd's shared speeds, from the protected own stats.
void RefreshCombatActionSpeeds(const Scenes::WorldEntryResources& resources);
// The same with the world and the CRT's rand(): 0x4f4a10 calls it when striking.
void RefreshCombatActionSpeeds(Kernel::WorldState& world, Crt::CrtRandom& random);
} // namespace Client::Combat
