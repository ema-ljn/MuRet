#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Actors
{
struct CharacterStepContext;
} // namespace Client::Actors

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// What NPCs say from the character step: 0x4fcd00 calls 0x4fc790 for each one, checks the name message's clock
// (0x4fcd65..0x4fce15) and, when the turned 0x20a's animation ends, calls 0x4fc8c0. All three talk with
// 0x557170(name, text, character, 0, −1): the uncolored chat bubble. Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "Lo que dicen los NPC".
namespace Client::Monsters
{

// strcmp(+0x1d9, 0x6c88b0) at 0x4fcda2: "황금의 궁수" (type 236). The name's 10 bytes and +0x1e3 against the first 11
// and, with those 11 equal, +0x1e4 (ActorState::nameSpill) against the 0 that ends it: equal only with +0x1e4 at 0.
[[nodiscard]] bool HasTheMessageName(const Kernel::ActorState& actor);

// 0x4fcd0c..0x4fcd55: with +0x88 at 4, on maps 0 and 2, the 0x20a with subtype 0xd1 to 0xd3 faces a fixed way (+0x1c
// and +0x20 at 0, and +0x24 at 90 on 0 and at 0 on 2). Returns whether it turned: the local that 0x4fce47 reads at the
// end.
[[nodiscard]] bool TurnTheNpc(Kernel::ActorState& actor, std::int32_t map);

// 0x4fc790(c, o) from 0x4fcd60: with +0x88 at 4, one rand() and, if even, the type's message: 0x1cd (type 253) outside
// map 30 Text 1974, 0x1d3 (242) 1975, 0x1d5 (241) 1976 and the 0x20a with +0x376 at 0x101 (257) 1827. Without the text,
// the meter or the table, the one that has to talk cuts.
[[nodiscard]] Core::Expected<void, std::string> SayTheNpcLine(const Kernel::ActorState& actor, std::size_t slot,
                                                              const Actors::CharacterStepContext& context);

// 0x4fcd65..0x4fce15, with any character: the first time (bit 0 of 0x743a174) 0x7437488 = timeGetTime() − 5000; then,
// if more than 4000 ms unsigned passed since 0x7437488 and the name is that of 0x6c88b0, Text 1167 (with 0x743a190 at
// 1) or 1168 (at 0), and 0x7437488 = now and 0x743a190 flips. Without `systemTime` it neither reads nor writes the
// clock, and the named one cuts (Q-C248).
[[nodiscard]] Core::Expected<void, std::string> SayTheNamedLine(const Kernel::ActorState& actor, std::size_t slot,
                                                                const Actors::CharacterStepContext& context);

// 0x4fc8c0(c, o) from 0x4fce50, with the turned 0x20a when its animation ends: rand() % 100 and, outside action 1,
// action 1. In action 1, by the draw: below 80 action 1, below 85 0x9d and below 90 0x9f, with an even rand() and on
// map 2 Text 904; below 95 0xab and below 100 0xa5, with rand() % 3 at 0 and on map 2 Text 905. The second rand() is
// drawn on any map. The actions go through 0x4ea030 and are those of Player.bmd.
[[nodiscard]] Core::Expected<void, std::string> FinishTheTurnedNpc(Kernel::ActorState& actor, std::size_t slot,
                                                                   const Actors::CharacterStepContext& context);
} // namespace Client::Monsters
