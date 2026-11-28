#pragma once

#include "Core/Expected.h"
#include "Kernel/PlayerKey.h"
#include "Protocol/Actors/Viewport.h"

#include <string>

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// The 1A, 0x4cedd0: Client/docs/research/06-render-escenas-y-ui.md, "La skill que pega a varios".
namespace Client::Combat
{

// 0x4cedd0: the striker is the key's (0x5110d0, or the phantom 400: Q-C778) and stays in [0x5767964]; its hand shine
// (0x53d810, RaiseTheHandShine), sound 0x76 (0x41ed10, in lastSoundId), action 0x96 (0x4ea030, with its model's
// actions: SetMonsterAction, also a monster or an NPC), the skill in its +0x38e (opened through the table: one rand()
// when it closes) and +0x381 at 1. With the 400 all of that goes to the 400 except the action, which reads whatever
// type is there and is discarded with a debug line (Q-C1125). Then, for each target (0x5110d0, the phantom too): an
// even rand(), 0x4f53a0's hit with its +0x384; with +0x384, the damage number (0x548e40, orange yellow of 15, rising)
// and, if the key is the player's (`player`, [0x57678bc]), the own record's life (+0x22) drops by +0x384 down to 0
// (opened and closed through the table: 3848 rand() when it closes). Without the striker's model loaded or with a hit
// that cuts, it cuts with the address.
[[nodiscard]] Core::Expected<void, std::string> ApplyMagicAttackCount(const Protocol::Actors::MagicAttackCount& attack,
                                                                      Kernel::PlayerKey player,
                                                                      const Scenes::WorldEntryResources& resources);
} // namespace Client::Combat
