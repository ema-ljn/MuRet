#include "Protocol/Items/Inventory.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"
#include "Network/PacketFields.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Protocol::Items
{
namespace
{
constexpr Network::PacketKind EquipmentChangeKind{Network::PacketType::C1, CharacterOpcode,
                                                  Character::EquipmentChangeSubcode};
constexpr Network::PacketKind ItemPutKind{Network::PacketType::C1, CharacterOpcode, Character::ItemPutSubcode};
// 0x4e316e: pkt[6] no lo lee nadie.
constexpr std::size_t EquipmentChangeGap = 1;

// Un C1 de `opcode`: el lector desde pkt[3].
Core::Expected<Core::ByteReader, Network::ProtocolError> FieldsOf(const Network::Packet& packet, Network::Opcode opcode)
{
    if (packet.Type() != Network::PacketType::C1 || packet.Opcode() != opcode)
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    return Core::ByteReader(packet.Bytes().subspan(Network::MinPacketSize(Network::PacketType::C1)));
}

std::optional<ItemData> ReadItem(Core::ByteReader& reader)
{
    const auto bytes = reader.ReadBytes(ItemDataSize);
    if (!bytes)
        return std::nullopt;
    ItemData item{};
    std::ranges::copy(*bytes, item.begin());
    return item;
}

// 0x4d36eb..0x4d3719 y 0x4d3a0c..0x4d3a5d: los lugares que traen los valores en vez de ser una casilla.
bool CarriesTheValues(std::uint8_t place)
{
    return place == MaximumPlace || place == CurrentPlace;
}
} // namespace

// 0x449d80, y en línea 0x5ca46c.. y 0x5bf294..: el tipo, el opcode y doce bytes, con el XOR32 desde pkt[3].
Outgoing WriteItemMove(const ItemMove& move)
{
    return EncryptedC1(Network::PacketWriter::C1(ItemMoveOpcode)
                           .PutU8(move.sourceFlag)
                           .PutU8(move.sourceSlot)
                           .PutBytes(move.item)
                           .PutU8(move.targetFlag)
                           .PutU8(move.targetSlot));
}

Outgoing WriteClearItemMoveReport()
{
    return EncryptedC1(
        Network::PacketWriter::C1(JoinOpcode, Join::DecryptErrorSubcode).PutU8(0).PutU8(ClearItemMoveHead));
}

Core::Expected<ItemMoveResult, Network::ProtocolError> ReadItemMoveResult(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, ItemMoveOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto result = reader.ReadU8();
    const auto slot = reader.ReadU8();
    const auto item = ReadItem(reader);
    if (!result || !slot || !item)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return ItemMoveResult{*result, *slot, *item};
}

Core::Expected<ItemDelete, Network::ProtocolError> ReadItemDelete(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, ItemDeleteOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto slot = reader.ReadU8();
    const auto ends = reader.ReadU8();
    if (!slot || !ends)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return ItemDelete{*slot, *ends};
}

Core::Expected<LifeRefill, Network::ProtocolError> ReadLifeRefill(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, LifeOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto place = reader.ReadU8();
    if (!place)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    if (!CarriesTheValues(*place))
        return LifeRefill{.place = *place, .life = 0};
    const auto life = reader.ReadU16Be();
    if (!life)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return LifeRefill{.place = *place, .life = *life};
}

Core::Expected<ManaRefill, Network::ProtocolError> ReadManaRefill(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, ManaOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto place = reader.ReadU8();
    const auto mana = reader.ReadU16Be();
    if (!place || !mana)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    if (!CarriesTheValues(*place))
        return ManaRefill{.place = *place, .mana = *mana, .bp = 0};
    const auto bp = reader.ReadU16Be();
    if (!bp)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return ManaRefill{.place = *place, .mana = *mana, .bp = *bp};
}

Core::Expected<Durability, Network::ProtocolError> ReadDurability(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, DurabilityOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto slot = reader.ReadU8();
    const auto durability = reader.ReadU8();
    const auto ends = reader.ReadU8();
    if (!slot || !durability || !ends)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return Durability{*slot, *durability, *ends};
}

Core::Expected<EquipmentChange, Network::ProtocolError> ReadEquipmentChange(const Network::Packet& packet)
{
    auto fields = Network::FieldsAfterSubcode(packet, EquipmentChangeKind);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto high = reader.ReadU8();
    const auto low = reader.ReadU8();
    const auto gap = reader.ReadBytes(EquipmentChangeGap);
    const auto equipment = reader.ReadBytes(EquipmentChangeSize);
    if (!high || !low || !gap || !equipment)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    EquipmentChange change{};
    change.key = static_cast<std::uint16_t>((*high << 8) | *low);
    std::ranges::copy(*equipment, change.equipment.begin());
    return change;
}

Core::Expected<ItemPut, Network::ProtocolError> ReadItemPut(const Network::Packet& packet)
{
    auto fields = Network::FieldsAfterSubcode(packet, ItemPutKind);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto slot = reader.ReadU8();
    const auto item = ReadItem(reader);
    if (!slot || !item)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return ItemPut{*slot, *item};
}
} // namespace Client::Protocol::Items
