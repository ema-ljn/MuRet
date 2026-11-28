#pragma once

#include "Core/Expected.h"
#include "Protocol/Items/InventoryList.h"
#include "Actors/ActorActions.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// An F3 10 item in the own inventory: 0x5bc740 with the grid 0x78a9bb0, 8 columns, 8 rows and the last argument at
// 1, as 0x4c55a0 calls it (0x4c5e7f..0x4c5e8c), and 0x54b3a0's type. Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "El F3 10, el inventario".
namespace Client::Kernel
{
struct ItemState;
}

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Items
{

using InventoryItemBytes = std::span<const std::byte, Protocol::Items::InventoryItemDataSize>;

// 0x5bc75d: with this type 0x5bc740 returns without touching anything.
inline constexpr std::uint16_t NoInventoryItem = 0x1fff;
// 0x5bc776 and 0x5bcc11: the equipment slots, from 0 to 11, and from 76 those of the second grid.
inline constexpr std::uint8_t FirstInventoryGridSlot = 12;
inline constexpr std::uint8_t FirstSecondGridSlot = 0x4c;

// 0x54b3a0: byte 0, bit 0x80 of byte 3 as bit 8 and the upper four of byte 5 as bits 9 to 12. Bytes 1, 2, 4 and 6
// and the lower four of byte 5 do not count.
[[nodiscard]] std::uint16_t InventoryItemType(InventoryItemBytes bytes);

// 0x5bc740 with the slot and the seven bytes of an F3 10 entry. From 0 to 11, in the own record's equipment, open
// (inline, 0x5bc77f..0x5bc93c): the durability, D+0x1c, the type, D+0x1b, occupied, 0x54bf40 and 0x512a30 on the
// published hero; then the pets' part (slot 8), the raven (1) and the ring 0x1a14 (10 and 11), and the inline close,
// with 3848 rand() if left with no accesses. From 12 on, in a grid, with the width and height of the Item.bmd record,
// and then, with WorldRuntime::slot78a6758 under 12, the record open for 0x54fe10 if the WORDs +0x3e or +0x42 are not
// 0. What MuRet does not have stops with an error after what the original does before, and the record is closed
// anyway. Returns what 0x4f2250 asked for inside 0x512a30, if it ran.
[[nodiscard]] Core::Expected<Actors::ActorStopResult, std::string>
PlaceInventoryItem(std::uint8_t slot, InventoryItemBytes bytes, const Scenes::WorldEntryResources& resources);

// 0x5bc150(slot, data), from the 31 with a grid that is neither 3 nor 5 (0x4c6033): the item in the shop and
// warehouse grid (0x78a7070), 8 per row from the slot, unbounded, and 0x54bf40 in each cell of its footprint. It does
// not open the own record. With type 0x1fff, nothing.
[[nodiscard]] Core::Expected<void, std::string> PlaceInTheNpcGrid(std::span<Kernel::ItemState> cells, std::uint8_t slot,
                                                                  InventoryItemBytes bytes,
                                                                  const Scenes::WorldEntryResources& resources);

// 0x5bc740(grid, columns, rows, slot, item, …) with a grid other than the inventory's (0x5bc766 jumps to
// 0x5bd307..0x5bd3ec): the type's footprint from `origin`, row · `columns` + origin + column, with the origin's
// column (origin % 8) and row (origin / 8) in each cell, and 0x54bf40 with the upper 24 bits of cell · 9. With type
// 0x1fff it touches nothing. What follows in the original (0x5bd3f2: 0x5bc470 with 0x78a5a20 and, with the last
// argument at 0, the own record) belongs to the caller.
[[nodiscard]] Core::Expected<void, std::string>
PlaceItemInOtherGrid(std::span<Kernel::ItemState> cells, std::int32_t columns, std::int32_t origin,
                     InventoryItemBytes bytes, const Scenes::WorldEntryResources& resources, std::string_view where);

// The first cell of the footprint of `bytes`'s type from `origin`, in the order 0x5bc740 writes them (row ·
// `columns` + origin + column), that falls outside the grid's `cells` cells, without writing anything: the one the
// original would write past the grid (Q-C200). With type 0x1fff, or with the whole footprint inside, nothing.
[[nodiscard]] Core::Expected<std::optional<std::int32_t>, std::string>
FirstCellPastTheGrid(std::size_t cells, std::int32_t columns, std::int32_t origin, InventoryItemBytes bytes,
                     const Scenes::WorldEntryResources& resources, std::string_view where);

// FirstCellPastTheGrid with PlaceInventoryItem's slot: from 0 to 11 (the equipment), nothing; from 12 to 75, the 64
// cells of 0x78a9bb0 from the slot − 12; from 76 on, the 32 of 0x78ab0b8 from the slot − 76. Returns the warning,
// which names the grid and the cell.
[[nodiscard]] Core::Expected<std::optional<std::string>, std::string>
InventoryPlacementPastTheGrid(std::uint8_t slot, InventoryItemBytes bytes,
                              const Scenes::WorldEntryResources& resources);

// Q-C200: a packet whose footprint writes outside the grid is discarded before its effects. A provider error is not
// consumed: the normal path returns it at the original point.
[[nodiscard]] bool DiscardGridOverflow(std::size_t cells, std::int32_t columns, std::int32_t origin,
                                       InventoryItemBytes bytes, const Scenes::WorldEntryResources& resources,
                                       std::string_view where);
[[nodiscard]] bool DiscardInventoryOverflow(std::uint8_t slot, InventoryItemBytes bytes,
                                            const Scenes::WorldEntryResources& resources);

// 0x4225d0, the Dark Horse's defense, which 0x54bf40 calls at 0x54cc5c with the 0x1a04: option 0x96 with 5 + the own
// record's agility / 20 + 2 · the horse's level (WorldRuntime::pets), in one byte, computed with the record open
// (3848 rand() when closing it if left with no accesses). If it already has a 0x96 from the second option on, it
// rewrites its value; otherwise, or if it is the first (Q-C415), it appends it at the end.
void AddTheDarkHorseOption(Kernel::ItemState& item, const Scenes::WorldEntryResources& resources);
// The same with the own record's access count and the rand() given: the pets' tooltip (0x423b07) calls it with the
// record open around it (0x5f7bc0), or with an intermediate draw's copies. It does not check the type: the caller
// already knows it is the 0x1a04 (0x4225dc).
void AddTheDarkHorseOption(Kernel::ItemState& item, const Kernel::WorldState& world, std::uint8_t& ownRecordDepth,
                           Crt::CrtRandom& random);
// Where 0x4225d0 writes its 0x96: the place of a 0x96 from the second option on, or the one after the last (Q-C415).
[[nodiscard]] std::uint8_t DarkHorseOptionPlace(const Kernel::ItemState& item);
} // namespace Client::Items
