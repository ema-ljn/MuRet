#include "Protocol/Duel.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace Client::Protocol::Duel
{
namespace
{
Core::Expected<Core::ByteReader, ProtocolError> FieldsOf(const Network::Packet& packet, std::uint8_t opcode)
{
    if (packet.Type() != Network::PacketType::C1 || packet.Opcode() != opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    return Core::ByteReader(packet.Bytes().subspan(Network::MinPacketSize(Network::PacketType::C1)));
}

std::optional<Name> ReadName(Core::ByteReader& reader)
{
    const auto bytes = reader.ReadBytes(Character::NameSize);
    if (!bytes)
        return std::nullopt;
    Name name{};
    std::ranges::copy(*bytes, name.begin());
    return name;
}

template <typename Peer>
Core::Expected<Peer, ProtocolError> ReadPeer(const Network::Packet& packet, std::uint8_t opcode)
{
    auto fields = FieldsOf(packet, opcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto key = reader.ReadU16Be();
    const auto name = ReadName(reader);
    if (!key || !name)
        return Core::Unexpected(ProtocolError::TooShort);
    return Peer{*key, *name};
}
} // namespace

Core::Expected<DuelStart, ProtocolError> ReadDuelStart(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, StartOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto result = reader.ReadU8();
    const auto key = reader.ReadU16Be();
    const auto name = ReadName(reader);
    if (!result || !key || !name)
        return Core::Unexpected(ProtocolError::TooShort);
    return DuelStart{*result, *key, *name};
}

Core::Expected<DuelEnd, ProtocolError> ReadDuelEnd(const Network::Packet& packet)
{
    return ReadPeer<DuelEnd>(packet, EndOpcode);
}

Core::Expected<DuelRequest, ProtocolError> ReadDuelRequest(const Network::Packet& packet)
{
    return ReadPeer<DuelRequest>(packet, RequestOpcode);
}

Core::Expected<DuelScore, ProtocolError> ReadDuelScore(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, ScoreOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto first = reader.ReadU16Be();
    const auto second = reader.ReadU16Be();
    const auto firstScore = reader.ReadU8();
    const auto secondScore = reader.ReadU8();
    if (!first || !second || !firstScore || !secondScore)
        return Core::Unexpected(ProtocolError::TooShort);
    return DuelScore{*first, *second, *firstScore, *secondScore};
}

Outgoing WriteDuelStart(std::uint16_t key, const Name& name)
{
    return EncryptedC1(Network::PacketWriter::C1(StartOpcode).PutU16Be(key).PutBytes(name));
}

Outgoing WriteDuelEnd()
{
    return EncryptedC1(Network::PacketWriter::C1(EndOpcode));
}

Outgoing WriteDuelAnswer(std::uint8_t accepted, std::uint16_t key, const Name& name)
{
    return EncryptedC1(Network::PacketWriter::C1(RequestOpcode).PutU8(accepted).PutU16Be(key).PutBytes(name));
}
} // namespace Client::Protocol::Duel
