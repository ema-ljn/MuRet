#pragma once

#include "Kernel/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// The grids and globals of the NPC windows (CL9 milestone 6): the shop and the warehouse share the grid at 0x78a7070,
// the chaos box has its own at 0x7d2d380, and the 30 (0x4d1e00), the 31 (0x4c5f60), the 83 and the 86 (0x4da280)
// write the rest. The items are emptied on entering selection (0x5bbe10), keeping their other bytes. Contract:
// Client/docs/research/04-protocolo.md, "Las ventanas de los NPC".
namespace Client::Npc
{
// 0x78a7070..0x78a926c: 120 cells of 0x48 bytes, 8 per row (0x5bc150 and the return of 0x657ab0(0x40)).
inline constexpr std::size_t NpcGridCellCount = 120;
// 0x7d2d380..0x7d2dcbc: 32 cells, 8 per row and 4 rows (0x4c5f60 and 0x4da280 fill it with 0x5bc740(.., 8, 4, ..)).
inline constexpr std::size_t ChaosBoxCellCount = 32;
inline constexpr std::int32_t NpcGridColumns = 8;
inline constexpr std::int32_t ChaosBoxRows = 4;
// 0x6c9da8..0x6c9dbc: the six chaos box rates that the 30 copies with result 3 (0x4d253b..0x4d2566).
inline constexpr std::size_t ChaosRateCount = 6;
// 0x723c80..0x724580: the pet trainer's box, 32 cells of 0x48 bytes, 8 per row and 4 rows (0x44ad10, 0x44ad40 and
// 0x44d6c4 walk it with 8 and 4).
inline constexpr std::size_t TrainerBoxCellCount = 32;

// 0x449c40: the trainer box empty, the type at 0xffff and +0x3c at 0. Entering selection (0x5bbe10, 0x5bc0db) and
// opening the trainer (0x449940) leave it so: MuRet starts like that, because there is no game without going through
// selection first (*static*).
[[nodiscard]] constexpr std::array<Kernel::ItemState, TrainerBoxCellCount> EmptyTrainerBox()
{
    std::array<Kernel::ItemState, TrainerBoxCellCount> cells{};
    for (Kernel::ItemState& cell : cells)
    {
        cell.id = 0xffff;
        cell.occupied = 0;
    }
    return cells;
}

// The list at 0x6e5660, of the bundles that can be undone in the 0xa4 (the class of 0x46e290, vtable 0x6b85f8;
// ChaosMix/JewelBundleList): an old window (0x45a210) with a std::deque of 0xc-byte rows. The static initializer
// 0x401ef0 builds it (the third in the table at 0x6c6004, before WinMain) and nobody destroys or rebuilds it: what
// 0x482f80 does not empty (the queue, the place, the bar, the focus) lasts from one opening to the next. Its state
// (+0x48, 0x45a9b0) stays at 0: it has no border to grab (vtable + 0x8c is 0x403140) and the bar's thumb is never
// grabbed (07 Q-C1151), so the drag (+0x11c and the branches of 1 and 2 of 0x483bc0) is not here.
struct JewelBundleList
{
    // A row (0x46e940): +0 is flipped by the click on it (0x46f20a; nobody else reads it), +4 the inventory cell and +8
    // the bundle's level.
    struct Row
    {
        std::int32_t flag = 0;
        std::int32_t cell = 0;
        std::uint8_t level = 0;
    };
    // +0x80..+0xa8: the deque and its count. With +0xc8 at 0 (the constructor; nobody changes it in this class), the
    // count of 0x403100 is the deque's.
    std::vector<Row> rows{};
    // +0x40: the id of the counter 0x5762eb0 (0x459ef0). It is the first window built (the two initializers before it
    // build none): the 1 (*static*; 08 P-C366).
    std::int32_t id = 1;
    // +0x50..+0x5c: the bottom-left corner (mode 2 of +0x78) and the size. 0x45a210 and 0x46e290 leave it at (0, 0) of
    // 0xb4 × 0x6d; the 0xa4's drawing puts it at (0xe4, height + 0xf0) every frame (0x62f734).
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0xb4;
    std::int32_t height = 0x6d;
    // +0x8..+0x30: the window's message queue (0x459f70). Only its own reach this one, the 0xf and the 0x10 of the
    // keyboard arrows (0x483c21..0x483c84, without parameters), which the next step handles (0x45aa8e).
    std::vector<std::int32_t> messages{};
    // +0xc0: the keyboard arrows' repeat.
    std::int32_t keyRepeat = 0;
    // +0xc4: the chosen one, from 1 on (0: none).
    std::int32_t selection = 0;
    // +0x100: the first one shown (the bottom one) and +0x104 how many are shown (8 from the constructor; nobody
    // changes it).
    std::int32_t scroll = 0;
    std::int32_t shownRows = 8;
    // +0x108, +0x10c, +0x110 and +0x118: the bar that the drawing sets (0x484490) and the next frame's step reads;
    // +0x114, the thumb's width (13 from the constructor).
    float barTop = 0.0f;
    float barBottom = 0.0f;
    float thumbY = 0.0f;
    float thumbWidth = 13.0f;
    float thumbHeight = 0.0f;
    // +0x120 and +0x124: the repeat of the bar's arrows and of the track.
    std::int32_t arrowRepeat = 0;
    std::int32_t trackRepeat = 0;
    // +0x12c: the bar's mode (1, from the constructor).
    std::int32_t barMode = 1;
};

// The jewel mix of the NPC of the 30 with 9 (dialogs 0xa1, 0xa2, 0xa3, 0xa4 and 0xa6, and the BC; Text.bmd texts 1806
// to 1815, "misturar" and "dissolver"): bundle 10, 20 or 30 jewels of bless (0x1c0d) or soul (0x1c0e) into a bundle
// (0x181e or 0x181f), or undo one. The globals of 0x6e5794..0x6e57a0 and 0x6c62d8..0x6c62e1; those of 0x6c62xx start
// at 0xff in .data, those of 0x6e57xx at 0 in .bss. 0x402e10 returns them to those values (except 0x6e579c, which it
// leaves at −1).
struct JewelMixState
{
    // 0x6e5794: 0 bundle (the 0xa2) and 1 undo (the 0xa4). The 0xa1's buttons choose it.
    std::int32_t mode = 0;
    // 0x6e5799: why it cannot be done (2 jewels missing, 4 no bundle to undo, 5 another case; 0x402bc0).
    std::uint8_t refusal = 0;
    // 0x6e579a: the counted jewels (0x402bc0), or 1 when undoing.
    std::uint8_t counted = 0;
    // 0x6e579c: the price (0x402cf0): the count · 50000 when bundling and 1000000 when undoing.
    std::int32_t price = 0;
    // 0x6e57a0: 100 with the mix possible (0x402cb2), 0 or the type otherwise. Nothing ported reads it.
    std::uint8_t field6e57a0 = 0;
    // 0x6c62d8: the inventory cell of the bundle to undo (0x402a90); the BC 01 adds 12 to it (0x401f5a).
    std::int32_t cell = -1;
    // 0x6c62dc: the level of the bundle to undo (0x402a90, 0x402ae0).
    std::int32_t level = -1;
    // 0x6c62e0: the jewel, 0 bless and 1 soul; 0xff unchosen.
    std::uint8_t kind = 0xff;
    // 0x6c62e1: how many to bundle (10, 20 or 30; 0xff unchosen), signed for the BC 00 (0x40276b).
    std::uint8_t count = 0xff;
    // The list at 0x6e5660. It is not one of 0x402e10's globals: 0x402a10 (0x482f80) empties it and 0x402a20 fills it
    // from the 0xa4's step.
    JewelBundleList bundles{};
};

// 0x78ab9bc..0x78ab9c8: what the warehouse left waiting for the password, which the 83 with 0xc sends (0x4e4744..).
// With the first at −1, the zen (0x4e6f80 with the second and third); otherwise, an item (0x449d80 with all four).
struct PendingWarehouseMove
{
    std::int32_t slot = 0;
    std::int32_t second = 0;
    std::int32_t third = 0;
    std::int32_t fourth = 0;
};

// The SetCursorPos of 0x4d28a9..0x4d28f6, in screen pixels.
struct CursorWarp
{
    std::int32_t x = 0;
    std::int32_t y = 0;
};

// 0x7879fc8..0x7879fdc: the ten keys of the warehouse keypad, words with 0 to 9 that are shuffled on opening it.
inline constexpr std::size_t WarehouseKeyCount = 10;

struct NpcWindowState
{
    // 0x78a7070: the grid of the NPC shop and the warehouse.
    std::array<Kernel::ItemState, NpcGridCellCount> grid{};
    // 0x7d2d380: the chaos box grid.
    std::array<Kernel::ItemState, ChaosBoxCellCount> chaosBox{};
    // 0x6c9da8 (.data): 75, 70, 65, 60, 55 and 50 until the 30 with result 3 overwrites them.
    std::array<std::int32_t, ChaosRateCount> chaosRates{75, 70, 65, 60, 55, 50};
    // 0x7d2dc80: the mix state. 1 requests it (0x5d9362), 2 leaves it ready (0x4c5f60 and 0x4da280) and 0 turns it off
    // (opening the box, 0x65ac28, and the 86 with 2 or 0xb).
    std::int32_t chaosMixState = 0;
    // 0x7d2dc9d: opening the chaos box sets it to 0 (0x65a94f) and a siege packet writes it (0x4e5a28, CL9.5). With
    // something, the drawing adds "+ n%" to the chance of the 2 and the 8 (0x5ff2f2).
    std::uint8_t field7d2dc9d = 0;
    // 0x6c932f: the warehouse lock. The 83 with 0 and 1 writes it, and opening and closing the warehouse turn it off.
    std::uint8_t warehouseLocked = 0;
    // 0x78abb1c: the 83 with 0 and 1 sets it to 0, and the 83 with 0xc only sends with it at 0.
    std::uint8_t field78abb1c = 0;
    PendingWarehouseMove pendingMove{};
    // 0x5767950: the 32 returns it to 0 (0x4e3c58); with another value, the shop does not buy (0x5c4b91, 0x5c4bcc).
    // Buying sets it to the price of 0x54d0f0 in mode 0 (0x5c4bc9 and the send's exits).
    std::int32_t buyLock = 0;
    // 0x78ababc: the repair-all cost. 0x5b1570 computes it in each shop step with 0x78abb09 and 0x5f4720 draws it.
    std::int32_t repairAllPrice = 0;
    // 0x78aba30: the shop tax, in percent. The B2 1A with +4 at 2 writes it (0x4e5521, the byte at +5; CL9.5): until
    // then, the .bss 0. The shop title (0x5f4813) and the tooltip price (0x5b1203) read it.
    std::int32_t taxRate = 0;
    // 0x7879fc8: the warehouse keypad.
    std::array<std::uint16_t, WarehouseKeyCount> keypad{};
    // 0x78ab9fc..0x78aba06: what was typed, at 0 on shuffling (0x5cb43c..0x5cb44f and 0x5ea22d..). The password's 83
    // (0x5e7e90) sends the first ten.
    std::array<std::uint8_t, 11> typedNumber{};
    // 0x78aba07..0x78aba0a: the new password of mode 4, which the OK copies (a dword of what was typed, 0x5e7d8b) to
    // compare it in mode 5 (0x5e7e34). Nobody else reads or writes it.
    std::array<std::uint8_t, 4> newPassword{};
    // 0x78abb24: the password that mode 5 accepted (the atoi of what was typed, 0x5e7e56) and that the 2 sends in the
    // 83 with 1 (0x5e7ffc). Nobody else reads or writes it.
    std::uint16_t warehousePassword = 0;
    // 0x78abb3a: the pressed key of the password box. The click in the keypad step turns it on (0x5e9ab8) and releasing
    // it turns it off (0x5e9ac9); the drawing (0x5e9e25) lowers the number of the key under the mouse by one point.
    std::uint8_t keypadPressed = 0;
    // 0x7d2dc98: the mix kind that the C1 04 86 sends (0x5d93f8). 0x5fd4b0 builds it in each step with the box open
    // (0x5d8dda, ChaosMix::ScanTheChaosBox).
    std::int32_t mixKind = 0;
    // 0x7d2d378: what the box is worth (0x5fd120 from 0x5d8dcd), from which the drawing gets mix 1's chance.
    std::int32_t chaosBoxValue = 0;
    // 0x7d3a114 (.bss, 0): the box mode that dialog 0x8e chooses (0x62599b, button 0 or 1). 0x5fd4b0 looks at it and
    // the drawing sets its title.
    std::int32_t chaosMixMode = 0;
    // 0x6c9da0 and 0x6c9da4 (.data, 5 and 4): the largest width and height (Item.bmd +0x24 and +0x25) of what is in the
    // box, which 0x5fd4f0 returns to 1 before counting. They follow the rates (0x6c9da8): the 2's drawing reads them in
    // a row.
    std::int32_t mixWidth = 5;
    std::int32_t mixHeight = 4;
    // 0x7d2dc84: the 0x1c's cape, 3 with excellent weapons and 4 with armors (0x5fdbe6, 0x5fdc1d).
    std::int32_t mixCapeKind = 0;
    // 0x7d2dc88: what the items of level 4 or more with an option from 0x50 to 0x53 are worth (0x54d0f0 in mode 1,
    // capped at 1000000), the 0x1c's chance.
    std::int32_t mixExcellentValue = 0;
    // 0x7d2dc8c: the level + 1 of the two items of the 2 (0x1c11 and 0x1c12) or of the 8 (0x1a10 and 0x1a11).
    std::int32_t mixLevel = 0;
    // 0x7d2dc90: the wings' option (0x50 or 0x51 its value, 0x55 its value · 4). Nobody else reads it.
    std::int32_t mixWingOption = 0;
    // 0x7d2dc94: the chance of the 7's wings and the 0x18's cape, capped at 90.
    std::int32_t mixWingRate = 0;
    // 0x7d2dc9c: the luck (option 0x54) of the last item of level 9 to 12 or of level 4 or more that looks at it.
    std::uint8_t mixLuck = 0;
    // 0x7d2dca0: the mix price, which the drawing writes (0x5febf0) and the mix button reads (0x5fe982).
    std::uint32_t mixPrice = 0;
    // 0x78aba34: the chaos box tax, in percent. The B2 1A with +4 at 1 writes it (0x4e5507 and 0x4dfbb0, the byte at
    // +5; CL9.5): until then, the .bss 0. The box title (0x5fed3e), the price with tax (0x5b12f0) and the trainer
    // (0x44d89e) read it.
    std::int32_t chaosTaxRate = 0;
    // 0x6e5798: with 0, the 30 with 9 opens dialog 0xa1 (0x4d259d). It is the jewel mix sent (0x4029e0 sets it to 1
    // with the BC; 0x402e50 and 0x402e10 return it to 0): meanwhile, the 30 with 9 opens nothing.
    std::uint8_t field6e5798 = 0;
    // 0x723c80: the trainer box (PetTrainer/PetTrainer.h). The 31 with grid 5 (0x4c5fde), the 86 with 100 (0x4da373)
    // and the 24 with 5 (0x4d1d55) fill it; 0x44ad40 draws it and its items are picked up with 0x44ad10 and dropped
    // with 0x44aca9 (CursorGrid::TrainerBox).
    std::array<Kernel::ItemState, TrainerBoxCellCount> trainerBox = EmptyTrainerBox();
    // 0x724585: 0x44ae10 sets it to 1 if the zen is not enough for the mix it found in the box. Nobody reads it
    // (Q-C956).
    std::uint8_t trainerZenShort = 0;
    // The jewel mix (dialogs 0xa1..0xa6 and the BC, ChaosMix/JewelMix.h).
    JewelMixState jewels{};
    // The SetCursorPos that the last 30 requested (0x4d28f6), with the mouse the scene sees: NpcTalkCursorWarp.
    // Applying it to the window is the platform's job (CL10).
    bool cursorWarpRequested = false;
};
} // namespace Client::Npc
