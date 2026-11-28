#pragma once

#include "Core/Expected.h"

#include <string>

// The NPC windows' drawing (CL9 milestone 6, phase 3): the shop (0x5f4720) and the warehouse (0x5f4e30), in 640 ×
// 480, with the GL state they leave and the text globals in HudState. The 3D items and their grid's numbers come
// later, in 0x5f7610 and 0x5f7840 (Hud/GameInventoryHud.h), from the corner this drawing leaves. The warehouse
// password box is in Warehouse/WarehouseKeypad.h. Contract: 06 § "Los dibujos de las ventanas de los NPC".
namespace Client::Ui
{
class PanelRegistry;
}

namespace Client::Hud
{
struct GameHudContext;
} // namespace Client::Hud

namespace Client::Npc
{

// 0x5f4720, from 0x5f7440 (0x5f75ad): with the shop open (0x10000, the protected byte: 1 rand()), the corner at
// (0x104, 0) (0x78aba28 and 0x78aba2c), the background (0x5ee220), the cells of the 8 × 15 grid (0x5e3660), the title
// (text 230) and the tax (text 1623 with 0x78aba30) in bold, the buy and sell buttons (0x11a..0x11d, with 0x78abb27)
// and, with 0x78abb09, repair (0x11e or 0x11f with 0x78abb0c) and repair all (0x11e in light blue, which with the
// click pending spends it and is drawn pressed), each with its help (231, 232, 233 and 237), and the repair-all price
// (text 239 and 0x78ababc with commas, in its amount's color). Leaves the bold font and white.
[[nodiscard]] Core::Expected<void, std::string> DrawTheNpcShop(Hud::GameHudContext& context, Ui::PanelRegistry& panels);

// 0x5f4e30, from 0x5f7440 (0x5f75c6): with the warehouse open (0x40, no rand()), the corner at (0x104, 0), the
// background, the grid cells, the title "text 234 (text 240)" (241 when locked by 0x6c932f, which also tints it
// yellow), the stored zen (+0xed0 of the own record, with text 224), text 266 in red with the fee (level² · 0.04 plus
// 2 · level when locked, rounded), and deposit, withdraw and the lock (0x110, 0x110 and 0x126, or 0x128 locked;
// pressed with the left button down over them, and the lock also with the keypad open), each with its help (235, 236
// and 242). Opens and closes the own record twice (3848 rand() each with the depth at 0). Leaves the normal or bold
// font and white.
[[nodiscard]] Core::Expected<void, std::string> DrawTheWarehouse(Hud::GameHudContext& context,
                                                                 Ui::PanelRegistry& panels);
// 0x5f7180: the eight instructions, the checkbox and the buttons of panel 0x2000.
[[nodiscard]] Core::Expected<void, std::string> DrawThePanel2000(Hud::GameHudContext& context,
                                                                 Ui::PanelRegistry& panels);
} // namespace Client::Npc
