#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Movement
{
struct HeroMovementContext;
} // namespace Client::Movement

// Talking to an NPC (CL9 milestone 2, P-C178): MoveHero's click on the chosen NPC (0x59da75..0x59dc59, in
// GameMovement.cpp) takes the hero to its cell with +0x379 at 2, and the arrival (0x5650c0 with that 2,
// 0x5674c8..0x5695b3) sends it the C1 30 with its key. The GS reply says what each NPC opens (0x4d1e00, milestone 6);
// the Dialog.bmd dialog that leads from one reply to another is the 0x8d (GameDialogSteps.cpp).
namespace Client::Npc
{

// 0x5674c8..0x5695b3, case 2 of 0x5650c0: MouseUpdateTime's maximum at 12; outside an active Crywolf, the hero stopped
// (0x4f2250) and +0x378 at 0; with the NPC of 0x6c8e54 (+0x376 of 0xcd or more), the 0xee's level warning, the panel
// flags, the quest window closed, the C3 A0 if the quests lack the class, the 0x101's warning and the C3 30; in an
// active Crywolf, before the C3 30, the click on an altar (0x567b00..0x567c23, Events::Crywolf::ClickTheCrywolfAltar,
// CL9 milestone 11), which with an elf shows the altar's state instead of talking. At the end, 0x6eeda8 and 0x6c8e54
// at −1.
[[nodiscard]] Core::Expected<void, std::string> TalkToTheNpc(Kernel::ActorState& hero,
                                                             const Movement::HeroMovementContext& context);
} // namespace Client::Npc
