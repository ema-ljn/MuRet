#pragma once

#include "Core/Expected.h"
#include "Actors/ActionSend.h"

#include <cstdint>
#include <string>

namespace Client::Protocol::Movement
{
struct Action;
}

namespace Client::Models
{
struct Skeleton;
} // namespace Client::Models

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// A character's action arriving from the GameServer (04 § La acción, 18; 06 § El chat, "El emote"). The GameServer
// forwards it to those in the viewport, not to whoever did it (CGActionRecv, 0x425cd0 of the GS): one's own emote
// shows through 0x576a60 and the others' through here. *Static*.
namespace Client::Actors
{

// 0x4cb750: the character with the key (0x5110d0, the first live one of the 400; with none, the ghost record 400:
// Q-C778). With the dark horse (+0x318 at 0x1c16) it does nothing. Otherwise, +0x378 (Movement) at 0, the angle of
// the direction ((pkt[5] − 1) · 45) and X and Y at the center of its target cell (+0x39a and +0x39b · 100 + 50,
// Q-C779), and the action of pkt[6]: 0x12 sets 0x98 and sound 0x6f; 0x78 and 0x79 strike (0x4f4a10, SetActorAttack,
// and then +0x381 at 1, the frame at 0, the target +0x3a4 with the hero's slot and [0x5767964] with the index); 0x7a
// and 0x7b go to 0x4f2250 (sitting, with the C1 18 of `sendAction`); 0x7e sets 5; from 0x80 to 0x9b the table's
// animation, with the elf's; and any other, itself (0x4ea030 does not set one the model lacks). It draws no rand()
// except in 0x4f2250 (the suffix) and in 0x4f4a10: the speeds of the own record (0x4f3f80, 3,848 when it closes)
// and those of its eight calls to _rand (0x69fd70) the branch reaches (with a player, 0x4f4db9 and 0x4f4e07/0x4f4e1c
// for the weapons and 0x4f5192 for the sound).
[[nodiscard]] Core::Expected<void, std::string> ApplyActionReply(const Protocol::Movement::Action& reply,
                                                                 const Scenes::WorldEntryResources& resources,
                                                                 const ActionSender& sendAction);

// The table of 0x4cb871..0x4cbcc8: from 0x80 to 0x9b, the animation of each action, and another for the elf
// (+0x1d4 & 7 at 2) from 0x80 to 0x91. Outside the table, the action itself. The letters' portrait
// (0x495b56..0x495da1) has the same one from 0x80 to 0x94.
[[nodiscard]] std::uint8_t TableAnimationOf(std::uint8_t characterClass, std::uint8_t action);

// The skeleton of model `model` as 0x4ea030 reads it (0x5769ad8 + model · 0xe0): first those 0x612d90 and 0x612270 load
// (Assets::AssetsState::fauna, with the map's library behind it), then those of `resources.models`.
[[nodiscard]] const Models::Skeleton* MonsterSkeletonOf(const Scenes::WorldEntryResources& resources,
                                                        std::uint16_t model);

// 0x4ea030(character, action) with the skeleton of MonsterSkeletonOf. A model that is not there is the original's
// record at 0 (+0x26, no actions): it sets nothing and is not an error. `address` is for the caller.
[[nodiscard]] Core::Expected<void, std::string> SetMonsterAction(Kernel::ActorState& actor, std::uint8_t action,
                                                                 std::uint32_t address,
                                                                 const Scenes::WorldEntryResources& resources);
} // namespace Client::Actors
