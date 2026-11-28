#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// The warehouse password box (CL9 milestone 6, phase 3): the keypad of 0x78abb20, opened by 0x5e7aa0
// (Npc::OpenTheKeypad), dropping into the locked inventory and the warehouse's lock button. Its step (0x5e7bf0, with
// the 83 and the 99), its drawing (0x5e9b10, HUD step 30) and what is under the mouse (0x5e7b50). Contract: 06 §
// "Los dibujos de las ventanas de los NPC" and 04 § "Las ventanas de los NPC".
namespace Client::Ui
{
class PanelRegistry;
}

namespace Client::Hud
{
struct GameHudContext;
struct GameUiContext;
} // namespace Client::Hud

namespace Client::Warehouse
{

// 0x7d3a0a4: the personal number's digits that modes 2, 3 and 6 ask for. Only the static initializer 0x622ac0 writes
// it, with the .data 7 (0x6c76c0); the other modes ask for 4.
inline constexpr std::int32_t PersonalNumberDigits = 7;
inline constexpr std::int32_t PasswordDigits = 4;

// What 0x5e7b50 returns: keys 0 to 9 are the shuffled keypad's, 10 erases, 11 is the OK and 12 cancel.
inline constexpr std::int32_t NoKeypadKey = -1;
inline constexpr std::int32_t KeypadDigitKeys = 10;
inline constexpr std::int32_t KeypadEraseKey = 10;
inline constexpr std::int32_t KeypadOkKey = 11;
inline constexpr std::int32_t KeypadCancelKey = 12;

// 0x5e7b50: the key under the mouse (0x7d3682c and 0x7d36828, integers): the eleven of 32 × 32 at
// ((i % 5) · 40 + 0xdf, (i / 5) · 40 + 0x96), and the OK and cancel of 0x46 × 0x15 at (0x109 + 0x4e · j, 0xeb), with
// the top and left edges inside. With none, −1. The key rows go every 40 from 0x96 and the drawing places them every
// 38 from 0x9a (07, Q-C923).
[[nodiscard]] std::int32_t KeypadKeyAt(std::int32_t mouseX, std::int32_t mouseY);

// 0x5e7bf0, first in 0x5ee140: with the mode at 0, false and nothing else. Otherwise true: 0x5ee140 does not go on.
// With the click and a key under the mouse, sound 0x19 and the key: a digit of 0x7879fc8 is appended to the typed ones
// if they did not reach the mode's digits, erase removes the last, cancel sets the mode to 0 and the OK, with all the
// digits: four equal ones clear the typed digits and open the 0x87; otherwise the mode at 0 and, by the mode it had
// (the table at 0x5e9af0): 1 sends the 83 with 0, the atoi of the typed digits and the first ten bytes of the chat
// buffer (Q-C924); 2, the 83 with 1, 0x78abb24's password and the typed digits; 3, the 83 with 2, 0 and the typed
// digits (the three with the XOR32 and in C3); 4 stores the typed digits and reopens the keypad in 5 (its 40 rand());
// 5, if they match, stores their atoi in 0x78abb24 and goes on in 2 without shuffling, otherwise opens the 0x88; 6
// sends the C1 03 31 in the clear, closes everything (0x657a90) and sends the 99 with the typed digits (Q-C925). Then,
// with the click the key stays pressed (0x78abb3a), the released button releases it, and the click and the button down
// go to 0.
[[nodiscard]] Core::Expected<bool, std::string> StepTheKeypad(const Hud::GameUiContext& context,
                                                              Ui::PanelRegistry& panels);

// 0x5e9b10, HUD step 30: with the mode at 0, nothing. Otherwise the frame of 0xfc and 0xfb at (213, 100), the title
// (text 690 plus the mode − 1 between 0 and 4; 6 is 691) and the prompt (697 with modes 2, 3 and 6, 696 with 5,
// otherwise 695) centered on 0x140, the typed bar (0xfd, 52 wide, 13 more per digit after the fourth) with a "*" per
// byte of 0x78ab9fc, the eleven keys (with their number from 0x7879fc8, or erase), the hovered one in yellow, pressed
// with 0x78abb3a, and the OK and cancel. No rand(). Leaves white and the big bold font.
[[nodiscard]] Core::Expected<void, std::string> DrawTheKeypad(Hud::GameHudContext& context);
} // namespace Client::Warehouse
