#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Outgoing.h"

#include <array>
#include <cstdint>

// The trade (04, "El comercio"): the 36 to the 3D both ways. The readers are the fields 0x4d4af0 (36), 0x4d5570 (37)
// and the inline cases of the dispatcher 0x4e2b10 read (38 to 3C, 0x4e3d6a..0x4e3e7f; the 3D goes to 0x4d57f0); the
// writers, what 0x406050 and 0x56c440 (36), 0x623e70 (37, 3A and 3C), 0x5ecde0 (3C and 3D) and 0x657ab0 (3D) build.
namespace Client::Protocol::Social
{
// The GS's 36 (0x4d4af0): C1 0D 36 and the requester's name, 10 bytes from pkt[3].
struct TradeRequest
{
    FrontEnd::NameBytes name{};
};

[[nodiscard]] Core::Expected<TradeRequest, Network::ProtocolError> ReadTradeRequest(const Network::Packet& packet);

// The 36 that arrived in the clear (0x4d4b1c, [EBP+0xc] at 0): it is not read; 0x4d4af0 only sends the report
// (WriteClearTradeRequestReport).
struct ClearTradeRequest
{
};

// The GS's 37 (0x4d5570): C1 14 37, the response at pkt[3] (0 no, 1 yes, 2 the other is busy), the name at
// pkt[4..13], the level at pkt[0xe] (WORD) and the guild at pkt[0x10] (DWORD), both little-endian.
struct TradeResponse
{
    std::uint8_t response = 0;
    FrontEnd::NameBytes name{};
    std::uint16_t level = 0;
    std::uint32_t guild = 0;
};

[[nodiscard]] Core::Expected<TradeResponse, Network::ProtocolError> ReadTradeResponse(const Network::Packet& packet);

// The 38 (0x4e3d6a): C1 04 38 and the place of the other's grid that is taken out.
struct TradeItemRemoved
{
    std::uint8_t slot = 0;
};

[[nodiscard]] Core::Expected<TradeItemRemoved, Network::ProtocolError>
ReadTradeItemRemoved(const Network::Packet& packet);

// The 39 (0x4e3d99): C1 0B 39, the place at pkt[3] and the item's seven bytes from pkt[4] (08 P-C200: the GS 0.97 sends
// them in another length).
struct TradeItemAdded
{
    std::uint8_t slot = 0;
    Items::ItemData item{};
};

[[nodiscard]] Core::Expected<TradeItemAdded, Network::ProtocolError> ReadTradeItemAdded(const Network::Packet& packet);

// The GS's 3A (0x4e3dd0): C1 04 3A and whether it accepted the zen put in (other than 0) or not.
struct TradeMoneyResult
{
    std::uint8_t accepted = 0;
};

[[nodiscard]] Core::Expected<TradeMoneyResult, Network::ProtocolError>
ReadTradeMoneyResult(const Network::Packet& packet);

// The 3B (0x4e3df3): C1 08 3B, a byte nobody reads and the other's zen (DWORD little-endian) at pkt[4].
struct TradeOtherMoney
{
    std::uint32_t money = 0;
};

[[nodiscard]] Core::Expected<TradeOtherMoney, Network::ProtocolError>
ReadTradeOtherMoney(const Network::Packet& packet);

// The GS's 3C (0x4e3e0a): C1 04 3C and the state of the other's OK (0 off, 1 on, 2 one's own goes off).
struct TradeOkState
{
    std::uint8_t state = 0;
};

[[nodiscard]] Core::Expected<TradeOkState, Network::ProtocolError> ReadTradeOkState(const Network::Packet& packet);

// The GS's 3D (0x4d57f0): C1 04 3D and the result (0 cancelled, 1 done, 2 no room, 3 the time). With the lock
// 0x78abb39, 0x4d57f0 keeps the packet's first four bytes at 0x57635bc and 0x4d1400 repeats it: `head` are those
// bytes.
struct TradeResult
{
    std::uint8_t result = 0;
    std::array<std::uint8_t, 4> head{};
};

[[nodiscard]] Core::Expected<TradeResult, Network::ProtocolError> ReadTradeResult(const Network::Packet& packet);

// The client's 36 (0x406050 at 0x4064f0..0x406af0 and 0x56c440): C1 05 36, the other's key big-endian (+0x1f4 >> 8
// and then the low byte), with the XOR32 and as a C3.
[[nodiscard]] Outgoing WriteTradeRequest(std::uint16_t key);

// The client's 37: C1 04 37 and the response (1 yes, 0 no), with the XOR32 and in the clear. The 0x78 dialog of the
// step 0x623e70 (yes 0x62c20a, no 0x62d4a2) and 0x4d4af0 (the 0, with the personal shop open) build it.
[[nodiscard]] Outgoing WriteTradeResponse(std::uint8_t response);

// The client's 3A (0x623e70, 0x626b30..0x626e36): C1 08 3A 00 and the zen (DWORD little-endian), with the XOR32 and in
// the clear.
[[nodiscard]] Outgoing WriteTradeMoney(std::uint32_t money);

// The client's 3C: C1 04 3C and the OK (1 on, 0 off), with the XOR32 and as a C3 (0x5ecde0, 0x5ecf9c..0x5ed2b5).
[[nodiscard]] Outgoing WriteTradeOkButton(std::uint8_t state);

// The client's 3D: C1 03 3D, with the XOR32 and as a C3 (0x5ecde0, 0x5ed4ec..; 0x657ab0 on closing the panel 0x20).
[[nodiscard]] Outgoing WriteTradeCancel();

// The report of a 36 that did not arrive encrypted (0x4d4af0, 0x4d4b22..0x4d5238): C1 06 F1 03 00 36 with the XOR32
// and as a C3, like the 24's (Inventory::WriteClearItemMoveReport).
[[nodiscard]] Outgoing WriteClearTradeRequestReport();
} // namespace Client::Protocol::Social
