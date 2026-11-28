#pragma once

#include "Core/Expected.h"

#include <string>

// The quest window (CL9 milestone 2; 06 § Las misiones, "La ventana"): the step (0x41b5a0, with 0x41a9e0 and 0x419bd0
// for the NPC dialog and 0x41b170 for the list) and the drawing (0x41ca40, with 0x41ba20 and 0x41c600), of the object
// [0x710148] (WorldRuntime::quest) with its window at +0x21383 (Ui::DelegatedPanels::quest: 1 the dialog, 2 the
// list). They send the C3 A2 (accept or finish the quest, with the C3's two rand()) and the C1 03 31 (0x419900, close
// it).
namespace Client::Ui
{
class PanelRegistry;
}

namespace Client::Hud
{
struct GameHudContext;
struct GameUiContext;
} // namespace Client::Hud

namespace Client::Quests
{

// 0x41b5a0, from 0x5ee140 (0x5ee20d), after the commands: the window's corner at +0x21390 and +0x21394, (0x104, 0)
// with the character open and (0x1c2, 0) otherwise. With the character open, the click on the quest button
// ([0x1f9, 0x211) × [0x18b, 0x1a3)) spends it, sets 0x77f3b10 to 0 and 0x6c8dcc to 6 and closes (Close(8)) or opens
// (Open(8), the list) the quests. Then, with the window at 1, 0x41a9e0; with 2, 0x41b170. The UI mouse is compared
// with the corner just set.
//
// 0x41a9e0: the mouse over the window (0xbe × 0x1b1) belongs to the UI; the answers (0x419bd0); with 0x419b10 (the
// current quest with its items), the click on the finish button ([x + 0x23, + 0x78) × [0x163, 0x17b)) sends the C3 A2
// with the quest and 1, spends the click, releases the button (0x7d36874) and sounds (0x1c); the click on the close
// button ([x + 0x19, + 0x18) × [y + 0x18b, + 0x18)) spends it and closes with 0x419900, without the registry's sound;
// and turns off the release (0x7d366ec).
//
// 0x419bd0: the click on the answers, which start at ((7 − answers − lines) · 0x12) / 2 + 0x42 + lines · 0x12 (at
// 0xfa with the state other than 1), 0x12 each, no more than 0x6a from x = 0x236 (Q-C871), spends it, releases the
// button and sets 0x77f3b10 to 0 and 0x6c8dcc to 6; the answer is (y − top) / 0x12 with C division, and the row from
// 1 to 17 pixels above the first is answer 0 (Q-C870). Its action in Dialog.bmd: 1 checks the conditions with the zen
// (0x418fb0) and, if they are not met, shows the dialog they left (0x419710); if they are, the A2; 2 closes with
// 0x419900; 3 sends the A2. With any of them it sounds (0x1c), and if the answer leads to a dialog (greater than 0)
// and the conditions did not fail, 0x419710 shows it.
//
// 0x41b170: the mouse over the window; the click on tab 0 ([x + 0x14, + 0x31) × [0xeb, 0xfb), or [0xed, 0xfb) if it
// is not the chosen one) chooses it (the C1 9F that follows is not reached, Q-C845); on tabs 1 and 2, nothing, and the
// click goes on. The click on the close button closes with Close(8), which sounds.
[[nodiscard]] Core::Expected<void, std::string> StepTheQuestWindow(const Hud::GameUiContext& context,
                                                                   Ui::PanelRegistry& panels);

// 0x41ca40, from 0x5f7440 (0x5f75db), after the window of 0x78abb00: with the window at 1, the NPC dialog (0x41ba20);
// with 2, the list (0x41c600). Then, with the character open, the quest button (0x103 with the window open, 0x102
// closed) of 24 × 24 at (505, y + 0x18b) and, with the mouse over it, its help: text 1140 and "(T)".
//
// 0x41ba20: the background with the line (0x5ee220(x, y, 1)); with state 1, the line at y + 325, the requested items
// with their name and how many (0x41b810(1, 1)), the finish button (gray if one is missing, darker with the click over
// it, which is spent on release) with text 699; with state 3, the zen it requires (text 198 and 0x5b1130 with the
// color of 0x5b10f0); the close button with its help (1002); the title, the name of the quest's NPC in the table at
// 0x7681eb8 (without it, nothing); the quest's name; and the dialog's text and answers (0x41b680(1)). With a class 3
// or 4 hero and 0x6eeda8, the NPC of quest 2 and without the quest's name.
//
// 0x41c600: the background with the line, the bottom frame, the three tabs (1140 to 1142; tabs 1 and 2 in red), the
// close button with its help (text 927 with "T") and the chosen tab: tab 0 (0x41c2a0) with "Quest", the name of the
// highest, the dialog's text without answers and the state (930 to 932); tabs 1 and 2 (0x41c420 and 0x41c4a0) are not
// reached (only tab 0 is chosen) and go as in the original.
[[nodiscard]] Core::Expected<void, std::string> DrawTheQuestWindow(Hud::GameHudContext& context,
                                                                   Ui::PanelRegistry& panels);

// 0x41b9e0, the first thing of the UI's 3D (0x5ac120, 0x5ac1c7): with the window at 1, the requested items of the
// current quest with state 1 (0x41b810(1, 0)), each with 0x5e1480 at (480, 235 + 25 · i) of 20 × 20 with the level
// << 3; with the list and +0x21380 (which nobody turns on), those of state 1 or 2 by +0x21381.
[[nodiscard]] Core::Expected<void, std::string> DrawTheQuestItems(Hud::GameHudContext& context);
} // namespace Client::Quests
