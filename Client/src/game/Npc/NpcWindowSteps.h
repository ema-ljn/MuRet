#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"

#include <cstdint>
#include <string>

// The NPC windows' steps (CL9 milestone 6, phase 2): the shop (0x5ed960) with the repair-all cost (0x5b1570), the
// warehouse (0x5ea060), the chaos box (0x5fe610), selling the cursor's item (0x5deca7..0x5df448), the cursor item's two
// confirmations (0x5d8dee..0x5d9659) and the OK of the warehouse zen (0x6270ed..0x6282c8). Called by 0x5ee140
// (Hud::StepTheGameUi) and 0x623e70 (Hud::Dialogs::StepTheOpenDialog). Contract:
// Client/docs/research/04-protocolo.md, "Las ventanas de los NPC", and 06 § El inventario.
namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Ui
{
class PanelRegistry;
}

namespace Client::Kernel
{
class WorldState;
struct WorldRuntime;
} // namespace Client::Kernel

namespace Client::Hud
{
struct GameUiContext;
} // namespace Client::Hud

namespace Client::Npc
{

// 0x5e7aa0(mode): the warehouse keypad shuffled (Items::ShuffleTheKeypad, 40 rand()), what was typed (0x78ab9fc..) at
// 0 and the mode in 0x78abb20. Inline in dropping on the inventory with the lock (0x5cb3af..) and in the warehouse's
// button 2 (0x5ea1aa..); called by the OK of the 0x73 (0x627c33, with 1) and that of the 0x8f (0x628276, with 6).
void OpenTheKeypad(Kernel::WorldRuntime& runtime, Crt::CrtRandom& random, std::int32_t mode);

// 0x5ed960, with the shop open (IsOpen(0x10000), its rand()). With 0x78abb09: the two repair-mode buttons (x + 0x19 and
// x + 0x55, y + 0x16d), the repair-all one (x + 0x73), which sends C1 05 34 FF 00 in C3 without spending the click, and
// 0x5b1570. Always, the close button in the inventory's corner (x + 0x19, y + 0x18b), which closes the inventory.
[[nodiscard]] Core::Expected<void, std::string> StepTheNpcShop(const Hud::GameUiContext& context,
                                                               Ui::PanelRegistry& panels);

// 0x5b1570: 0x78ababc at 0 and, with the own record open (3848 rand() on closing it), the sum of 0x5b13e0 over
// 0x54d0f0 in mode 2 of each worn item (durability below the maximum, 0x5b1ba0) of the twelve equipment slots (with a
// type) and of the 64 inventory cells (with +0x3c), except what is not repaired (0x1a00 to 0x1a05, 0x1a0a, 0x1a1f,
// 0x807, 0x80f, from 0x1c00 on, 0x1807 to 0x1813, 0x1a0e to 0x1a14, 0x1c15 and 0x1a25).
[[nodiscard]] Core::Expected<void, std::string>
PriceTheRepairOfEverything(Kernel::WorldState& world, Crt::CrtRandom& random, const Tables::ItemTable* items);

// 0x5ea060, with the warehouse open: the first of the four buttons (x + 0x1a, 0x4c and 0x7e at y + 0x186, and x + 0xd7
// at y + 0x18b, of 0x18 × 0x18) under the mouse; with the click, it spends it and 0x19 sounds. 0 and 1: dialog 0x73 to
// deposit or withdraw zen (0x78abacc), the text fields at 0 and the numeric field; 2: the shuffled keypad and
// 0x78abb20 at 4, or 3 with the lock (0x6c932f); 3: close the warehouse.
[[nodiscard]] Core::Expected<void, std::string> StepTheWarehouse(const Hud::GameUiContext& context,
                                                                 Ui::PanelRegistry& panels);

// 0x5fe610, with the chaos box open: the mix button (x + 0x4b, y + 300, of 0x2c × 0x20) with 0x7d2dc80 at 0 uses the
// mix kind (0x7d2dc98) that 0x5fd4f0 builds and its price (0x7d2dca0) from the drawing; the close button in the
// inventory's corner closes the box.
[[nodiscard]] Core::Expected<void, std::string> StepTheChaosBox(const Hud::GameUiContext& context,
                                                                Ui::PanelRegistry& panels);

// 0x5d8dee..0x5d9c56, the 0x78abb14 warnings of the NPC windows with their answer (0x6c932e at 1 or 2) already written,
// from PersonalShop::StepTheWarningAnswers: with 1, the sale that dialog 0x98 waits for (at 1: the C1 04 33 in C3; at
// 2, 0x5be150); with 2, the mix (at 1: 0x7d2dc80 at 1 and the C1 04 86 in the clear; at 2, nothing); with 4, panel
// 0x2000's (0x5d9b82: at 1, the warehouse keypad shuffled at 6; at 2, 0x78abb08 at 0). In all, 0x78abb14 at 0 and
// 0x6c932e at 0xff first. Another warning cuts.
[[nodiscard]] Core::Expected<void, std::string> AnswerTheNpcWarning(const Hud::GameUiContext& context,
                                                                    std::int32_t kind);

// 0x5deca7..0x5df448, with the click outside the grids: with the shop open (its rand()) and the mouse on its grid
// ([x + 0xf, x + 0xc3) × [y + 0x32, y + 0x172)), what does not sell does nothing, the valuable opens dialog 0x98 with
// 0x78abb14 at 1, and the rest goes out with C1 04 33 in C3. Returns false if it goes on to the floor (0x5df449).
[[nodiscard]] Core::Expected<bool, std::string> SellTheCursorItem(const Hud::GameUiContext& context,
                                                                  Ui::PanelRegistry& panels);

// 0x6270ed..0x62820a, the OK of dialog 0x73 (with Enter, up to 100000000) without the trade open, from the 0x73's step
// (Hud/Dialogs/GameDialogSteps.cpp): the warehouse zen, with the own record open. Deposit (0x78abacc at 0) or
// withdraw, each with the 0x73's close and the C1 08 81 in the clear or the 0x74; withdrawing with the lock waits for
// the password (−1, 1, the zen and 0x77f3cc8) and opens the keypad (0x5e7aa0(1)).
[[nodiscard]] Core::Expected<void, std::string> MoveTheWarehouseMoney(const Hud::GameUiContext& context);
// 0x5e74f0: panel 0x2000, its acceptance checkbox, the confirmation and the two exits with C1 03 31.
[[nodiscard]] Core::Expected<void, std::string> StepThePanel2000(const Hud::GameUiContext& context,
                                                                 Ui::PanelRegistry& panels);
} // namespace Client::Npc
