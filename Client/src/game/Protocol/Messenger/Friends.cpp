#include "Protocol/Messenger/Friends.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketFields.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Network/ReadList.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Protocol::Messenger
{
namespace
{
using NameBytes = std::array<std::byte, FriendNameSize>;

using Letters = Network::Field<std::uint8_t, 4>;
using MaxLetters = Network::Field<std::uint8_t, 5>;
using FriendCount = Network::Field<std::uint8_t, 6>;
constexpr Network::PacketLayout FriendListLayout{
    {Network::PacketType::C2, FriendListOpcode, std::nullopt}, FriendCount::End, 0x4dc010};

using AddResult = Network::Field<std::uint8_t, 3>;
using AddName = Network::Field<NameBytes, 4>;
using AddState = Network::Field<std::uint8_t, 0xe>;
constexpr Network::PacketLayout FriendAddResultLayout{
    {Network::PacketType::C1, FriendAddOpcode, std::nullopt}, AddState::End, 0x4dc3d0};

using RequesterName = Network::Field<NameBytes, 3>;
constexpr Network::PacketLayout FriendRequestLayout{
    {Network::PacketType::C1, FriendRequestOpcode, std::nullopt}, RequesterName::End, 0x4dc5b0};

using DeleteResult = Network::Field<std::uint8_t, 3>;
using DeleteName = Network::Field<NameBytes, 4>;
constexpr Network::PacketLayout FriendDeleteResultLayout{
    {Network::PacketType::C1, FriendDeleteOpcode, std::nullopt}, DeleteName::End, 0x4dc690};

using StateName = Network::Field<NameBytes, 3>;
using State = Network::Field<std::uint8_t, 0xd>;
constexpr Network::PacketLayout FriendStateLayout{
    {Network::PacketType::C1, FriendStateOpcode, std::nullopt}, State::End, 0x4dc720};

// strncpy of 10 to a zeroed buffer of 11: up to the first '\0'.
template <typename F> std::string ReadName(Network::LayoutReader& reader)
{
    return Network::TextField(reader.Read<F>());
}

FriendRow ReadRow(std::span<const std::byte, FriendRowSize> row)
{
    return {.name = Network::TextField(row.first<FriendNameSize>()),
            .state = std::to_integer<std::uint8_t>(row[FriendNameSize])};
}

// MuRet fills with zeros what the original leaves with whatever was in its buffer (Q-C882).
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

Core::Expected<FriendList, Network::ProtocolError> ReadFriendList(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, FriendListLayout,
        [](Network::LayoutReader& reader)
        {
            return FriendList{.letters = reader.Read<Letters>(),
                              .maxLetters = reader.Read<MaxLetters>(),
                              .friends = Network::ReadList<FriendRowSize>(reader, FriendCount::End,
                                                                          reader.Read<FriendCount>(), ReadRow)};
        });
}

Core::Expected<FriendAddResult, Network::ProtocolError> ReadFriendAddResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, FriendAddResultLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return FriendAddResult{.result = reader.Read<AddResult>(),
                                                          .name = ReadName<AddName>(reader),
                                                          .state = reader.Read<AddState>()};
                               });
}

Core::Expected<FriendRequest, Network::ProtocolError> ReadFriendRequest(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, FriendRequestLayout, [](Network::LayoutReader& reader)
                               { return FriendRequest{.name = ReadName<RequesterName>(reader)}; });
}

Core::Expected<FriendDeleteResult, Network::ProtocolError> ReadFriendDeleteResult(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, FriendDeleteResultLayout, [](Network::LayoutReader& reader)
        { return FriendDeleteResult{.result = reader.Read<DeleteResult>(), .name = ReadName<DeleteName>(reader)}; });
}

Core::Expected<FriendState, Network::ProtocolError> ReadFriendState(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, FriendStateLayout, [](Network::LayoutReader& reader)
        { return FriendState{.name = ReadName<StateName>(reader), .state = reader.Read<State>()}; });
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
} // namespace Client::Protocol::Messenger
