#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Actors/FullSetScan.h"
#include "Player/OwnStatHelpers.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Client::Tables
{
struct ItemSetTables;
}

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
struct ItemState;
}

namespace Client::Kernel
{
struct ActorState;
struct OwnCharacterRecord;
} // namespace Client::Kernel

// The own record's stats: 0x550d10, which 0x512a30 calls at 0x512ef5 (thiscall on [0x7681eb4]). Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "Los stats del registro propio", and 07, Q-C184, Q-C185 and Q-C187.
//
// RecalculateOwnStats runs everything read, in order: 0x40e1b0 (Player/OwnItemSets.h), the clearing of [0x6eed3c] +
// 0x937c, 0x54ef00, the twelve places, 0x40da20 five times, the Cape of Lord branch, the eight helpers of
// 0x551287..0x5512ba (Player/OwnStatHelpers.h) and the tail (UpdateOwnDerivedStatsTail).
namespace Client::Player
{
struct ItemSetOptionList;
struct ItemSetState;

// What 0x550d10, and 0x40e1b0 from it, read and write outside the own record. The caller lends it for the call.
struct OwnStatsContext
{
    // [0x6eed3c]'s tables that 0x40c600 loads (ItemSetType.bmd and ItemSetOption.bmd) and what 0x40e1b0, 0x40d010 and
    // 0x40da20 write in [0x6eed3c] and lasts from one call to the next (WorldState::ItemSets). With either at nullptr,
    // 0x40e1b0 stops with an error before touching anything.
    const Tables::ItemSetTables* itemSetTables = nullptr;
    ItemSetState* itemSets = nullptr;
    // *[0x7758550], Item.bmd: 0x40dc50, from 0x40de10 and from 0x550d10's twelve places. nullptr if not loaded: it
    // stops with an error at the first place that reads it.
    const Tables::ItemTable* items = nullptr;
    // DAT_0743a170, the published hero: 0x40d010 reads its class (+0x1d4) and writes its +0x358.
    Kernel::ActorState& publishedHero;
    // The own record's access count (Models::LogicalAccess): 0x40e1b0 and 0x40de10 open and close it at each place,
    // and the Cape of Lord branch once. Each closing that leaves it at 0 draws 3,848 rand() calls from `random`.
    std::uint8_t& ownRecordAccesses;
    Crt::CrtRandom& random;
    // 0x7879f80's item and its conditions, for 0x54efe0, 0x54f9b0 and 0x54fe10 (HeldItemOf). By default it replaces
    // nothing, like [0x7879fbc] in the .bss (0 at start). Picking up an item (the 0x48-byte copies to 0x7879f80, CL8
    // milestone 4) and the incoming 24 and F3 14 (0x4d1c89 and 0x4d29cb) write it.
    OwnEquipmentOverride held{};
    // [0x743a178] and [0x743a17c], which 0x550990 reads: what the last 0x505820 left (WorldRuntime::fullSetScan).
    // From 0x512a30 with the published hero, that same call's; otherwise the one of the last original frame that drew
    // the characters (0x50ba30, at 0x50c91e): an in-between drawing does not change it. nullopt if MuRet does not know
    // what it left: 0x550990 stops with an error.
    std::optional<Actors::Scan505820> scan505820 = std::nullopt;
};

// 0x550d10 on `own`, whole: 0x40e1b0, the clearing, 0x54ef00, the twelve places, 0x40da20 five times, Cape of Lord,
// the eight helpers of 0x551287..0x5512ba (Player/OwnStatHelpers.h) in their order and the tail
// (UpdateOwnDerivedStatsTail). A step's error stops there, with the earlier steps done and the accesses closed. Without
// `scan505820` it stops at 0x550990, with the five helpers before it done.
[[nodiscard]] Core::Expected<void, std::string> RecalculateOwnStats(Kernel::OwnCharacterRecord& own,
                                                                    const OwnStatsContext& context);

// 0x54ef00 (0x550d44): the WORDs +0x2a, +0x2c, +0x2e, +0x30, +0x36, +0x32 and +0x34 at 0.
void ClearOwnItemStatWords(Kernel::OwnCharacterRecord& own);

// What 0x40dc50 gives for an item.
struct ItemStatBonus
{
    // Byte +0x44 of its type's Item.bmd record, which it returns.
    std::uint8_t kind = 0;
    // (D+0x1c >> 2) & 3, the WORD it writes in its second argument.
    std::uint16_t value = 0;
};

// The WORD 0x40dc6e writes, independent of the Item.bmd selector it reads afterwards.
[[nodiscard]] std::uint16_t ItemStatBonusValueOf(const Kernel::ItemState& item);

// 0x40dc50. It indexes Item.bmd with the signed type: with one outside the table (−1 in an empty place), the original
// reads memory that is not the table's; MuRet returns an error (Q-C184).
[[nodiscard]] Core::Expected<ItemStatBonus, std::string> ItemStatBonusOf(const Kernel::ItemState& item,
                                                                         const Tables::ItemTable& items);

// 0x550d49..0x550db1: for each of the twelve places with a durability other than 0, 0x40dc50, and with type 1, 2, 3 or
// 4, 5 · value to +0x2a, +0x2c, +0x30 or +0x2e (16 bits, wraps). `items` is *[0x7758550], nullptr if not loaded. An
// IDffff with value 0 does not depend on the selector: the four sums would be 0. If it stops at another place, the
// earlier ones stay added.
[[nodiscard]] Core::Expected<void, std::string> AddOwnItemStatBonuses(Kernel::OwnCharacterRecord& own,
                                                                      const Tables::ItemTable* items);

// 0x550dc2..0x550e12: 0x40da20 (0x40d690's code) on `list` with +0x2a and 0x8c, +0x2c and 0x8d, +0x30 and 0x8e, +0x2e
// and 0x8f, and +0x36 and 0x90, in that order. If it stops at one, the earlier ones stay added.
[[nodiscard]] Core::Expected<void, std::string> AddOwnItemSetOptionValues(Kernel::OwnCharacterRecord& own,
                                                                          ItemSetOptionList& list);

// 0x550e17..0x551284: with Cape of Lord (0x1a1e, Item.bmd's 13 30) in the wings' place, opens the record, adds to
// +0x36 the values of its 0x90 options (0x54e540, Items::AddOwnItemOptionValues) and closes it. The closing is
// Models::LogicalAccess's on `ownRecordAccesses`, with the 3,848 rand() calls if it leaves the count at 0. With another
// type it opens and touches nothing. With more than nine options the original reads the next place, which MuRet does
// not project: an error, with the access closed.
[[nodiscard]] Core::Expected<void, std::string>
AddCapeOfLordOptions(Kernel::OwnCharacterRecord& own, std::uint8_t& ownRecordAccesses, Crt::CrtRandom& random);

// 0x5512bf..0x5513b1, the tail: 16-bit subtractions that wrap, a rand() in a range with IDIV, two caps of 100 and two
// rand() % 100 that decide +0xf02 and +0xf03. With +0xee2 = +0xee4 + 1 the IDIV divides by 0 (Q-C185): an error, with
// +0xef8, +0xefa and the first rand() already done, as in the original.
[[nodiscard]] Core::Expected<void, std::string> UpdateOwnDerivedStatsTail(Kernel::OwnCharacterRecord& own,
                                                                          Crt::CrtRandom& random);
} // namespace Client::Player
