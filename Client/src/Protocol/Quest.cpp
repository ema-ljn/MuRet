#include "Protocol/Quest.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>
#include <vector>

namespace Client::Protocol::Quest
{
namespace
{
// Un C1 de `opcode`: el lector desde pkt[3].
Core::Expected<Core::ByteReader, ProtocolError> FieldsOf(const Network::Packet& packet, std::uint8_t opcode)
{
    if (packet.Type() != Network::PacketType::C1 || packet.Opcode() != opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    return Core::ByteReader(packet.Bytes().subspan(Network::MinPacketSize(Network::PacketType::C1)));
}

// 0x4ddb80: (pkt[3] · 0x100 + pkt[4]) & 0x7fff.
constexpr std::uint16_t KeyMask = 0x7fff;
} // namespace

Core::Expected<QuestFlag, ProtocolError> ReadQuestFlag(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, QuestFlagOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto index = reader.ReadU8();
    const auto value = reader.ReadU8();
    if (!index || !value)
        return Core::Unexpected(ProtocolError::TooShort);
    return QuestFlag{*index, *value};
}

Core::Expected<QuestList, ProtocolError> ReadQuestList(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, QuestListOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto count = reader.ReadU8();
    if (!count)
        return Core::Unexpected(ProtocolError::TooShort);
    QuestList list{.count = *count, .states = {}};
    while (const auto state = reader.ReadU8())
        list.states.push_back(*state);
    return list;
}

Core::Expected<QuestStateChange, ProtocolError> ReadQuestState(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, QuestStateOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto index = reader.ReadU8();
    const auto state = reader.ReadU8();
    if (!index || !state)
        return Core::Unexpected(ProtocolError::TooShort);
    return QuestStateChange{*index, *state};
}

Core::Expected<SetQuestResult, ProtocolError> ReadSetQuestResult(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, SetQuestOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto index = reader.ReadU8();
    const auto result = reader.ReadU8();
    const auto state = reader.ReadU8();
    if (!index || !result || !state)
        return Core::Unexpected(ProtocolError::TooShort);
    return SetQuestResult{*index, *result, *state};
}

Core::Expected<QuestResult, ProtocolError> ReadQuestResult(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, QuestResultOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto high = reader.ReadU8();
    const auto low = reader.ReadU8();
    const auto kind = reader.ReadU8();
    const auto value = reader.ReadU8();
    if (!high || !low || !kind || !value)
        return Core::Unexpected(ProtocolError::TooShort);
    const auto key = static_cast<std::uint16_t>(((*high << 8) + *low) & KeyMask);
    return QuestResult{key, *kind, *value};
}

Outgoing WriteQuestListRequest()
{
    return EncryptedC1(Network::PacketWriter::C1(QuestListOpcode));
}

Outgoing WriteSetQuest(std::uint8_t quest, std::uint8_t state)
{
    return EncryptedC1(Network::PacketWriter::C1(SetQuestOpcode).PutU8(quest).PutU8(state));
}

Outgoing WriteTalk(std::int16_t key)
{
    // 0x567cca..0x567ccd: MOV AX, word; SAR EAX, 8 (el byte alto con signo); después el bajo.
    const auto high = static_cast<std::uint8_t>((static_cast<std::int32_t>(key) >> 8) & 0xff);
    const auto low = static_cast<std::uint8_t>(static_cast<std::uint16_t>(key) & 0xffu);
    return EncryptedC1(Network::PacketWriter::C1(TalkOpcode).PutU8(high).PutU8(low));
}
} // namespace Client::Protocol::Quest
