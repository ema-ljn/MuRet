#include "Protocol/PersonalShop.h"

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace Client::Protocol::PersonalShop
{
namespace
{
// 0x4dae80: la cuenta en pkt[5]; cada entrada, de 0x26 bytes desde pkt[6], con la clave (pkt[6] alta, pkt[7] baja) y
// el título desde pkt[8].
constexpr std::size_t CountAt = 5;
constexpr std::size_t FirstEntryAt = 6;
constexpr std::size_t EntrySize = 0x26;
// 0x4db700 y 0x4e4ea5.
constexpr std::size_t ResultAt = 4;
constexpr std::size_t ClosedKeyAt = 5;
// 0x4e4f5d: strncpy(pila, pkt + 5, 10).
constexpr std::size_t NoticeNameAt = 5;
// 0x4dafe0.
constexpr std::size_t TitleKeyAt = 4;
constexpr std::size_t TitleAt = 6;
constexpr std::size_t TitleNameAt = 0x2a;
// 0x4db120: el resultado en pkt[4] y el lugar en pkt[5].
constexpr std::size_t PriceSlotAt = 5;
// 0x4db890 y 0x4dbb40.
constexpr std::size_t ListResultAt = 5;
constexpr std::size_t ListKeyAt = 6;
constexpr std::size_t ListTitleAt = 0x12;
constexpr std::size_t ListCountAt = 0x36;
constexpr std::size_t FirstListEntryAt = 0x37;
constexpr std::size_t ListEntrySize = 0xc;
constexpr std::size_t ListEntryPriceAt = 8;
// 0x4dbcb0: lea pkt + 7 (el ítem) y pkt[0xe] (el lugar).
constexpr std::size_t PurchaseItemAt = 7;
constexpr std::size_t PurchaseSlotAt = 0xe;

Core::Expected<std::span<const std::byte>, ProtocolError> BytesOf(const Network::Packet& packet, std::size_t needed)
{
    if (packet.Opcode() != Opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    const std::span<const std::byte> bytes = packet.Bytes();
    if (bytes.size() < needed)
        return Core::Unexpected(ProtocolError::TooShort);
    return bytes;
}

std::uint8_t ByteAt(std::span<const std::byte> bytes, std::size_t at)
{
    return std::to_integer<std::uint8_t>(bytes[at]);
}

std::uint16_t KeyAt(std::span<const std::byte> bytes, std::size_t at)
{
    return static_cast<std::uint16_t>((ByteAt(bytes, at) << 8) | ByteAt(bytes, at + 1));
}

// Los 36 bytes del título hasta el primer 0: el terminador de la pila va en el 37.
std::string TitleAt36(std::span<const std::byte> bytes, std::size_t at)
{
    std::string title;
    for (std::size_t offset = 0; offset < TitleSize; ++offset)
    {
        const auto letter = static_cast<char>(ByteAt(bytes, at + offset));
        if (letter == '\0')
            break;
        title.push_back(letter);
    }
    return title;
}

Name NameAt(std::span<const std::byte> bytes, std::size_t at)
{
    Name name{};
    std::ranges::copy(bytes.subspan(at, name.size()), name.begin());
    return name;
}

Inventory::ItemData ItemAt(std::span<const std::byte> bytes, std::size_t at)
{
    Inventory::ItemData item{};
    std::ranges::copy(bytes.subspan(at, item.size()), item.begin());
    return item;
}

std::int32_t PriceAt(std::span<const std::byte> bytes, std::size_t at)
{
    const auto value = static_cast<std::uint32_t>(ByteAt(bytes, at)) |
                       (static_cast<std::uint32_t>(ByteAt(bytes, at + 1)) << 8) |
                       (static_cast<std::uint32_t>(ByteAt(bytes, at + 2)) << 16) |
                       (static_cast<std::uint32_t>(ByteAt(bytes, at + 3)) << 24);
    return static_cast<std::int32_t>(value);
}

// Lo común del 05 y el 0x13: con el resultado 1, la cuenta y las entradas; el 05 lee además la clave y el título.
Core::Expected<ShopItemList, ProtocolError> ReadList(const Network::Packet& packet, bool withOwner)
{
    auto read = BytesOf(packet, ListResultAt + 1);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const std::span<const std::byte> bytes = read.Value();
    ShopItemList list{};
    list.result = ByteAt(bytes, ListResultAt);
    if (list.result != ListedResult)
        return list;
    if (bytes.size() < FirstListEntryAt)
        return Core::Unexpected(ProtocolError::TooShort);
    const std::size_t count = ByteAt(bytes, ListCountAt);
    if (bytes.size() < FirstListEntryAt + count * ListEntrySize)
        return Core::Unexpected(ProtocolError::TooShort);
    if (withOwner)
    {
        list.key = KeyAt(bytes, ListKeyAt);
        std::ranges::copy(bytes.subspan(ListTitleAt, list.title.size()), list.title.begin());
    }
    for (std::size_t entry = 0; entry < count; ++entry)
    {
        const std::size_t at = FirstListEntryAt + entry * ListEntrySize;
        list.entries.push_back(ShopListEntry{
            .slot = ByteAt(bytes, at), .item = ItemAt(bytes, at + 1), .price = PriceAt(bytes, at + ListEntryPriceAt)});
    }
    return list;
}
} // namespace

Core::Expected<ShopTitles, ProtocolError> ReadShopTitles(const Network::Packet& packet)
{
    auto read = BytesOf(packet, FirstEntryAt);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const std::span<const std::byte> bytes = read.Value();
    const std::size_t count = ByteAt(bytes, CountAt);
    if (bytes.size() < FirstEntryAt + count * EntrySize)
        return Core::Unexpected(ProtocolError::TooShort);
    ShopTitles titles;
    for (std::size_t entry = 0; entry < count; ++entry)
    {
        const std::size_t at = FirstEntryAt + entry * EntrySize;
        titles.entries.push_back(ShopTitleEntry{.key = KeyAt(bytes, at), .title = TitleAt36(bytes, at + 2)});
    }
    return titles;
}

Core::Expected<ShopCreated, ProtocolError> ReadShopCreated(const Network::Packet& packet)
{
    auto read = BytesOf(packet, ResultAt + 1);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    return ShopCreated{.result = ByteAt(read.Value(), ResultAt)};
}

Core::Expected<ShopClosed, ProtocolError> ReadShopClosed(const Network::Packet& packet)
{
    auto read = BytesOf(packet, ResultAt + 1);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const std::span<const std::byte> bytes = read.Value();
    ShopClosed closed{.result = ByteAt(bytes, ResultAt)};
    // 0x4e4eab: la clave solo con el 1.
    if (closed.result == 1)
    {
        if (bytes.size() < ClosedKeyAt + 2)
            return Core::Unexpected(ProtocolError::TooShort);
        closed.key = KeyAt(bytes, ClosedKeyAt);
    }
    return closed;
}

Core::Expected<ShopNotice, ProtocolError> ReadShopNotice(const Network::Packet& packet)
{
    auto read = BytesOf(packet, NoticeNameAt + Character::NameSize);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    return ShopNotice{.name = NameAt(read.Value(), NoticeNameAt)};
}

Core::Expected<ShopTitle, ProtocolError> ReadShopTitle(const Network::Packet& packet)
{
    auto read = BytesOf(packet, TitleNameAt + Character::NameSize);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const std::span<const std::byte> bytes = read.Value();
    return ShopTitle{
        .key = KeyAt(bytes, TitleKeyAt), .title = TitleAt36(bytes, TitleAt), .name = NameAt(bytes, TitleNameAt)};
}

Core::Expected<ShopOwnerLeft, ProtocolError> ReadShopOwnerLeft(const Network::Packet& packet)
{
    auto read = BytesOf(packet, 0);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    return ShopOwnerLeft{};
}

Core::Expected<PriceResult, ProtocolError> ReadPriceResult(const Network::Packet& packet)
{
    auto read = BytesOf(packet, PriceSlotAt + 1);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    return PriceResult{.result = ByteAt(read.Value(), ResultAt), .slot = ByteAt(read.Value(), PriceSlotAt)};
}

Core::Expected<ShopItemList, ProtocolError> ReadShopItemList(const Network::Packet& packet)
{
    return ReadList(packet, true);
}

Core::Expected<ShopRefresh, ProtocolError> ReadShopRefresh(const Network::Packet& packet)
{
    auto list = ReadList(packet, false);
    if (!list.HasValue())
        return Core::Unexpected(list.Error());
    return ShopRefresh{.result = list.Value().result, .entries = std::move(list.Value().entries)};
}

Core::Expected<ShopPurchase, ProtocolError> ReadShopPurchase(const Network::Packet& packet)
{
    auto read = BytesOf(packet, ResultAt + 1);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const std::span<const std::byte> bytes = read.Value();
    ShopPurchase purchase{.result = ByteAt(bytes, ResultAt)};
    // 0x4dbcbe: el ítem y el lugar solo con el 1.
    if (purchase.result != 1)
        return purchase;
    if (bytes.size() < PurchaseSlotAt + 1)
        return Core::Unexpected(ProtocolError::TooShort);
    purchase.item = ItemAt(bytes, PurchaseItemAt);
    purchase.slot = ByteAt(bytes, PurchaseSlotAt);
    return purchase;
}

Outgoing WriteInventoryRequest()
{
    return EncryptedC1(Network::PacketWriter::C1(Character::CharacterOpcode).PutU8(Character::InventoryListSubcode));
}

Outgoing WriteViewClosed(std::uint16_t key, const Name& name)
{
    return EncryptedC1(Network::PacketWriter::C1(Opcode).PutU8(ViewClosedSubcode).PutU16Be(key).PutBytes(name));
}

Outgoing WriteShopItemsRequest(std::uint16_t key, const Name& name)
{
    return EncryptedC1(Network::PacketWriter::C1(Opcode).PutU8(ItemsRequestSubcode).PutU16Be(key).PutBytes(name));
}
} // namespace Client::Protocol::PersonalShop
