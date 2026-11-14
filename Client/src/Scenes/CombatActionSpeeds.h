#pragma once

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models
namespace Client::Scenes
{
class WorldState;
struct WorldEntryResources;
// 0x4f3f80: las velocidades compartidas de Player.bmd, desde los stats propios protegidos.
void RefreshCombatActionSpeeds(const WorldEntryResources& resources);
// Lo mismo con el mundo y el rand() de la CRT: 0x4f4a10 la llama al pegar (CL9 hito 3).
void RefreshCombatActionSpeeds(WorldState& world, Render::Models::CrtRandom& random);
} // namespace Client::Scenes
