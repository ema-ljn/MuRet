#include "Protocol/Actors/Viewport.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/ProtocolError.h"
#include "Network/ReadList.h"
#include "Protocol/Opcodes.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Client::Protocol::Actors
{
namespace
{
// 0x4c886b and 0x4c92be: the C2 lists have their count at pkt[4] and their records from pkt[5].
using RecordCount = Network::Field<std::uint8_t, 4>;
// Up to the last byte each reader reads of a record: +0x28 in the 12, +0x18 in the 45, +0xc in the 13 (0x4c9d3c),
// +0x16 in the 1F (the owner) and +0x1b in the 68 (the effects).
constexpr std::size_t PlayerRecordRead = 0x29;
constexpr std::size_t TransformedRecordRead = 0x19;
constexpr std::size_t MonsterRecordRead = 0xd;
constexpr std::size_t SummonRecordRead = 0x17;
constexpr std::size_t KeySize = 2;

constexpr Network::PacketLayout PlayersLayout{
    {Network::PacketType::C2, ViewportPlayersOpcode, std::nullopt}, RecordCount::End, 0x4c8860};
constexpr Network::PacketLayout TransformedLayout{
    {Network::PacketType::C2, TransformedOpcode, std::nullopt}, RecordCount::End, 0x4c92a0};
constexpr Network::PacketLayout MonstersLayout{
    {Network::PacketType::C2, ViewportMonstersOpcode, std::nullopt}, RecordCount::End, 0x4c9c60};
constexpr Network::PacketLayout SummonsLayout{
    {Network::PacketType::C2, ViewportSummonsOpcode, std::nullopt}, RecordCount::End, 0x4ca0b0};
constexpr Network::PacketLayout ObjectsLayout{
    {Network::PacketType::C2, ViewportObjectsOpcode, std::nullopt}, RecordCount::End, 0x4e0980};

using LeaveCount = Network::Field<std::uint8_t, 3>;
constexpr Network::PacketLayout LeaveLayout{
    {Network::PacketType::C1, ViewportLeaveOpcode, std::nullopt}, LeaveCount::End, 0x4ca780};

using ShownKey = Network::Field<std::uint16_t, 3, std::endian::big>;
// pkt[5..10]: the item up to its byte 5, the last one 0x54b3a0 reads.
constexpr std::size_t ShownItemAt = 5;
constexpr std::size_t ShownItemRead = 6;
constexpr Network::PacketLayout EquipmentShownLayout{
    {Network::PacketType::C1, EquipmentShownOpcode, std::nullopt}, ShownItemAt + ShownItemRead, 0x4c8380};

using PositionKey = Network::Field<std::uint16_t, 3, std::endian::big>;
using PositionX = Network::Field<std::uint8_t, 5>;
using PositionY = Network::Field<std::uint8_t, 6>;
constexpr Network::PacketLayout PositionLayout{
    {Network::PacketType::C1, PositionOpcode, std::nullopt}, PositionY::End, 0x4c6b40};

// Nobody reads the cell at pkt[6] and pkt[7].
using CasterKey = Network::Field<std::uint16_t, 3, std::endian::big>;
using CasterMagic = Network::Field<std::uint8_t, 5>;
using TargetCount = Network::Field<std::uint8_t, 8>;
constexpr Network::PacketLayout MagicAttackCountLayout{
    {Network::PacketType::C1, MagicAttackCountOpcode, std::nullopt}, TargetCount::End, 0x4cedd0};

struct Key
{
    std::uint16_t key;
    bool isNew;
};

Key SplitKey(std::uint16_t word)
{
    return {static_cast<std::uint16_t>(word & KeyMask), (word & NewBit) != 0};
}

std::uint16_t WholeKey(std::span<const std::byte, KeySize> bytes)
{
    return Core::ByteReader(bytes).ReadU16Be().value_or(0);
}

template <std::size_t Size> void CopyInto(std::array<std::byte, Size>& out, std::span<const std::byte> bytes)
{
    std::ranges::copy(bytes.first(Size), out.begin());
}

PlayerRecord ReadPlayerRecord(std::span<const std::byte, PlayerRecordRead> record)
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

TransformedRecord ReadTransformedRecord(std::span<const std::byte, TransformedRecordRead> record)
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

// 0x4c9c81..0x4c9cdb, then the effects (0x4c9cee), the cell, the target and the direction.
MonsterRecord ReadMonsterRecord(std::span<const std::byte, MonsterRecordRead> record)
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

// 0x4ca0d7..0x4ca10c, the type (0x4ca547) and the owner (0x4ca132).
SummonRecord ReadSummonRecord(std::span<const std::byte, SummonRecordRead> record)
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

// 0x4e09a4..0x4e09b9.
ObjectRecord ReadObjectRecord(std::span<const std::byte, ObjectRecordSize> record)
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

template <std::size_t Size, std::size_t Read, typename ReadOne>
auto ReadRecords(Network::LayoutReader& reader, ReadOne readOne)
{
    return Network::ReadList<Size, Read>(reader, RecordCount::End, reader.Read<RecordCount>(), readOne);
}
} // namespace

Core::Expected<Players, Network::ProtocolError> ReadPlayers(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, PlayersLayout,
                               [&packet](Network::LayoutReader& reader)
                               {
                                   const std::span<const std::byte> bytes = packet.Bytes();
                                   return Players{
                                       ReadRecords<PlayerRecordSize, PlayerRecordRead>(reader, ReadPlayerRecord),
                                       std::vector<std::byte>(bytes.begin(), bytes.end())};
                               });
}

Core::Expected<Leave, Network::ProtocolError> ReadLeave(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, LeaveLayout, [](Network::LayoutReader& reader)
        { return Leave{Network::ReadList<KeySize>(reader, LeaveCount::End, reader.Read<LeaveCount>(), WholeKey)}; });
}

Core::Expected<Transformed, Network::ProtocolError> ReadTransformed(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, TransformedLayout,
                               [](Network::LayoutReader& reader) {
                                   return Transformed{ReadRecords<TransformedRecordSize, TransformedRecordRead>(
                                       reader, ReadTransformedRecord)};
                               });
}

Core::Expected<EquipmentShown, Network::ProtocolError> ReadEquipmentShown(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, EquipmentShownLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   EquipmentShown shown{.key = reader.Read<ShownKey>()};
                                   std::ranges::copy(reader.Bytes(ShownItemAt, ShownItemRead), shown.item.begin());
                                   return shown;
                               });
}

Core::Expected<Position, Network::ProtocolError> ReadPosition(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, PositionLayout, [](Network::LayoutReader& reader)
        { return Position{reader.Read<PositionKey>(), reader.Read<PositionX>(), reader.Read<PositionY>()}; });
}

Core::Expected<Monsters, Network::ProtocolError> ReadMonsters(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, MonstersLayout, [](Network::LayoutReader& reader)
        { return Monsters{ReadRecords<MonsterRecordSize, MonsterRecordRead>(reader, ReadMonsterRecord)}; });
}

Core::Expected<Summons, Network::ProtocolError> ReadSummons(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, SummonsLayout, [](Network::LayoutReader& reader)
        { return Summons{ReadRecords<SummonRecordSize, SummonRecordRead>(reader, ReadSummonRecord)}; });
}

Core::Expected<Objects, Network::ProtocolError> ReadObjects(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, ObjectsLayout, [](Network::LayoutReader& reader)
        { return Objects{ReadRecords<ObjectRecordSize, ObjectRecordSize>(reader, ReadObjectRecord)}; });
}

Core::Expected<MagicAttackCount, Network::ProtocolError> ReadMagicAttackCount(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, MagicAttackCountLayout,
        [](Network::LayoutReader& reader)
        {
            return MagicAttackCount{
                .key = reader.Read<CasterKey>(),
                .magic = reader.Read<CasterMagic>(),
                .targets = Network::ReadList<KeySize>(reader, TargetCount::End, reader.Read<TargetCount>(), WholeKey)};
        });
}
} // namespace Client::Protocol::Actors
