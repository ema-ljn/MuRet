#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Kernel/ItemState.h"

#include <cstdint>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Actors
{
struct Scan505820;
} // namespace Client::Actors

namespace Client::Kernel
{
struct OwnCharacterRecord;
struct WorldRuntime;
} // namespace Client::Kernel

// The eight helpers 0x550d10 calls at 0x551287..0x5512ba, in that order, all thiscall on the own record, before its
// tail. Contract: Client/docs/research/06-render-escenas-y-ui.md, "Los ocho helpers de los stats propios", and 08,
// P-C38.
//
// Each one is a function, with its address. They write the record's WORDs +0x4c to +0x66 and +0xef4 (not the tail's
// +0xee2..+0xef2). Where the original reads something MuRet does not have, they return an error at that point, with
// what they wrote before, as the original up to there: Item.bmd not loaded or a type outside the table, [0x6eed3c]'s
// list not built (0x40e1b0) or an item with more than nine options. They assume the record is open, as from 0x512a30.
namespace Client::Player
{
struct ItemSetOptionList;

// 0x7879f80's item and what decides whether 0x54efe0, 0x54f9b0 and 0x54fe10 read it instead of an equipment place:
// with [0x7879fbc] > 0, signed (jle), and [0x78ab9b8] == 0x78a9bb0, place [0x78a6758], unsigned (ja with 11), if it is
// in that helper's list. What those globals are, unknown. By default it replaces nothing.
struct OwnEquipmentOverride
{
    std::int32_t dword7879fbc = 0;
    bool window78ab9b8Is78a9bb0 = false;
    std::uint32_t slot78a6758 = 0;
    Kernel::ItemState item7879f80{};
};

// WorldRuntime's four: cursorItemCount, cursorGrid on the inventory, slot78a6758 and cursorItem. The original reads
// them from the .bss each time; MuRet copies them when it builds the context.
[[nodiscard]] OwnEquipmentOverride HeldItemOf(const Kernel::WorldRuntime& runtime);

// 0x54ee00 (cdecl, returns a float): with max = Item.bmd's byte +0x2d, plus 1 for each of the first 4 levels
// ((scalar1 >> 3) & 15) and 2 for each of the others, plus 20 with flags1c % 4 at 1 or 2 or, otherwise, 15 with
// flags1b & 0x3f, and r = 1 − durability / max: 0.5 if r > 0.8f, 0.3f if r > 0.7f, 0.2f if r > 0.5f and 0 otherwise.
// The division and the subtraction in double (ADR 0012's PC53 profile). With max at 0 the x87, with the exceptions
// masked, gives infinity or NaN, and no comparison passes: 0.
[[nodiscard]] float ItemDurabilityFactor(std::uint8_t durability, std::uint8_t baseDurability, std::uint32_t scalar1,
                                         std::uint8_t flags1b, std::uint8_t flags1c);

// 0x54efe0 (0x551289): +0x50, +0x52, +0x54 and +0x56 by the class, the strength, the agility and the energy (each with
// its WORD of +0x2a, +0x2c or +0x30), the wings, the hands, place 9 and the rings, [0x6eed3c]'s list (0x40d690 and
// 0x40d7f0) and, with arrows or bolts with a level, a last adjustment. It replaces places 0, 1, 9, 10 and 11. `items`
// is *[0x7758550] and `setOptions` [0x6eed3c]'s list: nullptr if absent, and the error comes out where the original
// reads them.
[[nodiscard]] Core::Expected<void, std::string> UpdateOwnWords50To56(Kernel::OwnCharacterRecord& own,
                                                                     const OwnEquipmentOverride& held,
                                                                     const Tables::ItemTable* items,
                                                                     ItemSetOptionList* setOptions);

// 0x54f9b0 (0x551290): +0x5a and +0x5c by the energy (plus +0x30), the wings, the right hand, place 9, the rings and
// the list (0x40d690 on +0xef4, which it does not zero first, and 0x40d7f0). It replaces places 0, 9, 10 and 11.
[[nodiscard]] Core::Expected<void, std::string> UpdateOwnWords5aAnd5c(Kernel::OwnCharacterRecord& own,
                                                                      const OwnEquipmentOverride& held,
                                                                      const Tables::ItemTable* items,
                                                                      ItemSetOptionList* setOptions);

// 0x54fd40 (0x551297): +0x4e by the class, the level, the strength, the agility and, in class 4, the command (+0x20
// plus +0x36), and then 0x40d690.
[[nodiscard]] Core::Expected<void, std::string> UpdateOwnWord4e(Kernel::OwnCharacterRecord& own,
                                                                ItemSetOptionList* setOptions);

// 0x54fe10 (0x55129e): the keepalive's speeds, +0x4c and +0x58, by the class and the agility, the weapons, the 0x1a14
// rings (with bit 0x04 of +0x3c, which it sets or clears by opening and closing the record: Models::LogicalAccess on
// `ownRecordAccesses`, with 3,848 rand() calls if the closing leaves the count at 0), the gloves, bits 0x01 and 0x08
// of +0x3c and the 0x61 options. It replaces places 0, 1, 5, 10 and 11.
[[nodiscard]] Core::Expected<void, std::string> UpdateOwnAttackAndMagicSpeeds(Kernel::OwnCharacterRecord& own,
                                                                              const OwnEquipmentOverride& held,
                                                                              std::uint8_t& ownRecordAccesses,
                                                                              Crt::CrtRandom& random);

// 0x5507e0 (0x5512a5): +0x60 by the class and the agility, place 1 and the 0x5a options of places 2 to 6, 11 and 10.
[[nodiscard]] Core::Expected<void, std::string> UpdateOwnWord60(Kernel::OwnCharacterRecord& own,
                                                                const Tables::ItemTable* items);

// 0x550990 (0x5512ac): +0x62 by the class and the agility, places 1 to 7, what 0x505820 left (Actors/FullSetScan.h),
// the list (0x40d690, 0x40d7f0 twice and, with a type from 0xc00 to 0xdff in place 1, 0x40d730) and the 0x96 options
// of place 8 and of Cape of Lord. With a place with durability and type −1 it reads outside Item.bmd, as Q-C184: an
// error.
[[nodiscard]] Core::Expected<void, std::string> UpdateOwnWord62(Kernel::OwnCharacterRecord& own,
                                                                const Tables::ItemTable* items,
                                                                const Actors::Scan505820& scan,
                                                                ItemSetOptionList* setOptions);

// 0x550c30 (0x5512b3): +0x64 is the D+0x14 of the last of places 2 to 7 with durability (0 if its type is −1;
// 0x54e6b0). If none has any, +0x64 stays as it was.
void UpdateOwnWord64(Kernel::OwnCharacterRecord& own);

// 0x550c60 (0x5512ba): +0x66 is place 6's D+0x18 with durability, plus place 7's with durability (0x54e6d0). Without
// durability in 6 it does not set it: 7's is added to what was there.
void UpdateOwnWord66(Kernel::OwnCharacterRecord& own);
} // namespace Client::Player
