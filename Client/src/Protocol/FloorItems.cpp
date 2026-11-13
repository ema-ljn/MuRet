#include "Protocol/FloorItems.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Protocol::FloorItems
{
namespace
{
// 0x4d0ba0: la clave (2), la celda (2) y el ítem (7).
constexpr std::size_t CreateEntrySize = 4 + Inventory::ItemDataSize;
constexpr std::size_t KeySize = 2;

// Un paquete de `type` y `opcode`: el lector desde el byte que sigue al opcode (pkt[3] en un C1, pkt[4] en un C2).
Core::Expected<Core::ByteReader, ProtocolError> FieldsOf(const Network::Packet& packet, Network::PacketType type,
                                                         std::uint8_t opcode)
{
    if (packet.Type() != type || packet.Opcode() != opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    return Core::ByteReader(packet.Bytes().subspan(Network::MinPacketSize(type)));
}

Inventory::ItemData ItemFrom(std::span<const std::byte> bytes)
{
    Inventory::ItemData item{};
    std::ranges::copy(bytes, item.begin());
    return item;
}
} // namespace

Core::Expected<FloorItemsCreate, ProtocolError> ReadFloorItemsCreate(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C2, CreateOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const std::optional<std::uint8_t> count = reader.ReadU8();
    if (!count || *count > reader.Remaining() / CreateEntrySize)
        return Core::Unexpected(ProtocolError::TooShort);
    FloorItemsCreate create;
    create.items.reserve(*count);
    // Se comprobó toda la lista antes de leerla: ninguna lectura puede quedar incompleta.
    for (std::size_t index = 0; index < *count; ++index)
    {
        FloorItemEntry entry;
        entry.key = *reader.ReadU16Be();
        entry.x = *reader.ReadU8();
        entry.y = *reader.ReadU8();
        entry.item = ItemFrom(*reader.ReadBytes(Inventory::ItemDataSize));
        create.items.push_back(entry);
    }
    return create;
}

Core::Expected<FloorItemsDelete, ProtocolError> ReadFloorItemsDelete(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C2, DeleteOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const std::optional<std::uint8_t> count = reader.ReadU8();
    if (!count || *count > reader.Remaining() / KeySize)
        return Core::Unexpected(ProtocolError::TooShort);
    FloorItemsDelete deleted;
    deleted.keys.reserve(*count);
    for (std::size_t index = 0; index < *count; ++index)
        deleted.keys.push_back(*reader.ReadU16Be());
    return deleted;
}

Core::Expected<PickUpResult, ProtocolError> ReadPickUpResult(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C1, PickUpOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto result = reader.ReadU8();
    if (!result)
        return Core::Unexpected(ProtocolError::TooShort);
    PickUpResult pickUp{.result = *result};
    if (*result == NothingPickedUp)
        return pickUp;
    // 0x4d1051: el sonido lee el tipo de los bytes desde pkt[4] también con el zen, así que el original lee siete
    // bytes en cualquier caso (los del zen son sus cuatro y lo que sigue).
    const auto item = reader.ReadBytes(Inventory::ItemDataSize);
    if (!item)
        return Core::Unexpected(ProtocolError::TooShort);
    pickUp.item = ItemFrom(*item);
    if (*result == ZenPickedUp)
    {
        // 0x4d0d9b..0x4d0dc7: pkt[4] es el byte alto.
        for (std::size_t index = 0; index < 4; ++index)
            pickUp.zen = (pickUp.zen << 8) | std::to_integer<std::uint32_t>(pickUp.item[index]);
    }
    return pickUp;
}

Core::Expected<DropResult, ProtocolError> ReadDropResult(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C1, DropOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto result = reader.ReadU8();
    if (!result)
        return Core::Unexpected(ProtocolError::TooShort);
    // 0x4d10d9..0x4d10e4: pkt[4] solo con pkt[3] distinto de 0.
    const auto slot = reader.ReadU8();
    if (!slot && *result != DropRejected)
        return Core::Unexpected(ProtocolError::TooShort);
    return DropResult{.result = *result, .slot = slot.value_or(0)};
}

Core::Expected<ShortDropResult, ProtocolError> ReadShortDropResult(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C1, DropOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    return ShortDropResult{.result = fields.Value().ReadU8()};
}

Core::Expected<StatFruit, ProtocolError> ReadStatFruit(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C1, StatFruitOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    // 0x4de50f..0x4de51a: los tres bytes, siempre, antes del switch.
    const auto result = reader.ReadU8();
    const auto points = reader.ReadU8();
    const auto stat = reader.ReadU8();
    if (!result || !points || !stat)
        return Core::Unexpected(ProtocolError::TooShort);
    return StatFruit{.result = *result, .points = *points, .stat = *stat};
}

Outgoing WritePickUp(std::uint16_t index)
{
    return EncryptedC1(Network::PacketWriter::C1(PickUpOpcode).PutU16Be(index));
}

Outgoing WriteDrop(const Drop& drop)
{
    return EncryptedC1(Network::PacketWriter::C1(DropOpcode).PutU8(drop.x).PutU8(drop.y).PutU8(drop.slot));
}

Outgoing WriteUseItem(const UseItem& use)
{
    return EncryptedC1(Network::PacketWriter::C1(UseItemOpcode).PutU8(use.slot).PutU8(use.target).PutU8(use.kind));
}
} // namespace Client::Protocol::FloorItems
