#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>
#include <string_view>

// The own personal shop's window (CL9 milestone 7, phase 5; 04 § Las tiendas personales and 06 § Lo social en
// pantalla, "Las tiendas personales"): its step (0x5ea290), the 0x91 title and price dialog (0x5eba70), the 0x9c
// confirm box (0x4716e0, its step 0x471930 and its answer 0x623750) and the answers to the warnings that 0x5d8d50
// checks before the cursor item (0x5d8dee..0x5deac5). What they send goes out through
// InventoryServices::sendGamePacket, with each C3's two rand() before. GameServer 0.97 has no 3F: the oracle is the
// client's ASM (*static*).
namespace Client::Ui
{
class PanelRegistry;
struct DialogState;
} // namespace Client::Ui

namespace Client::Kernel
{
struct WorldRuntime;
} // namespace Client::Kernel

namespace Client::Hud
{
struct GameUiContext;
} // namespace Client::Hud

namespace Client::Items
{
struct InventoryMoveContext;
} // namespace Client::Items

namespace Client::Social
{
struct ConfirmBoxState;
} // namespace Client::Social

namespace Client::PersonalShop
{

// 0x5eba70: the 0x91 dialog's two modes, kept in 0x78aba60.
inline constexpr std::int32_t ShopTitleMode = 1;
inline constexpr std::int32_t ShopPriceMode = 2;
// 0x5ea9c8..0x5eaa1c: the 0x9c open-shop box, which answers 0x623750 with the 1000.
inline constexpr std::int32_t OpenShopConfirm = 1000;

// 0x5eba70(mode): with 1 or 2, dialog 0x91 (0x6236f0), 0x78aba60 at the mode, the chat field closed (0x6c8e68), the
// zen mode (0x77f3b6a) at 0 with the title and at 1 with the price, the text fields emptied (0x553570(0), with the EDIT
// of [0x5763504]) and the field length (0x6c8e78) at 0x1c or 8. With 0x6c7b8c at 1 (the .data) it also builds that
// EDIT (ChatInputState::dialogEdit): in view, +0x4c at 0xc or 9 (digits only), the background at 0, the cap of 0x1c or
// 8, the size of 0x85 or 0x3c by 10 and the position at (0xfd or 0x125, 0x8a). With another mode, nothing. No rand().
void OpenTheShopDialog(Kernel::WorldRuntime& runtime, std::int32_t mode);

// 0x6260f6..0x6262e8, the 0x91's step: the OK (x from 0xff to 0x133, y from 0xa0 to 0xb0, with the edges) with the
// click spends it, reads the EDIT into the message buffer and, with the title, keeps it in 0x78aba64 if 0x5ebbd0
// allows it (otherwise, text 1130 of type 1), or with the price keeps its atoi in 0x77f3b6c; the cancel (x from 0x14a
// to 0x17e) with the remaining click spends it. Enter (0x5763588, which it turns off) counts as the OK even if the
// cancel was pressed, without reading the EDIT; without Enter or a button, nothing. Then, 0x6c932e at 1 with the OK and
// a non-empty title or a price above 0 (signed), and at 2 otherwise; the text fields emptied (0x553570(0)), the chat
// field closed, the focus to the game's window and the pending dialog open. No rand().
[[nodiscard]] Core::Expected<void, std::string> StepTheShopDialog(const Hud::GameUiContext& context);

// 0x4716e0(id, text, type) on 0x575d9d0: with the 0x9c open (0x7d3a10c), false. Otherwise, the 0x9c through 0x6236f0
// (with another open, waiting) and, with the type outside 1..2, false with the dialog already requested. Otherwise,
// the id, the type and the rows again: the text in pieces cut when they pass 24 bytes (those of 0x80 or more count 2,
// without looking at the next one) or at the exact end; the piece that ends with a byte of 0x80 or more past the end
// is lost (Q-C966). true.
[[nodiscard]] bool OpenTheConfirmBox(Social::ConfirmBoxState& box, Ui::DialogState& dialogs, std::int32_t id,
                                     std::string_view text, std::int32_t type);

// 0x471930, the 0x9c's step (0x6260d1): with the 0x9c open and the click, the buttons in y from h + 140 to h + 156 with
// the edges (h: the rows times the letter step (0x7758568) + 2, compared in the x87). With two buttons, the one with x
// from 0xff to 0x133 answers 1 and the one from 0x14a to 0x17e answers 2; with one, the one from 0x127 to 0x15b answers
// 1. Each spends the click and calls 0x623750 with the box's id.
[[nodiscard]] Core::Expected<void, std::string> StepTheConfirmBox(const Hud::GameUiContext& context);

// 0x623750(id, answer): with the 1000 and the 1, the C1 28 3F 02 with the title (0x78aba64) in C3, all panels closed
// (0x657a90) and the pending dialog open (0x7d3a110 to 0x7d3a10c); with the 1000 and the 2, only the latter. With
// another id or answer, nothing. It is not the title dialog (that is 0x5eba70's 0x91).
[[nodiscard]] Core::Expected<void, std::string> AnswerTheConfirmBox(const Hud::GameUiContext& context, std::int32_t id,
                                                                    std::int32_t answer);

// 0x5ea290 (from 0x5ee1c5): with the own shop or the other's open (0x800 and 0x20000, no rand()) and 0x6c931c at 1:
// - the answered title (0x78aba60 at 1 with 0x6c932e at 1 or 2) leaves both at 0 and 0xff;
// - the window's corner at (0x104, 0) (0x78aba58 and 0x78aba5c);
// - the open button (x from 0x122 to 0x15a and y from 0x18c to 0x1a4, without the right and bottom edges, with the
//   click, which it spends): each item of grid 0x78ab0b8 (signed type of 0 or more) needs a price above 0 in the mode's
//   map (0x5f8110 and 0x5f80c0, at its slot column + row · 8 + 0x4c), and there must be some and a title; otherwise,
//   warning 1119 with type 2. With the shop open (0x78aba50), the C1 28 3F 02 with the title in C3 and the inventory
//   closed (0x65cae0(0x4000000)); closed, the 0x9c box with the 1000, text 1131 and two buttons;
// - the close one (x from x + 0x69 to x + 0xa1, the same y, with the click, which it spends): with the shop open, the
//   C1 04 3F 03 in C3;
// - the title one (x from x + 0x14 to x + 0xaa and y from y + 0x41 to y + 0x59, with the click, which it does not
//   spend): 0x5eba70(1).
[[nodiscard]] Core::Expected<void, std::string> StepThePersonalShopWindow(const Hud::GameUiContext& context,
                                                                          Ui::PanelRegistry& panels);

// 0x5d8dee..0x5dc798 and 0x5deaa7..0x5deac0: what 0x5d8d50 does before the cursor item, already without the C3 24
// lock. With 0x78abb14 from 1 to 6 (in that order) or, otherwise, with 0x78aba60 at 2, the answer of 0x6c932e: with 1
// or 2 it handles and clears it; with another the step ends without doing anything else (the warning keeps waiting).
// Returns whether the step continues with the cursor item (0x5deac5):
// - 1, 2 and 4 belong to the NPC windows (Npc::AnswerTheNpcWarning, CL9 milestone 6): the valuable sale, the mix and
//   the warehouse keypad;
// - 3, the trade OK warning (0x5ecf67): with yes, the own OK flips, 0x78abaa5 at 1 and the C1 04 3C with the new OK in
//   C3; with no, it is cleared;
// - 5, the purchase in another's shop (0x5c5181): with yes and the owner's key (0x78a6514), the C1 11 3F 06 with the
//   key, the name and the slot [0x78a6758] in C3; with no, it is cleared;
// - 6, the price below the item's value (0x5dc7e9): with yes, the price (below); with no, 0x5eba70(2);
// - 0x78aba60 at 2, the price dialog: with yes, 0x78aba60 at 0 and the typed price (0x77f3b6c, unsigned) against the
//   value of 0x78a59c8's item (0x54d0f0 with mode 2): lower, the warning 0x633300(2, 1132, 0x99) and 0x78abb14 at 6;
//   otherwise, the price. With no, 0x78aba60 at 0 and 0x5be150.
// The price (0x5da5ac.. and 0x5dc807..): with the shop open, the C1 04 3F 03 in C3; with an item on the cursor, the
// C1 09 3F 01 with the slot [0x78a6758] and the price in C3, the 24 from grid 0 to 4 (with the lock released) and the
// price in the mode's map at 0x77f3cc8; without an item, the 3F 01 and the price in the map at [0x78a6758]. At the
// end 0x77f3b6c at 0.
[[nodiscard]] Core::Expected<bool, std::string> StepTheWarningAnswers(const Hud::GameUiContext& context,
                                                                      const Items::InventoryMoveContext& moves);
} // namespace Client::PersonalShop
