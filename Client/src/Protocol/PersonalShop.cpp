#include "Protocol/PersonalShop.h"

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

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

Outgoing WriteShopItemsRequest(std::uint16_t key, const Name& name)
{
    return EncryptedC1(Network::PacketWriter::C1(Opcode).PutU8(ItemsRequestSubcode).PutU16Be(key).PutBytes(name));
}
} // namespace Client::Protocol::PersonalShop
