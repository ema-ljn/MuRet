#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// The NPC windows (04, "Las ventanas de los NPC"): what an NPC opens (30), its grid's list (31), buying (32), selling
// (33), repairing (34), the warehouse (81, 82 and 83) and the chaos box (86 and 87). The dispatcher 0x4e2b10
// (0x4e3c2a..0x4e3d24 and 0x4e4692..0x4e4862) or its handler takes the ones that arrive; the windows, the ones that
// leave. Closing the shop (C1 03 31) is Protocol::Session::WriteWindowClose.
namespace Client::Protocol::Npc
{
inline constexpr Network::Opcode PersonalNumberOpcode = EventDialogOpcode;

// An item's seven bytes, as in the F3 10 and the 24.
inline constexpr std::size_t ItemDataSize = 7;
using ItemData = std::array<std::byte, ItemDataSize>;

// 0x4d253b..0x4d2566: with the chaos box, pkt[4..9] go to the six ints of 0x6c9da8..0x6c9dbc.
inline constexpr std::size_t ChaosRateCount = 6;

// The 30 that arrives (0x4d1e00): pkt[3] says what the NPC opens and pkt[4..9], with the chaos box, its six rates.
// The GameServer 0.97 sends it as an 8-byte C3 with four (PMSG_TALKRESULT, Q-C920): the ones the packet does not
// carry, the original reads from what was left in its receive buffer, and MuRet leaves them at 0. pkt[4] is also what
// case 12 reads (0x4d25bc).
struct TalkResult
{
    std::uint8_t result = 0;
    std::array<std::uint8_t, ChaosRateCount> rates{};
    // How many of the six the packet carried.
    std::size_t ratesInPacket = 0;
};

[[nodiscard]] Core::Expected<TalkResult, Network::ProtocolError> ReadTalkResult(const Network::Packet& packet);

// The report of a 30 that did not arrive encrypted (0x4d1e30..0x4d24fb): C1 06 F1 03 00 30 with the XOR32 and as a
// C3, like the 24's, without reading the packet.
[[nodiscard]] Outgoing WriteClearTalkReport();

// An entry of the 31's list: the place and the item's seven bytes, 8 bytes each from pkt[6].
struct ListedItem
{
    std::uint8_t slot = 0;
    ItemData item{};
};

// The 31 that arrives (0x4c5f60): pkt[4] is the grid (3 the chaos box, 5 the trainer's and any other 0x78a7070's, the
// shop or the warehouse) and pkt[5] the count. The original reads pkt[4] and pkt[5] without looking at whether it is a
// C1 or a C2: neither does MuRet.
struct ItemList
{
    std::uint8_t grid = 0;
    std::vector<ListedItem> items;
};

[[nodiscard]] Core::Expected<ItemList, Network::ProtocolError> ReadItemList(const Network::Packet& packet);

// The 32 that arrives (0x4e3c33..0x4e3c84): with pkt[3] other than 0xff, the inventory place and the seven bytes from
// pkt[4].
struct BuyResult
{
    std::uint8_t slot = 0;
    ItemData item{};
};

inline constexpr std::uint8_t BuyRejected = 0xff;

[[nodiscard]] Core::Expected<BuyResult, Network::ProtocolError> ReadBuyResult(const Network::Packet& packet);

// The 33 that arrives (0x4e3c8a..0x4e3cbb): with pkt[3] at 0, selling failed; with another, the new zen at pkt[4..7].
struct SellResult
{
    std::uint8_t result = 0;
    std::uint32_t money = 0;
};

[[nodiscard]] Core::Expected<SellResult, Network::ProtocolError> ReadSellResult(const Network::Packet& packet);

// The 34 that arrives (0x4e3cc1..0x4e3d24): the zen after repairing, at pkt[4..7]; with 0, nothing.
struct RepairResult
{
    std::uint32_t money = 0;
};

[[nodiscard]] Core::Expected<RepairResult, Network::ProtocolError> ReadRepairResult(const Network::Packet& packet);

// The 81 that arrives (0x4e4692..0x4e46de): with pkt[3] other than 0, the warehouse's zen (pkt[4..7], at +0xed0 of
// one's own record) and the inventory's (pkt[8..11], at +0xecc).
struct WarehouseMoney
{
    std::uint8_t result = 0;
    std::uint32_t warehouseMoney = 0;
    std::uint32_t inventoryMoney = 0;
};

[[nodiscard]] Core::Expected<WarehouseMoney, Network::ProtocolError> ReadWarehouseMoney(const Network::Packet& packet);

// The 82 and the 87 (0x4e4819..0x4e4862): the end of the warehouse and of the chaos box, with no fields.
struct NpcWindowClosed
{
    Network::Opcode opcode{};
};

[[nodiscard]] Core::Expected<NpcWindowClosed, Network::ProtocolError>
ReadNpcWindowClosed(const Network::Packet& packet);

// The 83 that arrives (0x4e46df): pkt[3] is the state of the warehouse's lock.
struct WarehouseState
{
    std::uint8_t state = 0;
};

[[nodiscard]] Core::Expected<WarehouseState, Network::ProtocolError> ReadWarehouseState(const Network::Packet& packet);

// The 86 that arrives (0x4da280): pkt[3] is the result and, with 1 (0x4da2ff) and with 100 (0x4da378), the item's
// seven bytes from pkt[4].
struct ChaosMixResult
{
    std::uint8_t result = 0;
    ItemData item{};
};

inline constexpr std::uint8_t ChaosMixSucceeded = 1;
// 0x4da373: the trainer's mix, with its item for the trainer's box.
inline constexpr std::uint8_t TrainerMixResult = 100;

[[nodiscard]] Core::Expected<ChaosMixResult, Network::ProtocolError> ReadChaosMixResult(const Network::Packet& packet);

// 0x5c4bdc..0x5c4e1e: buying, C1 04 32 with the place of the shop's grid, with the XOR32 and as a C3.
[[nodiscard]] Outgoing WriteBuy(std::uint8_t slot);

// 0x5d8e1a..0x5d905d: selling, C1 04 33 with the place the cursor's item came from (0x78a6758), with the XOR32 and as
// a C3.
[[nodiscard]] Outgoing WriteSell(std::uint8_t slot);

// 0x5c4485.. (one item, with 0x78abb10) and 0x5edaa7.. (everything, 0xff and 0): repairing, C1 05 34, with the XOR32
// and as a C3.
[[nodiscard]] Outgoing WriteRepair(std::uint8_t position, std::uint8_t requestPosition);

// 0x5edaa7..: the place with which the shop's button repairs everything.
inline constexpr std::uint8_t RepairEverything = 0xff;

// 0x4e6f80(type, zen): C1 08 81 with the type and the zen, with the XOR32 and in the clear.
[[nodiscard]] Outgoing WriteWarehouseMoney(std::uint8_t type, std::uint32_t money);

// 0x658567..0x658709, in 0x657ab0(0x40): closing the warehouse, C1 03 82 in the clear.
[[nodiscard]] Outgoing WriteWarehouseClose();

// The number that goes with the warehouse's password (0x78ab9fc..0x78aba05).
inline constexpr std::size_t WarehouseNumberSize = 10;
using WarehouseNumber = std::array<std::byte, WarehouseNumberSize>;

// 0x5e7e90..0x5e82e0: the warehouse's password, C1 10 83 with the type, the password (0x78abb24) and the number, with
// the XOR32 and as a C3.
[[nodiscard]] Outgoing WriteWarehousePassword(std::uint8_t type, std::uint16_t password, const WarehouseNumber& number);

// 0x5e8706..0x5e8bfd: the OK of the password box in mode 6 (the 0x8f's), C1 0D 99 with the ten bytes typed
// (0x78ab9fc), with the XOR32 and as a C3. The GameServer 0.97 reads the 99 as another request (04, Q-C925).
[[nodiscard]] Outgoing WritePersonalNumber(const WarehouseNumber& number);

// 0x5d9362..0x5d9527: mixing, C1 04 86 with the mix's type (0x7d2dc98), with the XOR32 and in the clear.
[[nodiscard]] Outgoing WriteChaosMix(std::uint8_t kind);

// 0x658767..0x658909, in 0x657ab0(0x100): closing the chaos box, C1 03 87 in the clear.
[[nodiscard]] Outgoing WriteChaosBoxClose();

// 0x65a956..0x65ac07: the request opening the chaos box sends before opening the inventory, C1 04 BD 09 with the
// XOR32 and in the clear.
[[nodiscard]] Outgoing WriteChaosRateRequest();
} // namespace Client::Protocol::Npc
