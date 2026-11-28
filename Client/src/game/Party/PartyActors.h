#pragma once

#include "Social/SocialState.h"

#include <cstddef>
#include <span>

// The party members in the viewport (CL9 milestone 7, phase 2): the +0x1c and +0x20 of each slot of 0x77f3cd0,
// written by the frame's picking (0x59e700 and 0x59e3b0) and the right-hand list (0x5e4140), and read by the HUD bars
// (0x5a89b0) and the drawn list (0x5f130a). Contract: 06 § Lo social en pantalla. *Static*.
namespace Client::Ui
{
class PanelRegistry;
}

namespace Client::Kernel
{
class WorldState;
struct ActorState;
struct WorldRuntime;
} // namespace Client::Kernel

namespace Client::Hud
{
struct FramePickingContext;
} // namespace Client::Hud

namespace Client::Party
{

// The slots of 0x77f3cd0 up to the count (0x78aba48, signed: none with 0 or less).
[[nodiscard]] std::span<const Social::PartySlot> PartyMembers(const Kernel::WorldRuntime& runtime);

// 0x59e680(character), and the same inline loop of Teleport Ally (0x5926ca..0x592766): with a party (0x78aba48 above
// 0, signed), whether some slot of 0x77f3cd0 (every 0x24, up to the count) gives 0 in strncmp(slot, +0x1d9, n), with n
// the name's length or 1 if empty: the slot starts with the name (with the empty name, the empty slot). No rand().
[[nodiscard]] bool IsInTheParty(const Kernel::WorldState& world, const Kernel::ActorState& actor);
// The same over the slots PartyMembers gives (the effects' drawing, 0x50f9ee).
[[nodiscard]] bool IsInTheParty(std::span<const Social::PartySlot> members, const Kernel::ActorState& actor);

// 0x59e890..0x59e948 (in 0x59e700) and 0x59e439..0x59e4ed (in 0x59e3b0), with character `index`, which the caller
// already chose: the first party slot (up to 0x78aba48) with +0x1c at −2 whose name is its own (strncmp with the
// longer of the two lengths, at least 1: the texts' equality) takes `index` and its +0x78; each slot at −2 it compares
// first sets +0x20 to 0.
void MatchThePartyMembers(Kernel::WorldState& world, std::size_t index);

// 0x59ea9b..0x59eb66 (at the end of 0x59e700) and 0x59e50f..0x59e5da (of 0x59e3b0): each slot with a negative +0x1c
// sets +0x20 to 0 and +0x1c to −3 if its name is the published hero's (0x743a170) and to −1 otherwise.
void MarkTheMissingMembers(Kernel::WorldState& world);

// 0x59e3b0, from 0x59f52a: the first 400 of type 0x20a, players (+0x88 at 1), alive, in view (+0x16c), with alpha
// above 0 and not dying (+0x389), each with MatchThePartyMembers; then MarkTheMissingMembers. No rand().
void FindThePartyInTheViewport(Kernel::WorldState& world);

// 0x5e4140, from 0x59f48d: with the party closed (IsOpen(4)), the world's right-hand list. The world's width
// (0x5bb8d0, with its queries) minus 50 is its x and each row is 19 high from y 5. A slot with a character (+0x1c at 0
// or more) takes its row and, with the Dark Wizard or the Elf (+0x1d4 & 7 at 0 or 2), opens the own record to read its
// slot's skill (+0x425; the close spends 0x40b4d0's 3848 rand() if it leaves the count at 0): with 0xf, 0x10 or 0x1a
// to 0x1c and the mouse in [x − 3, x + 50] × [y, y + 19], the chosen one (0x6c8e30) is that character and it returns
// true. One at −1 also takes its row; −2 and −3 do not. All end at −2. The last one that hits wins.
[[nodiscard]] bool PickFromThePartyList(const Hud::FramePickingContext& context, Ui::PanelRegistry& panels);
} // namespace Client::Party
