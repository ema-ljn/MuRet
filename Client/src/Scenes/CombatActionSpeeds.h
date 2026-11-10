#pragma once

namespace Client::Scenes
{
struct WorldEntryResources;
// 0x4f3f80: las velocidades compartidas de Player.bmd, desde los stats propios protegidos.
void RefreshCombatActionSpeeds(const WorldEntryResources& resources);
} // namespace Client::Scenes
