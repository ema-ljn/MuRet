#pragma once

#include "Core/Expected.h"
#include "Npc/NpcWindowState.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Client::Ui
{
struct GameHudContext;
struct GameUiContext;
} // namespace Client::Ui

// The list of the bundles that can be unmixed, 0x6e5660 (CL9 milestone 6, phase 5; 06 § Los diálogos del juego, "El
// modo de la caja del caos y la mezcla de joyas"): the class of 0x46e290 (vtable 0x6b85f8), an old-window list with
// 0xc-byte rows. Its step (0x483bc0) is the chat log list's (0x479960, Chat/ChatLogWindow) with other functions in its
// vtable: it reads the keyboard arrows (+0xbc at 1), has no buttons or border, and each row chooses on click and asks
// on left double click. None takes rand(): the inventory is not in the protection table.
namespace Client::ChaosMix
{

// 0x402a10 → 0x482f80, vtable + 0x2c: empties the rows, sets their flags to 0 (+0x48, 0x483610, over none), the chosen
// one to 0 (+0x58) and the first visible one to 0.
void ClearTheBundles(Npc::JewelBundleList& list);

// 0x46e940, vtable + 0x94 (from 0x402a20): appends {0, cell, level} if the cell is in [0, 0x40] and not there yet; the
// level rejects nothing (07 Q-C954). Then the chosen one at 0, the first visible one shifted so the last row is in
// view, and the rows sorted by cell (0x46e4c0 with 0x46e4a0).
void AddTheBundle(Npc::JewelBundleList& list, std::int32_t cell, std::uint8_t level);

// 0x483aa0, vtable + 0x64: the chosen row, or none (the deque's end) without rows or without a chosen one. Without
// rows, sets the chosen one to 0.
[[nodiscard]] std::optional<Npc::JewelBundleList::Row> ChosenBundle(Npc::JewelBundleList& list);

struct BundleListStep
{
    // 0x46f297: the left double click's row that passes the checks of 0x46f27e..0x46f290, for 0x402a90(cell, level).
    // The original does it in the middle of the loop over the rows; the rest of the loop reads nothing 0x402a90 writes
    // (the dialog, the mix and the message area), so the caller asks for it on return.
    std::optional<Npc::JewelBundleList::Row> asked{};
};

// 0x402af0: 0x5762eb4 at the list's id, its step (0x45aa80(0): the queue with vtable + 0x28, 0x4834a0, the mouse over
// the list like the one below and vtable + 0x20, 0x483bc0) and 0x5762eb4 at 0. With the mouse grabbed by the list
// itself during the step, the bar's thumb is never grabbed (07 Q-C1151). The sounds (0x41ed10(0x19)) stay in
// WorldRuntime::lastSoundId (CL10).
[[nodiscard]] BundleListStep StepTheBundleList(const Ui::GameUiContext& context);

// 0x45a9d0(x, y) and 0x402b20 → 0x4832e0, vtable + 0x14: the bottom-left corner at (x, y), the frame with the arrows
// and the bar (0x46ebe0, which saves it for the step) and, from the first visible one, up to 8 rows (0x46f020) with the
// jewel's name (1806 or 1807, by 0x6c62e0) and the bundle's jewels, in the normal font. An intermediate draw does not
// write the list. Fails without the jewel's text.
[[nodiscard]] Core::Expected<void, std::string> DrawTheBundleList(Ui::GameHudContext& context, std::int32_t x,
                                                                  std::int32_t y);
} // namespace Client::ChaosMix
