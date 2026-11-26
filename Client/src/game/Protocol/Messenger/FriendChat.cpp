#include "Protocol/Messenger/FriendChat.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketFields.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/Messenger/Letters.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Protocol::Messenger
{
namespace
{
using AnswerResult = Network::Field<std::uint8_t, 0x23>;
using AnswerRoom = Network::Field<std::uint16_t, 0x12>;
using AnswerTicket = Network::Field<std::uint32_t, 0x14>;
using AnswerType = Network::Field<std::uint8_t, 0x18>;
constexpr std::size_t AnswerAddressAt = 3;
constexpr std::size_t AnswerNameAt = 0x19;
constexpr Network::PacketLayout RoomAnswerLayout{
    {Network::PacketType::C1, RoomRequestOpcode, std::nullopt}, AnswerResult::End, 0x4dce10};

using InviteCode = Network::Field<std::uint8_t, 3>;
using InviteWindow = Network::Field<std::uint32_t, 4>;
constexpr Network::PacketLayout InviteResultLayout{
    {Network::PacketType::C1, RoomInviteOpcode, std::nullopt}, InviteWindow::End, 0x4dd020};

using JoinCode = Network::Field<std::uint8_t, 3>;

using MemberKind = Network::Field<std::uint8_t, 3>;
using MemberIndex = Network::Field<std::uint8_t, 4>;
// 0x4dd20d..0x4dd262: the name (strncpy of 10), then pkt[3]; pkt[4], with the 0.
constexpr std::size_t MemberNameAt = 5;

// 0x4dd367: with 0 it reads no more. Each member, the name (strncpy of 10, 0x4dd382) and its number (0x4dd391).
using MemberCount = Network::Field<std::uint8_t, 6>;
constexpr std::size_t MembersFirstRow = 8;

using SpeakerIndex = Network::Field<std::uint8_t, 3>;
using MessageLength = Network::Field<std::uint8_t, 4>;

// 0x4dd540 reads pkt[5], writes the 0 at pkt[0x68] (outside a shorter packet: MuRet does not write it) and uses the
// text up to the first '\0'.
constexpr std::size_t NoticeTextAt = 5;

// _itoa to a zeroed buffer of 10 (0x493530).
constexpr std::size_t TicketSize = 10;
// 0x6c82e4.
constexpr std::array<std::byte, 3> ChatTextKey{std::byte{0xfc}, std::byte{0xcf}, std::byte{0xab}};

// The chat server's handlers read the packet of `opcode` by position whatever its type.
Network::PacketLayout LayoutOf(const Network::Packet& packet, Network::Opcode opcode, std::size_t minimumSize,
                               std::uint32_t address)
{
    return {{packet.Type(), opcode, std::nullopt}, minimumSize, address};
}

// The room handlers' strncpy(destination, pkt + at, limit), or the text up to the 0 that 0x4dd540 puts at pkt[0x68]:
// up to the first '\0' or the limit. If the packet ends first, the original reads on into the receive buffer
// (Q-C1011): the reader records TooShort.
std::string CStringAt(Network::LayoutReader& reader, std::size_t at, std::size_t limit)
{
    const std::size_t inside = at < reader.Size() ? std::min(limit, reader.Size() - at) : 0;
    const std::span<const std::byte> text = reader.Bytes(at, inside);
    if (std::ranges::find(text, std::byte{0}) == text.end())
    {
        return Network::TextField(reader.Bytes(at, limit));
    }
    return Network::TextField(text);
}

std::uint8_t ByteAt(Network::LayoutReader& reader, std::size_t at)
{
    const std::span<const std::byte> byte = reader.Bytes(at, 1);
    return byte.empty() ? std::uint8_t{0} : std::to_integer<std::uint8_t>(byte[0]);
}
} // namespace

void FlipChatText(std::vector<std::byte>& bytes)
{
    for (std::size_t at = 0; at < bytes.size(); ++at)
        bytes[at] ^= ChatTextKey[at % ChatTextKey.size()];
}

Core::Expected<RoomAnswer, Network::ProtocolError> ReadRoomAnswer(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, RoomAnswerLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return RoomAnswer{
                                       .address = Network::TextField(reader.Bytes(AnswerAddressAt, ChatAddressSize)),
                                       .room = reader.Read<AnswerRoom>(),
                                       .ticket = reader.Read<AnswerTicket>(),
                                       .type = reader.Read<AnswerType>(),
                                       .name = Network::TextField(reader.Bytes(AnswerNameAt, ChatNameSize)),
                                       .result = reader.Read<AnswerResult>()};
                               });
}

Core::Expected<InviteResult, Network::ProtocolError> ReadInviteResult(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, InviteResultLayout, [](Network::LayoutReader& reader)
        { return InviteResult{.result = reader.Read<InviteCode>(), .window = reader.Read<InviteWindow>()}; });
}

Core::Expected<JoinResult, Network::ProtocolError> ReadJoinResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, RoomJoinOpcode, JoinCode::End, 0x4dd1b0),
                               [](Network::LayoutReader& reader)
                               { return JoinResult{.result = reader.Read<JoinCode>()}; });
}

Core::Expected<Member, Network::ProtocolError> ReadMember(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, RoomMemberOpcode, MemberNameAt, 0x4dd1e0),
                               [](Network::LayoutReader& reader)
                               {
                                   return Member{.kind = reader.Read<MemberKind>(),
                                                 .index = reader.Read<MemberIndex>(),
                                                 .name = CStringAt(reader, MemberNameAt, ChatNameSize)};
                               });
}

Core::Expected<Members, Network::ProtocolError> ReadMembers(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, RoomMembersOpcode, MemberCount::End, 0x4dd350),
                               [](Network::LayoutReader& reader)
                               {
                                   const std::uint8_t count = reader.Read<MemberCount>();
                                   Members members;
                                   members.members.reserve(count);
                                   for (std::size_t row = 0; row < count; ++row)
                                   {
                                       const std::size_t at = MembersFirstRow + (row * MemberRowSize);
                                       std::string name = CStringAt(reader, at + 1, ChatNameSize);
                                       members.members.push_back(
                                           MemberRow{.index = ByteAt(reader, at), .name = std::move(name)});
                                   }
                                   return members;
                               });
}

Core::Expected<Message, Network::ProtocolError> ReadMessage(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, LayoutOf(packet, RoomMessageOpcode, MessageLength::End, 0x4dd3d0),
        [](Network::LayoutReader& reader)
        {
            Message message{.index = reader.Read<SpeakerIndex>(), .length = reader.Read<MessageLength>(), .text = {}};
            // 0x4dd3fc: with more than 100 it returns before copying.
            if (message.length > MessageLimit)
            {
                return message;
            }
            // It copies the length to a zeroed buffer of 101, takes the 3-byte XOR off and uses it up to the first
            // '\0'.
            const std::span<const std::byte> bytes = reader.Bytes(MessageLength::End, message.length);
            std::vector<std::byte> text(bytes.begin(), bytes.end());
            FlipChatText(text);
            message.text = Network::TextField(text);
            return message;
        });
}

Core::Expected<Notice, Network::ProtocolError> ReadNotice(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, RoomNoticeOpcode, NoticeTextAt + 1, 0x4dd540),
                               [](Network::LayoutReader& reader)
                               { return Notice{.text = CStringAt(reader, NoticeTextAt, NoticeEnd - NoticeTextAt)}; });
}

Core::Expected<void, Network::ProtocolError> CheckRoomPacket(const Network::Packet& packet)
{
    const auto readable = [](const auto& read) -> Core::Expected<void, Network::ProtocolError>
    {
        if (!read.HasValue())
            return Core::Unexpected(read.Error());
        return {};
    };
    const std::optional<Network::Opcode> opcode = packet.Opcode();
    if (!opcode)
        return {};
    switch (Network::ByteOf(*opcode))
    {
    case Network::ByteOf(RoomJoinOpcode):
        return readable(ReadJoinResult(packet));
    case Network::ByteOf(RoomMemberOpcode):
        return readable(ReadMember(packet));
    case Network::ByteOf(RoomMembersOpcode):
        return readable(ReadMembers(packet));
    case Network::ByteOf(RoomMessageOpcode):
        return readable(ReadMessage(packet));
    case Network::ByteOf(RoomNoticeOpcode):
        return readable(ReadNotice(packet));
    default:
        return {};
    }
}

Outgoing WriteRoomRequest(std::string_view name)
{
    return Xor32C1(Network::PacketWriter::C1(RoomRequestOpcode).PutBytes(Messenger::FixedText(name, ChatNameSize)));
}

Outgoing WriteInvite(std::string_view name, std::uint16_t room, std::uint32_t window)
{
    return Xor32C1(Network::PacketWriter::C1(RoomInviteOpcode)
                       .PutBytes(Messenger::FixedText(name, ChatNameSize))
                       .PutU8(0)
                       .PutU16Le(room)
                       .PutU32Le(window));
}

Outgoing WriteJoin(std::uint16_t room, std::uint32_t ticket)
{
    // _itoa(ticket, buf, 10): the ticket is an int; one from 0x80000000 on comes out with its sign.
    const auto value = static_cast<std::int32_t>(ticket);
    std::vector<std::byte> digits = Messenger::FixedText(std::to_string(value), TicketSize);
    FlipChatText(digits);
    return Xor32C1(Network::PacketWriter::C1(RoomJoinOpcode).PutU8(0).PutU16Le(room).PutBytes(digits));
}

Outgoing WriteMessage(std::string_view text)
{
    const std::string_view cut = text.substr(0, text.find('\0'));
    std::vector<std::byte> bytes(cut.size());
    for (std::size_t at = 0; at < cut.size(); ++at)
        bytes[at] = static_cast<std::byte>(cut[at]);
    FlipChatText(bytes);
    return Xor32C1(Network::PacketWriter::C1(RoomMessageOpcode)
                       .PutU8(0)
                       .PutU8(static_cast<std::uint8_t>(cut.size()))
                       .PutBytes(bytes));
}

Outgoing WriteLeave()
{
    return ClearC1(Network::PacketWriter::C1(RoomMemberOpcode));
}

Outgoing WritePing()
{
    return ClearC1(Network::PacketWriter::C1(RoomPingOpcode));
}
} // namespace Client::Protocol::Messenger
