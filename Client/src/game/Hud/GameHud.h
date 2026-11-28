#pragma once

#include "Core/Expected.h"
#include "Ui/GameHudContext.h"

#include <string>

// The game's HUD (06 § HUD y ventanas, CL8 milestone 2): 0x5a7320(1), which the game's draw calls at 0x637660, the
// overlays of 0x5b0550 (0x6376bb) and the game cursor 0x5ac2d0, called twice (0x6376d7 and 0x6376e1). In 640 × 480,
// with the GL state its primitives leave and the protection table's rand() in their order. What belongs to another
// wave stops with its address where its condition holds. The UI item models (grids, cursor and quick slots) go in the
// 3D pass of 0x5ac120 (Items/UiItemDrawing.h).
namespace Client::Hud
{

// 0x5a7320(1): the HUD's 31 children in their order (06 § HUD y ventanas, "El HUD hijo por hijo"). In the base state
// (no windows, no party, no event) it draws its map's sands or smoke, the bottom bar, the experience, the AG, the orbs,
// the current skill, the cells, the numbers and the quick slots' counts, and takes from rand() the closes of the
// arrows' own record, the twelve durability warnings, the bar, the experience, the AG and the main bar, the trade and
// shop queries and the hero's two cells.
[[nodiscard]] Core::Expected<void, std::string> DrawTheHud(Ui::GameHudContext& context);

// 0x5b0550: the overlays of mode 0x77f3b08 (Tooltips/GameHelps.h): F1's first help (1) and, with the mode left, the
// second (2) or the item's (3); with another, IsOpen(2) and the /move window (the 4, GameMoveWindow.h).
[[nodiscard]] Core::Expected<void, std::string> DrawTheOverlays(Ui::GameHudContext& context);

// 0x5ac2d0: the game cursor, with its choice table (06 § El cursor del juego). With a dialog (0x7d3a10c) or
// 0x78abb20, the usual one; otherwise the floor item's (5), the talk one (6, which animates its UVs with the clock),
// the object's, the attack one (4, or 0x68d in the 30), the commands' (0x65a), the repair one (7, spinning with mode
// 2), the press one (3, or 0xa with 0x77f3b5c) and the normal one (2). What it reads from another wave stops with its
// address.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGameCursor(Ui::GameHudContext& context);
} // namespace Client::Hud
