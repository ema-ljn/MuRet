#pragma once

#include "Core/Expected.h"
#include "Kernel/CharacterAppearance.h"

#include <cstddef>
#include <string>

namespace Client::Kernel
{
struct ActorState;
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// Another character's equipment from the 17 bytes of the list (0x5139f0), in the game: 0x12 (0x4c91fa) and F3 13
// (0x4e316e, InventoryMoves.h). Contract: Client/docs/research/06-render-escenas-y-ui.md, "Los otros jugadores en el
// viewport". All *static*, from the ASM (0x5139f0..0x5140b3).
namespace Client::Actors
{
class IHelperHooks;

// 0x513b3c..0x513ced with the fourth argument at 0: the pet of b[4] & 3 (0x1c12 + the value with its helper: 0x1c12
// with 0, none with 1, 0xc6 with 2; with 3, 0x1c15 and 0x110 if b[9] & 1, or no pet with +0x31b and +0x31c at 0),
// then the dark horse of b[11] & 1 (0x1c16 and 0xbf) and Fenrir of b[11] & 4 (0x1c37, +0x31b at b[15] & 3 and helper
// 0x14a with 1, 0x14c with 2 and 0x14b with the rest), each one with 0x443fa0 in that order (`helpers`). The selection
// (SelectionActor) and the game share it.
[[nodiscard]] Core::Expected<void, std::string> CreateCharacterSetPets(Kernel::WorldState& world, std::size_t slot,
                                                                       Kernel::Equipment equipment,
                                                                       const IHelperHooks& helpers);

// 0x5139f0(index, equipment, 0, 0) in the game. `index` is what 0x5110d0 gave: 400 (the pool's record 401, which
// exists) if there is none. Without type 0x20a at +2, nothing. Otherwise, in this order: the hands (the 12-bit type,
// ((b[11 or 12] & 0xf0) << 4) | b[0 or 1], plus 0x212; the empty 0x1fff never happens: 0xfff keeps 0x1211, Q-C102);
// with class 4 and type 0xa05 on the left, the raven 0x1c17 and 0x420cf0; the wings; 0x443d40 (the character's
// helpers, off); the pet (CreateCharacterSetPets); 0x414340; +0x360 with b[11] & 2 (4 if it was not between 1 and 3;
// without the bit, 0 if it was not between 1 and 3); the level of the hands (bits 0..2 and 3..5 of b[5..7] through
// 0x513340) and +0x2fa and +0x31a at 0. With +0x3e3 at 0, also: the five body parts (index 0x1ff is the class's)
// with their levels, the excellent (b[9]) and set (b[10]) bits of each part and of the hands, +0x358 with b[10] & 1,
// 0x4053f0 and 0x512930. What MuRet does not have stops with an error after what came before. 0x443d40 and 0x443fa0
// go through `helpers`.
[[nodiscard]] Core::Expected<void, std::string> ApplyCharacterSet(std::size_t index, Kernel::Equipment equipment,
                                                                  const Scenes::WorldEntryResources& resources,
                                                                  const IHelperHooks& helpers);

// 0x5139f0(0, equipment, object, helper), as the letters' portrait calls it (0x497810): the same on a character
// outside the pool with its own helper (`helper`, Scenes::OwnHelper). Instead of 0x443d40 it turns that helper off
// (0x513b39), and each pet goes straight to 0x443d70 on it (0x513b7b..0x513ce5), without the conditions of 0x443fa0
// (on maps 18 to 23 0x443d70 builds nothing): the first one that builds it leaves it alive and the later ones do not
// touch it (Q-C1026). The raven (0x420cf0) goes to the character as in the pool.
[[nodiscard]] Core::Expected<void, std::string> ApplyCharacterSetTo(Kernel::ActorState& actor,
                                                                    const IHelperHooks& helper,
                                                                    Kernel::Equipment equipment,
                                                                    const Scenes::WorldEntryResources& resources);
} // namespace Client::Actors
