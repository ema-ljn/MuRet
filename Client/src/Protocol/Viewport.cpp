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
#include <utility>
#include <vector>

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
// 0x4c9c81..0x4c9cdb: la clave, el tipo alto entero y el tipo; 0x4c9cee los estados; la celda, el blanco y la
// dirección.
MonsterRecord ReadMonsterRecord(std::span<const std::byte> record)
{
    constexpr std::size_t EffectsOffset = 4;
    constexpr std::size_t CellOffset = 8;
    constexpr std::size_t TargetOffset = 0xa;
    constexpr std::size_t DirectionOffset = 0xc;
    constexpr std::uint8_t TypeHighMask = 3;
    Core::ByteReader reader(record);
    MonsterRecord read;
    const auto [key, isNew] = SplitKey(reader.ReadU16Be().value_or(0));
    read.key = key;
    read.isNew = isNew;
    read.typeHigh = std::to_integer<std::uint8_t>(record[2]);
    read.type =
        static_cast<std::uint16_t>((read.typeHigh & TypeHighMask) << 8 | std::to_integer<std::uint8_t>(record[3]));
    Core::ByteReader effects(record.subspan(EffectsOffset));
    read.effects = effects.ReadU32Le().value_or(0);
    read.x = std::to_integer<std::uint8_t>(record[CellOffset]);
    read.y = std::to_integer<std::uint8_t>(record[CellOffset + 1]);
    read.targetX = std::to_integer<std::uint8_t>(record[TargetOffset]);
    read.targetY = std::to_integer<std::uint8_t>(record[TargetOffset + 1]);
    read.direction = std::to_integer<std::uint8_t>(record[DirectionOffset]);
    return read;
}

// 0x4ca0d7..0x4ca10c y lo que lee después: el tipo de 16 bits y el nombre del dueño en +0xd (0x4ca547 y 0x4ca132).
SummonRecord ReadSummonRecord(std::span<const std::byte> record)
{
    constexpr std::size_t TypeOffset = 2;
    constexpr std::size_t EffectsOffset = 4;
    constexpr std::size_t CellOffset = 8;
    constexpr std::size_t TargetOffset = 0xa;
    constexpr std::size_t DirectionOffset = 0xc;
    constexpr std::size_t OwnerOffset = 0xd;
    Core::ByteReader reader(record);
    SummonRecord read;
    const auto [key, isNew] = SplitKey(reader.ReadU16Be().value_or(0));
    read.key = key;
    read.isNew = isNew;
    Core::ByteReader type(record.subspan(TypeOffset));
    read.type = type.ReadU16Be().value_or(0);
    Core::ByteReader effects(record.subspan(EffectsOffset));
    read.effects = effects.ReadU32Le().value_or(0);
    read.x = std::to_integer<std::uint8_t>(record[CellOffset]);
    read.y = std::to_integer<std::uint8_t>(record[CellOffset + 1]);
    read.targetX = std::to_integer<std::uint8_t>(record[TargetOffset]);
    read.targetY = std::to_integer<std::uint8_t>(record[TargetOffset + 1]);
    read.directionAndPk = std::to_integer<std::uint8_t>(record[DirectionOffset]);
    CopyInto(read.owner, record.subspan(OwnerOffset));
    return read;
}

// 0x4e09a4..0x4e09b9: el tipo de registro, la clase o el tipo, la clave, la celda, el equipo y los estados.
ObjectRecord ReadObjectRecord(std::span<const std::byte> record)
{
    constexpr std::size_t KeyOffset = 3;
    constexpr std::size_t CellOffset = 5;
    constexpr std::size_t EquipmentOffset = 7;
    constexpr std::size_t EffectsOffset = 0x18;
    ObjectRecord read;
    read.kind = std::to_integer<std::uint8_t>(record[0]);
    read.classOrTypeHigh = std::to_integer<std::uint8_t>(record[1]);
    read.typeLow = std::to_integer<std::uint8_t>(record[2]);
    Core::ByteReader key(record.subspan(KeyOffset));
    read.key = static_cast<std::uint16_t>(key.ReadU16Be().value_or(0) & KeyMask);
    read.x = std::to_integer<std::uint8_t>(record[CellOffset]);
    read.y = std::to_integer<std::uint8_t>(record[CellOffset + 1]);
    CopyInto(read.equipment, record.subspan(EquipmentOffset));
    Core::ByteReader effects(record.subspan(EffectsOffset));
    read.effects = effects.ReadU32Le().value_or(0);
    return read;
}

// Los `count` registros de un C2 con la cuenta en pkt[4], leídos con `read`.
template <typename Record, typename Reader>
Core::Expected<std::vector<Record>, ProtocolError> ReadRecords(const Network::Packet& packet, std::uint8_t opcode,
                                                               std::size_t size, std::size_t lastRead, Reader read)
{
    auto fields = FieldsOf(packet, Network::PacketType::C2, opcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    const auto count = fields.Value().ReadU8();
    if (!count)
        return Core::Unexpected(ProtocolError::TooShort);
    const auto records = RecordsOf(packet, *count, size, lastRead);
    if (!records.HasValue())
        return Core::Unexpected(records.Error());
    std::vector<Record> out;
    out.reserve(*count);
    for (std::size_t index = 0; index < *count; ++index)
        out.push_back(read(records.Value().subspan(index * size)));
    return out;
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

namespace Client::Protocol::Viewport
{
Core::Expected<Monsters, ProtocolError> ReadMonsters(const Network::Packet& packet)
{
    // Hasta +0xc, la dirección (0x4c9d3c).
    constexpr std::size_t LastRead = 0xd;
    auto records = ReadRecords<MonsterRecord>(packet, MonstersOpcode, MonsterRecordSize, LastRead, ReadMonsterRecord);
    if (!records.HasValue())
        return Core::Unexpected(records.Error());
    return Monsters{std::move(records).Value()};
}

Core::Expected<Summons, ProtocolError> ReadSummons(const Network::Packet& packet)
{
    // Hasta +0x16, el último byte del dueño.
    constexpr std::size_t LastRead = 0x17;
    auto records = ReadRecords<SummonRecord>(packet, SummonsOpcode, SummonRecordSize, LastRead, ReadSummonRecord);
    if (!records.HasValue())
        return Core::Unexpected(records.Error());
    return Summons{std::move(records).Value()};
}

Core::Expected<Objects, ProtocolError> ReadObjects(const Network::Packet& packet)
{
    // Hasta +0x1b, el último byte de los estados.
    constexpr std::size_t LastRead = ObjectRecordSize;
    auto records = ReadRecords<ObjectRecord>(packet, ObjectsOpcode, ObjectRecordSize, LastRead, ReadObjectRecord);
    if (!records.HasValue())
        return Core::Unexpected(records.Error());
    return Objects{std::move(records).Value()};
}

Core::Expected<MagicAttackCount, ProtocolError> ReadMagicAttackCount(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C1, MagicAttackCountOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto key = reader.ReadU16Be();
    const auto magic = reader.ReadU8();
    // pkt[6] y pkt[7], la celda.
    constexpr std::size_t CellBytes = 2;
    const auto cell = reader.ReadBytes(CellBytes);
    const auto count = reader.ReadU8();
    if (!key || !magic || !cell || !count)
        return Core::Unexpected(ProtocolError::TooShort);
    MagicAttackCount attack{.key = *key, .magic = *magic, .targets = {}};
    attack.targets.reserve(*count);
    for (std::size_t index = 0; index < *count; ++index)
    {
        const auto target = reader.ReadU16Be();
        if (!target)
            return Core::Unexpected(ProtocolError::TooShort);
        attack.targets.push_back(*target);
    }
    return attack;
}
} // namespace Client::Protocol::Viewport
