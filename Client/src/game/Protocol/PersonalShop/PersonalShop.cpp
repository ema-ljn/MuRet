#include "Protocol/PersonalShop/PersonalShop.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Network/ReadList.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace Client::Protocol::PersonalShop
{
namespace
{
// 0x4dae80: the count at pkt[5]; each entry, 0x26 bytes from pkt[6], with the key (pkt[6] high, pkt[7] low) and the
// title from pkt[8].
using TitleCount = Network::Field<std::uint8_t, 5>;
constexpr std::size_t TitleEntrySize = 0x26;
constexpr std::size_t KeySize = 2;
// 0x4db700, 0x4e4ea5, 0x4db120 and 0x4dbcb0.
using Result = Network::Field<std::uint8_t, 4>;
// 0x4e4eab: the key only with the 1.
using ClosedKey = Network::Field<std::uint16_t, 5, std::endian::big>;
constexpr std::uint8_t ClosedWithKey = 1;
// 0x4e4f5d: strncpy(stack, pkt + 5, 10).
using NoticeName = Network::Field<FrontEnd::NameBytes, 5>;
// 0x4dafe0.
using TitleKey = Network::Field<std::uint16_t, 4, std::endian::big>;
constexpr std::size_t TitleAt = 6;
using TitleName = Network::Field<FrontEnd::NameBytes, 0x2a>;
using PriceSlot = Network::Field<std::uint8_t, 5>;
// 0x4db890 and 0x4dbb40.
using ListResult = Network::Field<std::uint8_t, 5>;
using ListKey = Network::Field<std::uint16_t, 6, std::endian::big>;
using ListTitle = Network::Field<std::array<std::byte, TitleSize>, 0x12>;
using ListCount = Network::Field<std::uint8_t, 0x36>;
constexpr std::size_t ListEntrySize = 0xc;
constexpr std::size_t ListEntryPriceAt = 8;
// 0x4dbcbe: lea pkt + 7 (the item) and pkt[0xe] (the place), only with the 1.
using PurchasedItem = Network::Field<Items::ItemData, 7>;
using PurchasedSlot = Network::Field<std::uint8_t, 0xe>;
constexpr std::uint8_t Purchased = 1;
// 0x4e4fcc reads nothing past the subcode.
constexpr std::size_t NothingMore = 0;

// 0x4e4e3a: the dispatcher switches on the byte after the opcode (pkt[3] in a C1, pkt[4] in a C2) and each case reads
// its fields at fixed places of the packet, whatever its type, without looking at the subcode again: the layout takes
// the packet's type and no subcode.
Network::PacketLayout LayoutOf(const Network::Packet& packet, std::size_t minimumSize, std::uint32_t address)
{
    return {{packet.Type(), PersonalShopOpcode, std::nullopt}, minimumSize, address};
}

std::uint16_t KeyOf(std::span<const std::byte, KeySize> bytes)
{
    return static_cast<std::uint16_t>((std::to_integer<std::uint16_t>(bytes[0]) << 8) |
                                      std::to_integer<std::uint16_t>(bytes[1]));
}

// The title's 36 bytes up to the first 0: the stack's terminator goes in the 37th.
std::string TitleText(std::span<const std::byte> bytes)
{
    std::string title;
    for (const std::byte letter : bytes)
    {
        if (letter == std::byte{0})
        {
            break;
        }
        title.push_back(static_cast<char>(letter));
    }
    return title;
}

ShopTitleEntry ReadTitleEntry(std::span<const std::byte, TitleEntrySize> bytes)
{
    return ShopTitleEntry{.key = KeyOf(bytes.first<KeySize>()),
                          .title = TitleText(bytes.subspan<KeySize, TitleSize>())};
}

ShopListEntry ReadListEntry(std::span<const std::byte, ListEntrySize> bytes)
{
    ShopListEntry entry{.slot = std::to_integer<std::uint8_t>(bytes[0])};
    std::ranges::copy(bytes.subspan<1, Items::ItemDataSize>(), entry.item.begin());
    Core::ByteReader price(bytes.subspan<ListEntryPriceAt, sizeof(std::uint32_t)>());
    entry.price = static_cast<std::int32_t>(*price.ReadU32Le());
    return entry;
}

// What the 05 and the 0x13 share: with the result 1, the count and the entries; the 05 also reads the key and the
// title.
Core::Expected<ShopItemList, Network::ProtocolError> ReadList(const Network::Packet& packet, std::uint32_t address,
                                                              bool withOwner)
{
    return Network::ReadPacket(packet, LayoutOf(packet, ListResult::End, address),
                               [withOwner](Network::LayoutReader& reader)
                               {
                                   ShopItemList list{.result = reader.Read<ListResult>(), .entries = {}};
                                   if (list.result != ListedResult)
                                   {
                                       return list;
                                   }
                                   if (withOwner)
                                   {
                                       list.key = reader.Read<ListKey>();
                                       list.title = reader.Read<ListTitle>();
                                   }
                                   list.entries = Network::ReadList<ListEntrySize>(
                                       reader, ListCount::End, reader.Read<ListCount>(), ReadListEntry);
                                   return list;
                               });
}
} // namespace

Core::Expected<ShopTitles, Network::ProtocolError> ReadShopTitles(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, TitleCount::End, 0x4dae80),
                               [](Network::LayoutReader& reader)
                               {
                                   return ShopTitles{Network::ReadList<TitleEntrySize>(
                                       reader, TitleCount::End, reader.Read<TitleCount>(), ReadTitleEntry)};
                               });
}

Core::Expected<ShopCreated, Network::ProtocolError> ReadShopCreated(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, Result::End, 0x4db700), [](Network::LayoutReader& reader)
                               { return ShopCreated{.result = reader.Read<Result>()}; });
}

Core::Expected<ShopClosed, Network::ProtocolError> ReadShopClosed(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, Result::End, 0x4e4ea5),
                               [](Network::LayoutReader& reader)
                               {
                                   ShopClosed closed{.result = reader.Read<Result>()};
                                   if (closed.result == ClosedWithKey)
                                   {
                                       closed.key = reader.Read<ClosedKey>();
                                   }
                                   return closed;
                               });
}

Core::Expected<ShopNotice, Network::ProtocolError> ReadShopNotice(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, NoticeName::End, 0x4e4f5d), [](Network::LayoutReader& reader)
                               { return ShopNotice{.name = reader.Read<NoticeName>()}; });
}

Core::Expected<ShopTitle, Network::ProtocolError> ReadShopTitle(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, TitleName::End, 0x4dafe0),
                               [](Network::LayoutReader& reader)
                               {
                                   return ShopTitle{.key = reader.Read<TitleKey>(),
                                                    .title = TitleText(reader.Bytes(TitleAt, TitleSize)),
                                                    .name = reader.Read<TitleName>()};
                               });
}

Core::Expected<ShopOwnerLeft, Network::ProtocolError> ReadShopOwnerLeft(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, NothingMore, 0x4e4fcc),
                               [](Network::LayoutReader&) { return ShopOwnerLeft{}; });
}

Core::Expected<PriceResult, Network::ProtocolError> ReadPriceResult(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, LayoutOf(packet, PriceSlot::End, 0x4db120), [](Network::LayoutReader& reader)
        { return PriceResult{.result = reader.Read<Result>(), .slot = reader.Read<PriceSlot>()}; });
}

Core::Expected<ShopItemList, Network::ProtocolError> ReadShopItemList(const Network::Packet& packet)
{
    return ReadList(packet, 0x4db890, true);
}

Core::Expected<ShopRefresh, Network::ProtocolError> ReadShopRefresh(const Network::Packet& packet)
{
    auto list = ReadList(packet, 0x4dbb40, false);
    if (!list.HasValue())
    {
        return Core::Unexpected(list.Error());
    }
    return ShopRefresh{.result = list.Value().result, .entries = std::move(list.Value().entries)};
}

Core::Expected<ShopPurchase, Network::ProtocolError> ReadShopPurchase(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, Result::End, 0x4dbcb0),
                               [](Network::LayoutReader& reader)
                               {
                                   ShopPurchase purchase{.result = reader.Read<Result>()};
                                   if (purchase.result == Purchased)
                                   {
                                       purchase.item = reader.Read<PurchasedItem>();
                                       purchase.slot = reader.Read<PurchasedSlot>();
                                   }
                                   return purchase;
                               });
}

Outgoing WriteInventoryRequest()
{
    return EncryptedC1(Network::PacketWriter::C1(CharacterOpcode, Character::InventoryListSubcode));
}

Outgoing WriteViewClosed(std::uint16_t key, const FrontEnd::NameBytes& name)
{
    return EncryptedC1(Network::PacketWriter::C1(PersonalShopOpcode, ViewClosedSubcode).PutU16Be(key).PutBytes(name));
}

Outgoing WriteShopItemsRequest(std::uint16_t key, const FrontEnd::NameBytes& name)
{
    return EncryptedC1(Network::PacketWriter::C1(PersonalShopOpcode, ItemListSubcode).PutU16Be(key).PutBytes(name));
}

Outgoing WriteSetPrice(std::uint8_t slot, std::uint32_t price)
{
    return EncryptedC1(Network::PacketWriter::C1(PersonalShopOpcode, SetPriceSubcode).PutU8(slot).PutU32Le(price));
}

Outgoing WriteOpenShop(const std::array<std::byte, TitleSize>& title)
{
    return EncryptedC1(Network::PacketWriter::C1(PersonalShopOpcode, OpenSubcode).PutBytes(title));
}

Outgoing WriteCloseShop()
{
    return EncryptedC1(Network::PacketWriter::C1(PersonalShopOpcode, CloseSubcode));
}

Outgoing WriteBuyItem(std::uint16_t key, const FrontEnd::NameBytes& name, std::uint8_t slot)
{
    return EncryptedC1(
        Network::PacketWriter::C1(PersonalShopOpcode, BuySubcode).PutU16Be(key).PutBytes(name).PutU8(slot));
}
} // namespace Client::Protocol::PersonalShop
