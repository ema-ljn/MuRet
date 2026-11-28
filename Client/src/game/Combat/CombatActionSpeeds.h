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
// 0x4f3f80: las velocidades compartidas de Player.bmd, desde los stats propios protegidos.
void RefreshCombatActionSpeeds(const Scenes::WorldEntryResources& resources);
// Lo mismo con el mundo y el rand() de la CRT: 0x4f4a10 la llama al pegar (CL9 hito 3).
void RefreshCombatActionSpeeds(Kernel::WorldState& world, Crt::CrtRandom& random);
} // namespace Client::Combat
