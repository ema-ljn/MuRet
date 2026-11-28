#pragma once

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Kernel/ItemState.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// The inventory in the game's HUD (06 § El inventario, CL8 milestone 4): the window (0x5f2340 with 0x5ee220,
// 0x5e2460, 0x5e3660 and the set label 0x40e720), the grids' 3D items (0x5f7610 → 0x5e2fc0 and 0x5e3760), the numbers
// (0x5f7840) and the cursor item (0x5f7b10). In 640 × 480, with the GL state they leave and the rand() of the
// protection table and of each item's draw in their order. The NPC windows' grids (shop, warehouse, chaos box and
// trainer's box) are CL9 milestone 6; the trade's and the personal shops' go with their window (0x5f3b40 and 0x5f3030).
//
// A UI item's model (0x5e1480 → 0x5e0720) goes in the 3D pass of 0x5ac120 (Items/UiItemDrawing.h).
namespace Client::Ui
{
class PanelRegistry;
}

namespace Client::Items
{
struct UiItemBox;
} // namespace Client::Items

namespace Client::Hud
{
struct GameHudContext;

// 0x5e1480(x, y, w, h, type, level, excellent, set, always): the item of `box` with the type and the level (+4, as
// signed 32 bits). With the context's models (GameHudContext::uiItems), it places it in front of the UI camera and
// draws it in its pass (Items/UiItemDrawing.h), with the rand() of 0x607b60, 0x609640 and 0x60b3d0's set pass; without
// them, it takes the same rand() without drawing. With some types and levels it draws nothing and takes none. The
// 0x1a18 stops before its joints (0x530860).
[[nodiscard]] Core::Expected<void, std::string> DrawUiItem(GameHudContext& context, const Items::UiItemBox& box,
                                                           std::int32_t type, std::uint32_t level,
                                                           std::uint8_t excellent, std::uint8_t set, bool always);

// 0x5e3660(x, y, grid, columns, rows): a 20 × 20 box per slot, in the warning color of whatever is there (empty ones
// too): Item_box (0x115) empty, InventoryBox2 (0x116) with a type. Used by the inventory and the NPC windows
// (Npc/NpcWindowDrawing.h and ChaosMix/ChaosBoxDrawing.h).
void DrawTheGridBoxes(Render::SpriteBatch& sprites, float x, float y, std::span<const Kernel::ItemState> cells,
                      std::size_t columns, std::size_t rows);
// Item.bmd +0x24 and +0x25 of the type: the cells wide and tall, each times 20 (0x6b86d0) on the x87 with the fstp,
// into the box's width and height (x and y at 0). Without Item.bmd or the type, the error with `where`.
[[nodiscard]] Core::Expected<Items::UiItemBox, std::string> ItemSizeOf(const GameHudContext& context, std::int32_t type,
                                                                       std::string_view where);

// 0x5f2340: with the inventory open, the window at (0x1c2, 0), or at (0x104, 0) with the character or the party open:
// the background, the equipment slots in their warning color, the 8 × 8 grid, the title, the zen, the close, repair and
// personal shop buttons with their help, and the set label. Takes 3848 rand() per close of the own record that leaves
// it with no accesses (the slots, the zen and the level) and one per trade and shop query.
[[nodiscard]] Core::Expected<void, std::string> DrawTheInventory(GameHudContext& context, Ui::PanelRegistry& panels);

// 0x5f3b40: with the trade open (IsOpen(0x20), its rand()), the window at (0x104, 0) (0x78a5a1c and 0x78a5a18): the
// background with the line (0x5ee220 with 1), the boxes of the two grids (0x5e3660: the other's at y + 70, the own at
// y + 270), the other's guild mark (nothing: the guild table is milestone 8), the title (226), the other's OK
// (accept_box 0x122 or 0x123, red with 0x78abac4 > 0), the zen (Money_01/02), own OK (red while waiting, 0x78abac8)
// and close buttons, with their help (227 to 229), both zens (0x10f, "%d,%03d" and the color by how much), the other's
// name, the bobbing arrow (9), their level by floor (368 and 369), the hero's name and the lines 370 (pulsing), 365,
// 366 and 367. No more rand().
[[nodiscard]] Core::Expected<void, std::string> DrawTheTradeWindow(GameHudContext& context, Ui::PanelRegistry& panels);

// 0x5f3030 (CL9 milestone 7): with the own personal shop or another's open (0x800 or 0x20000, no rand()), the window
// at (0x104, 0) (0x78aba58 and 0x78aba5c), in bold: the background (0x5ee220 with 0) and the title (1102); with the
// own (0x6c931c at 1), the 150-wide strip, green with the shop open (0x78aba50) or red, with 1103 or 560; the title box
// (Item_Money, 0x10f) with 0x78aba64's or, without it and with the own, 1105; the cells of grid 0x78ab0b8 (0x5e3660)
// at (x + 15, y + 110); with the own, the open or apply (1107 or 1106) and close (1108, grey with the shop closed)
// buttons (text_button, 0x659); the pulsing warning (370) and the lines of the own (1109, 1111, 1112, 1113 and 1115)
// or another's (1110, 1111, 1112, 1114 and 1116), the last two (1134 and 1135) pulsing.
[[nodiscard]] Core::Expected<void, std::string> DrawThePersonalShopWindow(GameHudContext& context,
                                                                          Ui::PanelRegistry& panels);

// 0x5f7610: the 3D items of the open grids, in their order. From the inventory, the equipment (0x5e2fc0, with the own
// record open around it, each slot in its box) and the grid (0x5e3760 with 0, at Item.bmd's size); the shop's (with
// its rand()) and the warehouse's, 8 × 15 from the NPC window's corner plus (15, 50), and the chaos box's, 8 × 4 from
// the same corner plus (15, 110); the others' stop with their address.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGridItems(GameHudContext& context, Ui::PanelRegistry& panels);

// 0x5f7840: the grids' numbers (0x5e3760 with 1): potions with more than 1 and the jewel bundles.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGridNumbers(GameHudContext& context, Ui::PanelRegistry& panels);

// 0x5f7b10: the cursor item (0x7879f80), at Item.bmd's size and centered on the mouse, with 0x5e1480's last argument
// at 1.
[[nodiscard]] Core::Expected<void, std::string> DrawTheCursorItem(GameHudContext& context);

// Which item 0x633110 draws (0x633116..0x63315d and 0x6331fc..0x633220): with the open dialog (0x7d3a10c) 0x94 or
// 0x98 and the warning (0x78abb14) at 1, the cursor's (0x7879f80: selling something valuable); with 0x96, 0xa8, 0x99,
// or 0x94 with warning 5 (which the selector turns into 0x96), the dialogs' (0x78a59c8). Otherwise, none.
enum class DialogItemSource : std::uint8_t
{
    None,
    Cursor,
    Dialog,
};
[[nodiscard]] DialogItemSource DialogItemOf(std::int32_t dialog, std::int32_t warningKind);

// 0x633110, between the frame's two cursors (0x6376dc; the call has no condition, it has it inside): DialogItemOf's
// item, without looking at 0x7879fbc, with its own pass (the projection of 1°, W / H, 20 and `farPlane`, this frame's
// world far at 0x6d5d44, and the view at identity) and 0x5e1480(245, 115, 40, 60, type, +4, D+0x1b, D+0x1c, 1): the
// box is fixed, not Item.bmd's. The rand() are 0x5e0720's. Without the UI resources, only the rand().
[[nodiscard]] Core::Expected<void, std::string> DrawTheDialogItem(GameHudContext& context, float farPlane);
} // namespace Client::Hud
