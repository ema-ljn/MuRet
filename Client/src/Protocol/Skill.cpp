#include "Protocol/Skill.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PacketFields.h"
#include "Protocol/ProtocolError.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::Protocol::Skill
{
namespace
{
constexpr std::uint16_t SucceededBit = 0x8000;
constexpr std::uint16_t TargetMask = 0x7fff;

// Un C1 de `opcode` (o un C3 descifrado, que llega como C1): el lector desde pkt[3].
Core::Expected<Core::ByteReader, ProtocolError> FieldsOf(const Network::Packet& packet, std::uint8_t opcode)
{
    if (packet.Type() != Network::PacketType::C1 || packet.Opcode() != opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    return Core::ByteReader(packet.Bytes().subspan(Network::MinPacketSize(Network::PacketType::C1)));
}

constexpr PacketKind SkillListKind{Network::PacketType::C1, Character::CharacterOpcode, SkillListSubcode};
constexpr PacketKind SummonLifeKind{Network::PacketType::C1, Character::CharacterOpcode, SummonLifeSubcode};
} // namespace

Core::Expected<MagicAttack, ProtocolError> ReadMagicAttack(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, MagicAttackOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto skill = reader.ReadU8();
    const auto source = reader.ReadU16Be();
    const auto target = reader.ReadU16Be();
    if (!skill || !source || !target)
        return Core::Unexpected(ProtocolError::TooShort);
    // 0x4cce2f..0x4cce55: el SAR 0xf del blanco entero y el AND 0x7fff.
    return MagicAttack{*skill, *source, static_cast<std::uint16_t>(*target & TargetMask),
                       (*target & SucceededBit) != 0};
}

Core::Expected<DurationMagic, ProtocolError> ReadDurationMagic(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, DurationMagicOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto skill = reader.ReadU8();
    const auto key = reader.ReadU16Be();
    const auto x = reader.ReadU8();
    const auto y = reader.ReadU8();
    const auto direction = reader.ReadU8();
    if (!skill || !key || !x || !y || !direction)
        return Core::Unexpected(ProtocolError::TooShort);
    return DurationMagic{*skill, *key, *x, *y, *direction};
}

Core::Expected<MagicCancel, ProtocolError> ReadMagicCancel(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, MagicCancelOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto skill = reader.ReadU8();
    const auto key = reader.ReadU16Be();
    if (!skill || !key)
        return Core::Unexpected(ProtocolError::TooShort);
    return MagicCancel{*skill, *key};
}

Core::Expected<ActorState, ProtocolError> ReadActorState(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, ActorStateOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto type = reader.ReadU8();
    // 0x4cbe22: MOV ESI,[pkt+4], el DWORD en little endian.
    const auto mask = reader.ReadU32Le();
    const auto key = reader.ReadU16Be();
    if (!type || !mask || !key)
        return Core::Unexpected(ProtocolError::TooShort);
    return ActorState{*type, *mask, *key};
}

Core::Expected<ActorEffect, ProtocolError> ReadActorEffect(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, ActorEffectOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    // 0x4dbf59..0x4dbf61: AH = pkt[3] y AL = pkt[4].
    const auto key = reader.ReadU16Be();
    const auto kind = reader.ReadU8();
    if (!key || !kind)
        return Core::Unexpected(ProtocolError::TooShort);
    return ActorEffect{*key, *kind};
}

Core::Expected<SkillTarget, ProtocolError> ReadSkillTarget(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, SkillTargetOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto skill = reader.ReadU8();
    // 0x4cc29d y 0x4cc2a3: MOV DI,[pkt+6] y MOV AX,[pkt+4], los WORD en little endian.
    const auto source = reader.ReadU16Le();
    const auto target = reader.ReadU16Le();
    if (!skill || !source || !target)
        return Core::Unexpected(ProtocolError::TooShort);
    return SkillTarget{*skill, *source, *target};
}

Core::Expected<SkillList, ProtocolError> ReadSkillList(const Network::Packet& packet)
{
    auto fields = FieldsAfterSubcode(packet, SkillListKind);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto count = reader.ReadU8();
    const auto list = reader.ReadU8();
    if (!count || !list)
        return Core::Unexpected(ProtocolError::TooShort);
    SkillList read{.count = *count, .list = *list, .slot = 0, .skill = 0, .entries = {}};
    // 0x4c507c, 0x4c5145 y 0x4c522e: cuál de las cuatro formas.
    if (*count == SkillListRemoveOne || *count == SkillListAddOne || *list == SkillListClearSlot)
    {
        const auto slot = reader.ReadU8();
        if (!slot)
            return Core::Unexpected(ProtocolError::TooShort);
        read.slot = *slot;
        if (*count == SkillListAddOne)
        {
            const auto skill = reader.ReadU8();
            if (!skill)
                return Core::Unexpected(ProtocolError::TooShort);
            read.skill = *skill;
        }
        return read;
    }
    // 0x4c537e..0x4c5390: de a tres bytes desde pkt[6], el lugar y la skill.
    constexpr std::size_t EntrySize = 3;
    read.entries.reserve(*count);
    for (std::uint8_t entry = 0; entry < *count; ++entry)
    {
        const auto bytes = reader.ReadBytes(EntrySize);
        if (!bytes)
            return Core::Unexpected(ProtocolError::TooShort);
        read.entries.push_back(
            SkillListEntry{std::to_integer<std::uint8_t>((*bytes)[0]), std::to_integer<std::uint8_t>((*bytes)[1])});
    }
    return read;
}

Core::Expected<SummonLife, ProtocolError> ReadSummonLife(const Network::Packet& packet)
{
    auto fields = FieldsAfterSubcode(packet, SummonLifeKind);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    const auto life = fields.Value().ReadU8();
    if (!life)
        return Core::Unexpected(ProtocolError::TooShort);
    return SummonLife{*life};
}

Outgoing WriteClearSkillReport(std::uint8_t head)
{
    return EncryptedC1(
        Network::PacketWriter::C1(Connection::JoinOpcode).PutU8(Connection::DecryptErrorSubcode).PutU8(0).PutU8(head));
}

Outgoing WriteMagicAttack(std::uint8_t skill, std::uint16_t key)
{
    return EncryptedC1(Network::PacketWriter::C1(MagicAttackOpcode).PutU8(skill).PutU16Be(key));
}

Outgoing WriteDurationMagic(const DurationMagicRequest& request)
{
    return EncryptedC1(Network::PacketWriter::C1(DurationMagicOpcode)
                           .PutU8(request.skill)
                           .PutU8(request.x)
                           .PutU8(request.y)
                           .PutU8(request.direction)
                           .PutU8(request.byte7)
                           .PutU8(request.byte8)
                           .PutU16Be(request.key)
                           .PutU8(request.byte11));
}
} // namespace Client::Protocol::Skill
