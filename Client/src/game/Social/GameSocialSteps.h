#pragma once

#include "Core/Expected.h"

#include <string>

// The UI steps of the trade and the duel (CL9 milestone 7; 04 § El comercio and § El duelo): the trade window
// (0x5ecde0, one of 0x5e4ce0's sisters) and the duel request (0x5ebca0, from 0x636ed6). What they send goes out
// through IGameUiRequests::SendGamePacket, with each C3's two rand() before.
namespace Client::Ui
{
class PanelRegistry;
}

namespace Client::Ui
{
struct GameUiContext;
} // namespace Client::Ui

namespace Client::Social
{

// 0x5ecde0: with the trade open (its IsOpen takes the protected byte's rand()), the three foot buttons, from the
// corner 0x78a5a1c/0x78a5a18, 0x18 × 0x18 at y + 0x186:
// - the zen (x + 0x1a): dialog 0x73, the text fields empty (0x553570(0)), length 8, a Tab field, the chat closed, the
//   zen mode at 1 and its value at 0, 0x78abacc at 2 and sound 0x19;
// - the OK (x + 0x61), with the wait 0x78abac8 (which counts down by one) at 0 and no item on the cursor: plays the
//   sound and, with the other's grid changed and the own OK off, the warning 0x633300(4, 0x173, 0x94); otherwise the
//   own OK flips, 0x78abaa5 at 1 and the C1 04 3C with the new OK, in C3;
// - cancel (x + 0x89): plays the sound and, with no item on the cursor, 0x78abaa5 at 0, the C1 03 3D in C3 and the
//   buttons (0x77f3b10 at 0 and 0x6c8dcc at 6).
// Each button spends the click (0x7d366d4).
[[nodiscard]] Core::Expected<void, std::string> StepTheTradeWindow(const Ui::GameUiContext& context,
                                                                   Ui::PanelRegistry& panels);

// 0x5ebca0: with the duel request (0x78abad9), the mouse over the box (0xd5..0x1aa × 0x6e..0x118) belongs to the UI;
// the click on yes (0xed..0x133) or no (0x14c..0x192), both at y 0xfa..0x10f, sends the C1 10 AC with the answer,
// 0x78abadc's key and 0x78abae0's name in C3 and lowers the request. It neither spends the click nor plays a sound.
[[nodiscard]] Core::Expected<void, std::string> StepTheDuelRequest(const Ui::GameUiContext& context);
} // namespace Client::Social
