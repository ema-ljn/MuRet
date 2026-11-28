#pragma once

#include "CastleSiege/CastleBattleState.h"
#include "CastleSiege/CastleSeniorWindow.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "CastleSiege/SiegeBattleUiState.h"
#include "CastleSiege/SiegePanelState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>

// What the castle packets keep outside the battle (CL9 milestone 12; 04 § Castle Siege, las ventanas del castillo):
// the .bss zone 0x7d5c504..0x7d5c658 that B3 and B2 05..10 fill, the packet fields of the registration window
// ([0x7d536f8], 0x80000) and of the hunting zone guard's window ([0x7d53700], 0x200000).
// The two globals of B2 1A live in NpcWindowState (chaosTaxRate/taxRate). The open byte lives in
// DelegatedPanels (castleSiege at +4 and secondCastleSiege at +0x14); the widgets are kept here between frames.
namespace Client::CastleSiege
{
// The buttons get negative internal ids, like the dialog's and the guild's (P-C280); the native ones come from the
// counter 0x5762eb0. Their value is not used for the wire.
[[nodiscard]] inline Ui::OldButton CastleButton(std::int32_t id, std::int32_t width, std::int32_t height)
{
    Ui::OldButton button;
    button.base.id = id;
    button.base.width = width;
    button.base.height = height;
    return button;
}

// 0x46f320 and 0x4706f0: lists anchored at the bottom, 18 rows, capacity 150.
[[nodiscard]] inline Ui::OldList CastleGuildList(std::int32_t id)
{
    Ui::OldList list;
    list.base.id = id;
    list.base.x = 470;
    list.base.y = 360;
    list.base.width = 157;
    list.base.height = 235;
    list.base.rectMode = 2;
    list.rows = 18;
    return list;
}

// 0x46f7e0 and 0x470bb0: the row's toggle, strncpy of the name and the fields copied by B4/B5.
struct CastleGuildRow
{
    std::int32_t toggled = 0;
    std::array<char, 9> name{};
    std::uint32_t marks = 0;
    std::uint8_t first = 0;
    std::uint8_t second = 0;
};

// 0x7d5c504..0x7d5c658, flat: the server picks each record's index without the client checking it (0x668fd0,
// 0x669030: base + index × 0x1c, in 32 bits), and the records overlap (statue 4 is gate 0; gate 7 falls on the
// selections and the taxes). MuRet keeps the bytes as the original does and discards the packet that would write
// outside the zone (Q-C1360).
inline constexpr std::uint32_t CastleDataBase = 0x7d5c504;
inline constexpr std::size_t CastleDataSize = 0x154;
// 0x668fd0: type 0x11b (the statues) from 0x7d5c504 and 0x115 (the gates) from 0x7d5c574, 0x1c each.
// 0x669030 picks the gates with 0 and the statues with another value.
inline constexpr std::uint32_t StatueRecordsAt = 0x7d5c504;
inline constexpr std::uint32_t GateRecordsAt = 0x7d5c574;
inline constexpr std::uint32_t NpcRecordBytes = 0x1c;
inline constexpr std::uint32_t GateType = 0x115;
inline constexpr std::uint32_t StatueType = 0x11b;
// 0x7d5c640 and 0x7d5c644: the taxes the Castle Senior window shows and changes; 0x7d5c648 and 0x7d5c64c, the ones
// the server gave (0x669070 and 0x6690f0 write both pairs).
inline constexpr std::uint32_t ChaosTaxAt = 0x7d5c640;
inline constexpr std::uint32_t StoreTaxAt = 0x7d5c644;
inline constexpr std::uint32_t ServerChaosTaxAt = 0x7d5c648;
inline constexpr std::uint32_t ServerStoreTaxAt = 0x7d5c64c;
// 0x7d5c650..0x7d5c657: the castle's money, 64 bits (0x669070 byte by byte, 0x669160).
inline constexpr std::uint32_t CastleMoneyAt = 0x7d5c650;

// The fields of a 0x1c record (B3, 0x4df7d0 and 0x4df950).
inline constexpr std::uint32_t RecordType = 0;
inline constexpr std::uint32_t RecordIndex = 4;
inline constexpr std::uint32_t RecordDefense = 8;
inline constexpr std::uint32_t RecordRecovery = 0xc;
inline constexpr std::uint32_t RecordMaxLife = 0x10;
inline constexpr std::uint32_t RecordLife = 0x14;
// +0x1a: the NPC is there (0x6623e0 picks B2 05 with 0 and B2 06 with another value).
inline constexpr std::uint32_t RecordAlive = 0x1a;

struct CastleData
{
    std::array<std::uint8_t, CastleDataSize> bytes{};

    // false if [address, address + size) leaves the zone.
    [[nodiscard]] static bool Holds(std::uint32_t address, std::uint32_t size);
    // The caller already checked Holds: outside the zone, std::out_of_range.
    [[nodiscard]] std::uint32_t U32(std::uint32_t address) const;
    [[nodiscard]] std::uint64_t U64(std::uint32_t address) const;
    void SetU32(std::uint32_t address, std::uint32_t value);
    void SetU64(std::uint32_t address, std::uint64_t value);
    void SetBytes(std::uint32_t address, std::span<const std::uint8_t> values);
};

// 0x669030: the record of a type (0x115 picks the gates, any other the statues) and an index, in 32 bits.
[[nodiscard]] std::uint32_t NpcRecordAddress(std::uint32_t type, std::uint32_t index);

// The selections of the Castle Senior windows (set by 0x663b20 for the gates and 0x666190 for the statues) and what
// the dialog's answers read: the gates from 0x7d5c590 and the statues from 0x7d5c520, record 1 of each list
// (0x6623e0, 0x664370).
inline constexpr std::uint32_t SelectedGateAt = 0x7d5c638;
inline constexpr std::uint32_t SelectedStatueAt = 0x7d5c63c;
inline constexpr std::uint32_t FirstGateAt = 0x7d5c590;
inline constexpr std::uint32_t FirstStatueAt = 0x7d5c520;

// 0x660da0: a record's life level by its max life (+0x10, signed). A gate: up to 500000 the 0, up to 800000 the 1,
// exactly 1500000 the 2 and otherwise the 3. A statue: up to 400000 the 0, up to 750000 the 1, up to 1300000 the 2
// and otherwise the 3. Another type, −1.
[[nodiscard]] std::int32_t LifeLevelOf(std::uint32_t type, std::int32_t maxLife);
// 0x660d20: a level's max life. A gate: 500000, 800000, 1500000 with the 2 and 3000000 with any other; a statue:
// 400000, 750000, 1300000 with the 2 and 2500000 with any other. Another type, −1.
[[nodiscard]] std::int32_t MaxLifeOfLevel(std::uint32_t type, std::int32_t level);
// 0x66205d..0x6620ed: FILD/FIDIV of integers, FSTP of the quotient and of the red bar's product.
[[nodiscard]] float NpcLifeBarWidth(std::int32_t life, std::int32_t maximum, float width);
// 0x661050: recovery increase by type and level, also on aliased records of the flat zone.
[[nodiscard]] std::int32_t NpcRecoveryIncrease(std::uint32_t type, std::int32_t recovery);

// One of the three dates of +0xbc..+0xcd: the year in a word (the first one high in the packet) and four bytes.
struct SiegeDate
{
    std::uint16_t year = 0;
    std::array<std::uint8_t, 4> rest{};
};

// The fields of [0x7d536f8] (0x64aca0, 0x344 bytes) that the packets write. The constructor sets +0x1c to −1, +0xd4
// and +0xd8 to 0 and the names to 0; +0xbc..+0xd0 keep whatever new left (Q-C1362): MuRet starts them at 0.
struct SiegeRegistrationWindow
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
    // +0x18: initial tab; 0x64b200 sets it back to 0 only when the window was open.
    std::int32_t tab = 0;
    // +0x1c: the siege state (pkt[5] of B2 00 with MOVSX).
    std::int32_t state = -1;
    // +0xa8 (8 bytes and the 0 of +0xb0) and +0xb1 (10 and the 0 of +0xbb): the owner guild and its master, with
    // strncpy.
    std::array<std::uint8_t, 9> ownerGuild{};
    std::array<std::uint8_t, 11> ownerMaster{};
    // +0xbc..+0xcd.
    std::array<SiegeDate, 3> dates{};
    // +0xd0: pkt[0x2a..0x2d], the first one high.
    std::uint32_t remaining = 0;
    // +0xd4 (0x64d2b0): the registered marks; +0xd8 (0x64d290): the guild is registered (the argument's low byte, in a
    // dword).
    std::uint32_t marks = 0;
    std::uint32_t registered = 0;
    Ui::OldButton button = CastleButton(-300, 50, 18);
    Ui::OldList siegeGuildList = CastleGuildList(-301);
    Ui::OldList registeredGuildList = CastleGuildList(-302);
    std::deque<CastleGuildRow> siegeGuilds;
    std::deque<CastleGuildRow> registeredGuilds;
};

// The fields of [0x7d53700] (0x6485f0, 0x24c bytes) that the packets write; the constructor sets them to 0.
struct HuntZoneWindow
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
    // +0x15: the entry is open (B2 1F with 1: SETNZ of pkt[5]).
    std::uint8_t allowed = 0;
    std::uint8_t mode = 0;
    // +0x18: the price (B2 09 with pkt[5] at 3).
    std::uint32_t fee = 0;
    std::int32_t editedFee = 0;
    std::int32_t step = 0;
    std::int32_t max = 0;
    std::uint32_t dialogId = 0;
    Ui::OldButton commit = CastleButton(-310, 50, 18);
    Ui::OldButton plus = CastleButton(-311, 16, 16);
    Ui::OldButton minus = CastleButton(-312, 16, 16);
    Ui::OldButton enter = CastleButton(-313, 50, 18);
};

struct CastleSiegeState
{
    CastleData data{};
    SiegeRegistrationWindow registration{};
    HuntZoneWindow huntZone{};
    CastleSeniorWindow senior{};
    // The battle owner's mark (milestone 13); holders has its canonical owner in WorldRuntime.
    CastleBattleState battle{};
    // 0x5763530: the class (+0x18) of the siege panel 0x412140 builds on map 30 and 0x412560 releases elsewhere: 0
    // without a guild (0x20 bytes, vtable 0x6b88a8), 1 and 2 with one. 0x59ff10 and 0x5a7320 draw it.
    std::optional<std::uint8_t> siegePanelKind{};
    // The class 0 panel: 0x412195 builds it again whenever there was none or it was of another class.
    SiegePanelState siegePanel{};
    SiegeBattleUiState siegeBattleUi{};
    // [0x5767924]: the 0x36 bytes of the Castle Siege info, two halves of 0x1b. The B2 20 creates and fills it
    // (0x4e04e0) and 0x4e05f0 releases it; the 0x14 clears +4..+6 or +0x1f..+0x21 (0x4ca799..0x4ca7cd).
    std::optional<std::array<std::uint8_t, 0x36>> castleSiegeInfo{};
};
} // namespace Client::CastleSiege
