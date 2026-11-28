#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// CL9 milestone 11, phase 4: the window of 0x78abb00 (06 § La ventana de los chips), which 94 (0x4dd8a0) opens with its
// type + 1 at the inventory's corner (0x1c2, 0): 1 the Rena and 2 the stone (item 0x1c15 of level 0 or 1, which the
// button registers with C1 05 95), 3 the serial number entry (an EDIT and C1 12 9D) and 4 the Muto number request
// (C1 04 98 01); the close button sends C1 03 97. All *static*: GS 0.97 serves 95, 97 and 98, but not 9D, which comes
// from the ASM. MuRet has 0x6c7b8c (.data) at 1: what the original does with it at 0 (the entry without an EDIT,
// 0x553ae0) is not reached.
namespace Client::Ui
{
class PanelRegistry;
}

namespace Client::Kernel
{
struct WorldRuntime;
} // namespace Client::Kernel

namespace Client::Ui
{
struct GameHudContext;
struct GameUiContext;
} // namespace Client::Ui

namespace Client::Events
{

// 0x78abb00: what 94 opens with the type + 1.
inline constexpr std::int32_t RenaChipWindow = 1;
inline constexpr std::int32_t StoneChipWindow = 2;
inline constexpr std::int32_t SerialNumberWindow = 3;
inline constexpr std::int32_t MutoNumberWindow = 4;

// 0x4dd8ce..0x4dd920, 94 with type 2: the 0x40 bytes of 9D (0x78ab078) zeroed, 0x553570(0), the chat field closed
// (0x6c8e68), the zen mode (0x77f3b6a), the typed zen (0x77f3b6c) and 0x78abacc at 0 (SocialState), the dialog field's
// length (0x6c8e78) at 0xc, Tab with a single field (0x6c8e6c at 1) and 0x77f3b6b at 1.
void OpenTheSerialEntry(Kernel::WorldRuntime& runtime);

// 0x56b6a9..0x56b72a, SendMove with 0x78abb00 at 3 after 97: the chosen field at 0, the buffers of 0x7798bf8 zeroed
// with their lengths except the whisper's (the second, 0x7798cf8), the EDITs of [0x5763504] and [0x5763508] emptied
// (vtable + 0x4c with 0), the chat closed and the zen mode, the typed zen and 0x78abacc at 0, with 0x77f3b6b at 0.
// Leaves the focus alone: the EDIT keeps it. MuRet does not have 0x6c8e74 (−1), the marks of 0x77f31c0 or the other
// eight buffers.
void CloseTheSerialEntry(Kernel::WorldRuntime& runtime);

// 0x5e6520, from 0x5ee1ec after the cursor item, with 0x78abb00 other than 0. The mouse over the window belongs to the
// UI (0x775857c). With the release (0x7d366ec) over the button: with 4, C1 04 98 01; with 1, 2 or another that is not
// 3, the slot of 0x1c15 of level 1 (with 2) or 0 and, if there is one, C1 05 95; with 3, without free slots
// (0x558740(−1, −1)) notice 896 and otherwise C1 12 9D with the EDIT's text, and the left button released (0x7d36874).
// Then the click (0x7d366d4) on the close button (0x19, 0x18b from the inventory's corner, 24 × 24): 97, 0x78abb00 at
// 0, the inventory closed (0x65cae0, with its rand()), 0x553570(0), the chat closed, the zen mode, the typed zen,
// 0x78abacc and 0x77f3b6b at 0 and the focus to the game window. At the end it clears the release. The packets go out
// through the step's send() (context.chat).
[[nodiscard]] Core::Expected<void, std::string> StepTheEventChipWindow(const Ui::GameUiContext& context,
                                                                       Ui::PanelRegistry& panels);

// 0x5f6550, from 0x5f75d0 after the event NPCs: the inventory's corner at (0x1c2, 0), the background, the title (the
// name of NPC 236 of NpcName(%s).txt, 0x54ebb0), its type's button (window 4's at y 220 with 903; the others, except 3,
// at 231 with 243 or 818, in white with the bold font if there are items and in gray if not), and with 3 the entry:
// texts 893, 894, 916 and 917, frame 0x10f and the EDIT of [0x5763504] reconfigured every frame (+0x4c at 2, its
// background at 0, the limit at 12, 150 × 14 at (495, 265), the focus and its drawing 0x468320) and button 895. With
// the button pressed (0x7d366d4) and the release (0x7d366ec), the drawing clears them with the left button (0x7d36874).
// Then the close button with its help (1002), each type's texts (700..703; 882..885; 891, 892, 897 and 898 with the 9D
// result in bold, blinking with the clock; 900, 706, 901 and 902), and except with 3, the labels (245 or 819 and 246 or
// 820) with "x%d" of the item count and of 0x78abb04 in the big font. No rand(). An intermediate drawing does not write
// the focus, the EDIT or the click.
[[nodiscard]] Core::Expected<void, std::string> DrawTheEventChipWindow(Ui::GameHudContext& context);

// 0x5f70a0, from the UI's 3D (0x5ac1da) with 0x78abb00 other than 0 and 3: the model of 0x1c15 (level 0, or 0xf in +4
// with 2 and 4; with 5, type 0) at Item.bmd's size, at (540, 298) except with 4 and at (540, 333), with the rand() of
// 0x5e1480. Then 0x5ac120 does not draw the grids, the cursor item or the quick slots.
[[nodiscard]] Core::Expected<void, std::string> DrawTheEventChipItems(Ui::GameHudContext& context);
} // namespace Client::Events
