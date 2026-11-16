#include "Protocol/Friends.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PacketFields.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Protocol::Friends
{
namespace
{
using NameBytes = std::array<std::byte, FriendNameSize>;

// El paquete del tipo y el opcode, con el lector después del opcode: pkt[3] en un C1, pkt[4] en un C2.
Core::Expected<Core::ByteReader, ProtocolError> FieldsOf(const Network::Packet& packet, Network::PacketType type,
                                                         std::uint8_t opcode)
{
    if (packet.Type() != type || packet.Opcode() != opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    return Core::ByteReader(packet.Bytes().subspan(Network::MinPacketSize(type)));
}

// strncpy de 10 a un buffer de 11 en cero: hasta el primer '\0'.
std::optional<std::string> ReadName(Core::ByteReader& reader)
{
    const auto bytes = reader.ReadBytes(FriendNameSize);
    if (!bytes)
        return std::nullopt;
    return TextField(*bytes);
}

// El nombre en 10 bytes. MuRet completa con ceros lo que el original deja con lo que había en su buffer (Q-C882).
NameBytes NameField(std::string_view name)
{
    NameBytes bytes{};
    const std::size_t end = name.find('\0');
    const std::string_view text = name.substr(0, end);
    for (std::size_t at = 0; at < bytes.size() && at < text.size(); ++at)
        bytes[at] = static_cast<std::byte>(text[at]);
    return bytes;
}
} // namespace

Core::Expected<FriendList, ProtocolError> ReadFriendList(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C2, FriendListOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto letters = reader.ReadU8();
    const auto maxLetters = reader.ReadU8();
    const auto count = reader.ReadU8();
    if (!letters || !maxLetters || !count)
        return Core::Unexpected(ProtocolError::TooShort);
    FriendList list{.letters = *letters, .maxLetters = *maxLetters, .friends = {}};
    list.friends.reserve(*count);
    for (std::uint8_t at = 0; at < *count; ++at)
    {
        auto name = ReadName(reader);
        const auto state = reader.ReadU8();
        if (!name || !state)
            return Core::Unexpected(ProtocolError::TooShort);
        list.friends.push_back(FriendRow{.name = std::move(*name), .state = *state});
    }
    return list;
}

Core::Expected<FriendAddResult, ProtocolError> ReadFriendAddResult(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C1, FriendAddOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto result = reader.ReadU8();
    auto name = ReadName(reader);
    const auto state = reader.ReadU8();
    if (!result || !name || !state)
        return Core::Unexpected(ProtocolError::TooShort);
    return FriendAddResult{.result = *result, .name = std::move(*name), .state = *state};
}

Core::Expected<FriendRequest, ProtocolError> ReadFriendRequest(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C1, FriendRequestOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    auto name = ReadName(fields.Value());
    if (!name)
        return Core::Unexpected(ProtocolError::TooShort);
    return FriendRequest{.name = std::move(*name)};
}

Core::Expected<FriendDeleteResult, ProtocolError> ReadFriendDeleteResult(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C1, FriendDeleteOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto result = reader.ReadU8();
    auto name = ReadName(reader);
    if (!result || !name)
        return Core::Unexpected(ProtocolError::TooShort);
    return FriendDeleteResult{.result = *result, .name = std::move(*name)};
}

Core::Expected<FriendState, ProtocolError> ReadFriendState(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, Network::PacketType::C1, FriendStateOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    auto name = ReadName(reader);
    const auto state = reader.ReadU8();
    if (!name || !state)
        return Core::Unexpected(ProtocolError::TooShort);
    return FriendState{.name = std::move(*name), .state = *state};
}

Outgoing WriteFriendListRequest()
{
    return ClearC1(Network::PacketWriter::C1(FriendListOpcode));
}

Outgoing WriteFriendAdd(std::string_view name)
{
    const NameBytes bytes = NameField(name);
    return Xor32C1(Network::PacketWriter::C1(FriendAddOpcode).PutBytes(bytes));
}

Outgoing WriteFriendAnswer(std::uint8_t answer, std::string_view name)
{
    const NameBytes bytes = NameField(name);
    return Xor32C1(Network::PacketWriter::C1(FriendRequestOpcode).PutU8(answer).PutBytes(bytes));
}

Outgoing WriteFriendDelete(std::string_view name)
{
    const NameBytes bytes = NameField(name);
    return Xor32C1(Network::PacketWriter::C1(FriendDeleteOpcode).PutBytes(bytes));
}

Outgoing WriteFriendState(std::uint8_t state)
{
    return Xor32C1(Network::PacketWriter::C1(FriendStateOpcode).PutU8(state));
}
} // namespace Client::Protocol::Friends
