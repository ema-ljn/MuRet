#include "Protocol/Viewport.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Protocol::Viewport
{
namespace
{
// Los registros del 0x12 y del 0x45 empiezan en pkt[5] (0x4c886b, 0x4c92be).
constexpr std::size_t C2RecordsOffset = 5;
// Lo que lee de cada registro, hasta su último byte leído (+0x28 en el 0x12, +0x18 en el 0x45).
constexpr std::size_t PlayerRecordRead = 0x29;
constexpr std::size_t TransformedRecordRead = 0x19;

struct Key
{
    std::uint16_t key;
    bool isNew;
};

Key SplitKey(std::uint16_t word)
{
    return {static_cast<std::uint16_t>(word & KeyMask), (word & NewBit) != 0};
}

// Un paquete de `type` y `opcode`: el lector desde el primer byte después del opcode.
Core::Expected<Core::ByteReader, ProtocolError> FieldsOf(const Network::Packet& packet, Network::PacketType type,
                                                         std::uint8_t opcode)
{
    if (packet.Type() != type || packet.Opcode() != opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    return Core::ByteReader(packet.Bytes().subspan(Network::MinPacketSize(type)));
}

// Los `count` registros de `size` bytes desde pkt[5], con al menos `read` bytes el último.
Core::Expected<std::span<const std::byte>, ProtocolError> RecordsOf(const Network::Packet& packet, std::size_t count,
                                                                    std::size_t size, std::size_t read)
{
    const std::span<const std::byte> bytes = packet.Bytes();
    if (count == 0)
        return std::span<const std::byte>{};
    const std::size_t needed = C2RecordsOffset + (count - 1) * size + read;
    if (bytes.size() < needed)
        return Core::Unexpected(ProtocolError::TooShort);
    return bytes.subspan(C2RecordsOffset);
}

template <std::size_t Size> void CopyInto(std::array<std::byte, Size>& out, std::span<const std::byte> bytes)
{
    std::ranges::copy(bytes.first(Size), out.begin());
}

PlayerRecord ReadPlayerRecord(std::span<const std::byte> record)
{
    constexpr std::size_t EquipmentOffset = 5;
    constexpr std::size_t EffectsOffset = 0x18;
    constexpr std::size_t NameOffset = 0x1c;
    constexpr std::size_t TargetOffset = 0x26;
    constexpr std::size_t DirectionOffset = 0x28;
    Core::ByteReader reader(record);
    PlayerRecord read;
    const auto [key, isNew] = SplitKey(reader.ReadU16Be().value_or(0));
    read.key = key;
    read.isNew = isNew;
    read.x = std::to_integer<std::uint8_t>(record[2]);
    read.y = std::to_integer<std::uint8_t>(record[3]);
    read.classAndState = std::to_integer<std::uint8_t>(record[4]);
    CopyInto(read.equipment, record.subspan(EquipmentOffset));
    Core::ByteReader effects(record.subspan(EffectsOffset));
    read.effects = effects.ReadU32Le().value_or(0);
    CopyInto(read.name, record.subspan(NameOffset));
    read.targetX = std::to_integer<std::uint8_t>(record[TargetOffset]);
    read.targetY = std::to_integer<std::uint8_t>(record[TargetOffset + 1]);
    read.directionAndPk = std::to_integer<std::uint8_t>(record[DirectionOffset]);
    return read;
}

TransformedRecord ReadTransformedRecord(std::span<const std::byte> record)
{
    constexpr std::size_t TypeOffset = 4;
    constexpr std::size_t EffectsOffset = 8;
    constexpr std::size_t NameOffset = 0xc;
    constexpr std::size_t TargetOffset = 0x16;
    constexpr std::size_t DirectionOffset = 0x18;
    Core::ByteReader reader(record);
    TransformedRecord read;
    const auto [key, isNew] = SplitKey(reader.ReadU16Be().value_or(0));
    read.key = key;
    read.isNew = isNew;
    read.x = std::to_integer<std::uint8_t>(record[2]);
    read.y = std::to_integer<std::uint8_t>(record[3]);
    Core::ByteReader type(record.subspan(TypeOffset));
    read.type = type.ReadU16Be().value_or(0);
    Core::ByteReader effects(record.subspan(EffectsOffset));
    read.effects = effects.ReadU32Le().value_or(0);
    CopyInto(read.name, record.subspan(NameOffset));
    read.targetX = std::to_integer<std::uint8_t>(record[TargetOffset]);
    read.targetY = std::to_integer<std::uint8_t>(record[TargetOffset + 1]);
    read.directionAndPk = std::to_integer<std::uint8_t>(record[DirectionOffset]);
    return read;
}
} // namespace

Core::Expected<Players, ProtocolError> ReadPlayers(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C2, PlayersOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    // pkt[4], el primer byte después del opcode (0x4c8870).
    const auto count = fields.Value().ReadU8();
    if (!count)
        return Core::Unexpected(ProtocolError::TooShort);
    const auto records = RecordsOf(packet, *count, PlayerRecordSize, PlayerRecordRead);
    if (!records.HasValue())
        return Core::Unexpected(records.Error());
    Players players;
    players.records.reserve(*count);
    for (std::size_t index = 0; index < *count; ++index)
        players.records.push_back(ReadPlayerRecord(records.Value().subspan(index * PlayerRecordSize)));
    const std::span<const std::byte> bytes = packet.Bytes();
    players.packet.assign(bytes.begin(), bytes.end());
    return players;
}

Core::Expected<Leave, ProtocolError> ReadLeave(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C1, LeaveOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto count = reader.ReadU8();
    if (!count)
        return Core::Unexpected(ProtocolError::TooShort);
    Leave leave;
    leave.keys.reserve(*count);
    for (std::size_t index = 0; index < *count; ++index)
    {
        const auto key = reader.ReadU16Be();
        if (!key)
            return Core::Unexpected(ProtocolError::TooShort);
        leave.keys.push_back(*key);
    }
    return leave;
}

Core::Expected<Transformed, ProtocolError> ReadTransformed(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C2, TransformedOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    // pkt[4] (0x4c92b1).
    const auto count = fields.Value().ReadU8();
    if (!count)
        return Core::Unexpected(ProtocolError::TooShort);
    const auto records = RecordsOf(packet, *count, TransformedRecordSize, TransformedRecordRead);
    if (!records.HasValue())
        return Core::Unexpected(records.Error());
    Transformed transformed;
    transformed.records.reserve(*count);
    for (std::size_t index = 0; index < *count; ++index)
        transformed.records.push_back(ReadTransformedRecord(records.Value().subspan(index * TransformedRecordSize)));
    return transformed;
}

Core::Expected<EquipmentShown, ProtocolError> ReadEquipmentShown(const Network::Packet& packet)
{
    // pkt[5..10]: el ítem hasta el byte 5, el último que lee 0x54b3a0.
    constexpr std::size_t ReadItemBytes = 6;
    auto fields = FieldsOf(packet, Network::PacketType::C1, EquipmentShownOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto key = reader.ReadU16Be();
    const auto item = reader.ReadBytes(ReadItemBytes);
    if (!key || !item)
        return Core::Unexpected(ProtocolError::TooShort);
    EquipmentShown shown;
    shown.key = *key;
    std::ranges::copy(*item, shown.item.begin());
    return shown;
}

Core::Expected<Position, ProtocolError> ReadPosition(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C1, PositionOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto key = reader.ReadU16Be();
    const auto x = reader.ReadU8();
    const auto y = reader.ReadU8();
    if (!key || !x || !y)
        return Core::Unexpected(ProtocolError::TooShort);
    return Position{*key, *x, *y};
}
} // namespace Client::Protocol::Viewport
