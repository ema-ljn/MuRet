#pragma once

#include "Core/Expected.h"

#include <string>

// The /move window (CL8 milestone 6; 06 § HUD y ventanas, "La ventana /move"): mode 4 of 0x77f3b08 (Ui::MovePanel),
// with the destinations of movereq.bmd (WorldRuntime::moves, the list at 0x723c68). *Static*.
namespace Client::Hud
{

// 0x5ecdc0 → 0x449850 → 0x448580, in the UI step (0x636e24) with the window open and nobody holding the mouse. The
// mouse over the window (0 < x ≤ width, 0 < y ≤ height) belongs to the UI (0x775857c). Each destination, in its order,
// opens and closes the own record (3848 rand() when closing it, Q-C183) and passes with the level (2/3 of the required
// one for classes 3 and 4, with ftol), the zen and the PK below 6; the first that passes with the left button down
// (0x7d36874) over its row says text 260, a space and its name like the Enter chat (Chat::SayInTheChat, inline at
// 0x4488d3..0x448ef8), closes the window and turns off the button and the edge (0x7d366d4). Then the bottom row, with
// the button, closes it too. Without the chat input (`context.chat`), the click on a destination stops.
[[nodiscard]] Core::Expected<void, std::string> StepTheMoveWindow(const Ui::GameUiContext& context);

// 0x5b10e0 → 0x449860 → 0x448fa0, from the overlays (0x5b0550): the black background at 80 %, the red bottom row, the
// title (933, bold) and the columns (934 to 936), and for each destination, with the own record opened and closed as
// in the step, the highlight under the mouse if it passes (with nobody holding the mouse), its name, the level and the
// zen in their colors; at the end, 1002 in the bottom row. Without Text.bmd, it stops.
[[nodiscard]] Core::Expected<void, std::string> DrawTheMoveWindow(Ui::GameHudContext& context);
} // namespace Client::Hud

namespace Client::Ui
{
struct GameHudContext;
struct GameUiContext;
} // namespace Client::Ui
