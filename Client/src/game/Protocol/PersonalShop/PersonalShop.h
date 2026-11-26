#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Outgoing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The personal shops (04, "Las tiendas personales"): the 3F that arrives. The GameServer 0.97 does not have it: the
// oracle is the client's ASM. 0x4e2b10 dispatches on the byte after the opcode (pkt[3] in a C1, pkt[4] in a C2,
// 0x4e4e3a) and each case reads its fields at fixed places of the packet, without looking at the type: the readers do
// the same. Here go the cases of the signs and the notices (0, 2, 3, 8, 0x10 and 0x12) and those of the item lists (1,
// 5, 6 and 0x13).
namespace Client::Protocol::PersonalShop
{
// 0x4db890 and 0x4dbb40: the result that carries the list.
inline constexpr std::uint8_t ListedResult = 1;

// 0x4dae80 and 0x4dafe0: the title, 36 bytes with the terminator in the stack's 37th.
inline constexpr std::size_t TitleSize = 36;

// A 0x26-byte entry of the 3F 00 (0x4dae80): the key big-endian and the title.
struct ShopTitleEntry
{
    std::uint16_t key = 0;
    std::string title;
};

// The 3F 00 (0x4dae80): the count at pkt[5] and the entries from pkt[6]. With the subcode at pkt[4], it is a C2.
struct ShopTitles
{
    std::vector<ShopTitleEntry> entries;
};

[[nodiscard]] Core::Expected<ShopTitles, Network::ProtocolError> ReadShopTitles(const Network::Packet& packet);

// The 3F 02 (0x4db700): the result of opening one's own at pkt[4] (1, open).
struct ShopCreated
{
    std::uint8_t result = 0;
};

[[nodiscard]] Core::Expected<ShopCreated, Network::ProtocolError> ReadShopCreated(const Network::Packet& packet);

// The 3F 03 (0x4e4ea5): the result at pkt[4] and, with 1, the key from pkt[5..6] big-endian.
struct ShopClosed
{
    std::uint8_t result = 0;
    std::uint16_t key = 0;
};

[[nodiscard]] Core::Expected<ShopClosed, Network::ProtocolError> ReadShopClosed(const Network::Packet& packet);

// The 3F 08 (0x4e4f5d): the name from pkt[5..14].
struct ShopNotice
{
    FrontEnd::NameBytes name{};
};

[[nodiscard]] Core::Expected<ShopNotice, Network::ProtocolError> ReadShopNotice(const Network::Packet& packet);

// The 3F 10 (0x4dafe0): the key from pkt[4..5] big-endian, the title from pkt[6..0x29] and the name from
// pkt[0x2a..0x33].
struct ShopTitle
{
    std::uint16_t key = 0;
    std::string title;
    FrontEnd::NameBytes name{};
};

[[nodiscard]] Core::Expected<ShopTitle, Network::ProtocolError> ReadShopTitle(const Network::Packet& packet);

// The 3F 12 (0x4e4fcc): it reads nothing else.
struct ShopOwnerLeft
{
};

[[nodiscard]] Core::Expected<ShopOwnerLeft, Network::ProtocolError> ReadShopOwnerLeft(const Network::Packet& packet);

// The 3F 01 (0x4db120, "ReceiveSetPriceResult"): the result at pkt[4] and the place at pkt[5].
struct PriceResult
{
    std::uint8_t result = 0;
    std::uint8_t slot = 0;
};

[[nodiscard]] Core::Expected<PriceResult, Network::ProtocolError> ReadPriceResult(const Network::Packet& packet);

// A 0xc-byte entry of the lists (0x4db9e2.. and 0x4dbc01..): the place, the item's seven bytes and the price (a
// signed DWORD LE at +8).
struct ShopListEntry
{
    std::uint8_t slot = 0;
    Items::ItemData item{};
    std::int32_t price = 0;
};

// The 3F 05 (0x4db890, "ReceivePersonalShopItemList") and the 3F 0x13 (0x4dbb40, "ReceiveRefreshItemList"): the result
// at pkt[5], the owner's key at pkt[6..7] (BE), the title at pkt[0x12..0x35] (0x4db92b, a strncpy of 0x24), the count
// at pkt[0x36] and the entries from pkt[0x37]. The 0x13 reads neither the key nor the title. Without the result 1 only
// pkt[5] is read. Both arrive as C2s (the subcode at pkt[4]).
struct ShopItemList
{
    std::uint8_t result = 0;
    std::uint16_t key = 0;
    // The 36 bytes as they are: strncpy neither cuts nor terminates at the 36th.
    std::array<std::byte, TitleSize> title{};
    std::vector<ShopListEntry> entries;
};

[[nodiscard]] Core::Expected<ShopItemList, Network::ProtocolError> ReadShopItemList(const Network::Packet& packet);

// The 3F 0x13: the same without the key or the title, in its own type for the dispatch.
struct ShopRefresh
{
    std::uint8_t result = 0;
    std::vector<ShopListEntry> entries;
};

[[nodiscard]] Core::Expected<ShopRefresh, Network::ProtocolError> ReadShopRefresh(const Network::Packet& packet);

// The 3F 06 (0x4dbcb0, "ReceivePurchaseItem"): the result at pkt[4] and, with 1, the item's seven bytes at
// pkt[7..0xd] and its place at pkt[0xe]. pkt[5] and pkt[6] are not read.
struct ShopPurchase
{
    std::uint8_t result = 0;
    Items::ItemData item{};
    std::uint8_t slot = 0;
};

[[nodiscard]] Core::Expected<ShopPurchase, Network::ProtocolError> ReadShopPurchase(const Network::Packet& packet);

// The C1 04 F3 10 with the XOR32 and as a C3 ("SendRequestInventory"), which opening a shop (0x65ac87 and 0x65b359)
// with 0x6c931c other than −1 and the 3F 01 that fails (0x4db1a2) send.
[[nodiscard]] Outgoing WriteInventoryRequest();

// What the window of one's own shop sends: opening it with the title (0x5ea290 and the box 0x9c, 0x623750), closing it
// (0x5ea290, 0x5cc2cf, 0x5d5046 and 0x5da5ac), a place's price (0x5d8d50) and buying in another's (0x5d9c9e).

// The 3F 01 (0x5dab08.. and 0x5dc05f..): C1 09 3F 01, the place ([0x78a6758], the low byte) and the price (0x77f3b6c, a
// DWORD LE), with the XOR32 and as a C3.
[[nodiscard]] Outgoing WriteSetPrice(std::uint8_t slot, std::uint32_t price);

// The 3F 02 (0x5ea407.. and 0x62378b..): C1 28 3F 02 and the title's 36 bytes (0x78aba64, MOVSD × 9: whatever is past
// the terminator goes too), with the XOR32 and as a C3.
[[nodiscard]] Outgoing WriteOpenShop(const std::array<std::byte, TitleSize>& title);

// The 3F 03 (0x5eaab1.., 0x5cc2f7.., 0x5d50b3.. and 0x5da5d2..): C1 04 3F 03, with the XOR32 and as a C3.
[[nodiscard]] Outgoing WriteCloseShop();

// The 3F 06 (0x5d9c9e..): C1 11 3F 06, the owner's key (0x78a6514) big-endian, their name (0x78a64f9, 10 bytes) and
// the place ([0x78a6758], the low byte), with the XOR32 and as a C3.
[[nodiscard]] Outgoing WriteBuyItem(std::uint16_t key, const FrontEnd::NameBytes& name, std::uint8_t slot);

// 0x5eb020's 3F 07, on closing another's shop with their key (0x78a6514) other than 0: C1 10 3F 07, the key
// big-endian and the name (0x78a64f9, 10 bytes), with the XOR32 and as a C3.
[[nodiscard]] Outgoing WriteViewClosed(std::uint16_t key, const FrontEnd::NameBytes& name);

// The client's 3F 05 (0x406050 from 0x406b02, the command window's command 2, the text 1124): C1 10 3F 05, the other's
// key big-endian (+0x1f4) and their name (+0x1d9, 10 bytes), with the XOR32 and as a C3. What the server answers is
// the 3F 05 above (ReadShopItemList).
[[nodiscard]] Outgoing WriteShopItemsRequest(std::uint16_t key, const FrontEnd::NameBytes& name);
} // namespace Client::Protocol::PersonalShop
