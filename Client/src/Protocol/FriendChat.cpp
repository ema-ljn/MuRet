#include "Protocol/FriendChat.h"

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Letters.h"
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
#include <vector>

namespace Client::Protocol::FriendChat
{
namespace
{
constexpr std::size_t RoomAnswerSize = 0x24;
constexpr std::size_t InviteResultSize = 8;
constexpr std::size_t JoinResultSize = 4;
// 01: pkt[3] y pkt[4], y el nombre desde pkt[5].
constexpr std::size_t MemberNameAt = 5;
// 02: la cuenta en pkt[6] y los miembros desde pkt[8].
constexpr std::size_t MembersCountAt = 6;
constexpr std::size_t MembersFirstRow = 8;
constexpr std::size_t MessageHeaderSize = 5;
// 0D: pkt[5] y el texto desde ahí.
constexpr std::size_t NoticeTextAt = 5;
// El ticket: _itoa a un buffer de 10 en cero (0x493530).
constexpr std::size_t TicketSize = 10;
// 0x6c82e4.
constexpr std::array<std::byte, 3> ChatTextKey{std::byte{0xfc}, std::byte{0xcf}, std::byte{0xab}};

Core::Expected<std::span<const std::byte>, ProtocolError> BytesOf(const Network::Packet& packet, std::uint8_t opcode,
                                                                  std::size_t size)
{
    if (packet.Opcode() != opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    const std::span<const std::byte> bytes = packet.Bytes();
    if (bytes.size() < size)
        return Core::Unexpected(ProtocolError::TooShort);
    return bytes;
}

Core::Expected<std::span<const std::byte>, ProtocolError> C1BytesOf(const Network::Packet& packet, std::uint8_t opcode,
                                                                    std::size_t size)
{
    if (packet.Type() != Network::PacketType::C1)
        return Core::Unexpected(ProtocolError::WrongPacket);
    return BytesOf(packet, opcode, size);
}

std::uint8_t U8At(std::span<const std::byte> bytes, std::size_t at)
{
    return std::to_integer<std::uint8_t>(bytes[at]);
}

std::uint16_t U16At(std::span<const std::byte> bytes, std::size_t at)
{
    return static_cast<std::uint16_t>(U8At(bytes, at) | (U8At(bytes, at + 1) << 8U));
}

std::uint32_t U32At(std::span<const std::byte> bytes, std::size_t at)
{
    return static_cast<std::uint32_t>(U16At(bytes, at)) | (static_cast<std::uint32_t>(U16At(bytes, at + 2)) << 16U);
}

std::string TextAt(std::span<const std::byte> bytes, std::size_t at, std::size_t size)
{
    return TextField(bytes.subspan(at, size));
}

// strncpy(destino, pkt + at, tope) de los handlers de la sala, o el texto hasta el 0 que 0x4dd540 pone en pkt[0x68]:
// hasta el primer '\0' o el tope. Si el paquete termina antes, el original sigue leyendo el búfer de recepción
// (Q-C1011): no se lee.
std::optional<std::string> CStringAt(std::span<const std::byte> bytes, std::size_t at, std::size_t limit)
{
    std::string text;
    for (std::size_t read = 0; read < limit; ++read)
    {
        if (at + read >= bytes.size())
            return std::nullopt;
        const std::uint8_t byte = U8At(bytes, at + read);
        if (byte == 0)
            return text;
        text.push_back(static_cast<char>(byte));
    }
    return text;
}
} // namespace

void FlipChatText(std::vector<std::byte>& bytes)
{
    for (std::size_t at = 0; at < bytes.size(); ++at)
        bytes[at] ^= ChatTextKey[at % ChatTextKey.size()];
}

Core::Expected<RoomAnswer, ProtocolError> ReadRoomAnswer(const Network::Packet& packet)
{
    auto read = C1BytesOf(packet, RoomRequestOpcode, RoomAnswerSize);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const std::span<const std::byte> bytes = read.Value();
    return RoomAnswer{.address = TextAt(bytes, 3, ChatAddressSize),
                      .room = U16At(bytes, 0x12),
                      .ticket = U32At(bytes, 0x14),
                      .type = U8At(bytes, 0x18),
                      .name = TextAt(bytes, 0x19, ChatNameSize),
                      .result = U8At(bytes, 0x23)};
}

Core::Expected<InviteResult, ProtocolError> ReadInviteResult(const Network::Packet& packet)
{
    auto read = C1BytesOf(packet, InviteOpcode, InviteResultSize);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    return InviteResult{.result = U8At(read.Value(), 3), .window = U32At(read.Value(), 4)};
}

Core::Expected<JoinResult, ProtocolError> ReadJoinResult(const Network::Packet& packet)
{
    auto read = BytesOf(packet, JoinOpcode, JoinResultSize);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    return JoinResult{.result = U8At(read.Value(), 3)};
}

Core::Expected<Member, ProtocolError> ReadMember(const Network::Packet& packet)
{
    // 0x4dd20d..0x4dd262: el nombre (strncpy de 10) y después pkt[3]; pkt[4], con el 0.
    auto read = BytesOf(packet, MemberOpcode, MemberNameAt);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const std::span<const std::byte> bytes = read.Value();
    std::optional<std::string> name = CStringAt(bytes, MemberNameAt, ChatNameSize);
    if (!name)
        return Core::Unexpected(ProtocolError::TooShort);
    return Member{.kind = U8At(bytes, 3), .index = U8At(bytes, 4), .name = std::move(*name)};
}

Core::Expected<Members, ProtocolError> ReadMembers(const Network::Packet& packet)
{
    // 0x4e62b1: pkt[6]; con 0 no lee más. Cada miembro, el nombre (strncpy de 10, 0x4e62d5) y su número (0x4e62e4).
    auto read = BytesOf(packet, MembersOpcode, MembersCountAt + 1);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const std::span<const std::byte> bytes = read.Value();
    const std::uint8_t count = U8At(bytes, MembersCountAt);
    Members members;
    members.members.reserve(count);
    for (std::size_t row = 0; row < count; ++row)
    {
        const std::size_t at = MembersFirstRow + (row * MemberRowSize);
        std::optional<std::string> name = CStringAt(bytes, at + 1, ChatNameSize);
        if (!name)
            return Core::Unexpected(ProtocolError::TooShort);
        members.members.push_back(MemberRow{.index = U8At(bytes, at), .name = std::move(*name)});
    }
    return members;
}

Core::Expected<Message, ProtocolError> ReadMessage(const Network::Packet& packet)
{
    auto read = BytesOf(packet, MessageOpcode, MessageHeaderSize);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const std::span<const std::byte> bytes = read.Value();
    Message message{.index = U8At(bytes, 3), .length = U8At(bytes, 4), .text = {}};
    // 0x4dd3fc: con más de 100 vuelve antes de copiar.
    if (message.length > MessageLimit)
        return message;
    if (bytes.size() < MessageHeaderSize + message.length)
        return Core::Unexpected(ProtocolError::TooShort);
    // Copia el largo a un buffer de 101 en cero, le saca el XOR de 3 bytes y lo usa hasta el primer '\0'.
    std::vector<std::byte> text(bytes.begin() + MessageHeaderSize,
                                bytes.begin() + static_cast<std::ptrdiff_t>(MessageHeaderSize + message.length));
    FlipChatText(text);
    message.text = TextField(text);
    return message;
}

Core::Expected<Notice, ProtocolError> ReadNotice(const Network::Packet& packet)
{
    // 0x4dd540 lee pkt[5], escribe el 0 en pkt[0x68] (fuera de un paquete más corto: MuRet no lo escribe) y usa el
    // texto hasta el primer '\0'.
    auto read = BytesOf(packet, NoticeOpcode, NoticeTextAt + 1);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    std::optional<std::string> text = CStringAt(read.Value(), NoticeTextAt, NoticeEnd - NoticeTextAt);
    if (!text)
        return Core::Unexpected(ProtocolError::TooShort);
    return Notice{.text = std::move(*text)};
}

Core::Expected<void, ProtocolError> CheckRoomPacket(const Network::Packet& packet)
{
    const auto readable = [](const auto& read) -> Core::Expected<void, ProtocolError>
    {
        if (!read.HasValue())
            return Core::Unexpected(read.Error());
        return {};
    };
    const std::optional<std::uint8_t> opcode = packet.Opcode();
    if (!opcode)
        return {};
    switch (*opcode)
    {
    case JoinOpcode:
        return readable(ReadJoinResult(packet));
    case MemberOpcode:
        return readable(ReadMember(packet));
    case MembersOpcode:
        return readable(ReadMembers(packet));
    case MessageOpcode:
        return readable(ReadMessage(packet));
    case NoticeOpcode:
        return readable(ReadNotice(packet));
    default:
        return {};
    }
}

Outgoing WriteRoomRequest(std::string_view name)
{
    return Xor32C1(Network::PacketWriter::C1(RoomRequestOpcode).PutBytes(Letters::FixedText(name, ChatNameSize)));
}

Outgoing WriteInvite(std::string_view name, std::uint16_t room, std::uint32_t window)
{
    return Xor32C1(Network::PacketWriter::C1(InviteOpcode)
                       .PutBytes(Letters::FixedText(name, ChatNameSize))
                       .PutU8(0)
                       .PutU16Le(room)
                       .PutU32Le(window));
}

Outgoing WriteJoin(std::uint16_t room, std::uint32_t ticket)
{
    // _itoa(ticket, buf, 10): el ticket es un int; uno desde 0x80000000 sale con el signo.
    const auto value = static_cast<std::int32_t>(ticket);
    std::vector<std::byte> digits = Letters::FixedText(std::to_string(value), TicketSize);
    FlipChatText(digits);
    return Xor32C1(Network::PacketWriter::C1(JoinOpcode).PutU8(0).PutU16Le(room).PutBytes(digits));
}

Outgoing WriteMessage(std::string_view text)
{
    const std::string_view cut = text.substr(0, text.find('\0'));
    std::vector<std::byte> bytes(cut.size());
    for (std::size_t at = 0; at < cut.size(); ++at)
        bytes[at] = static_cast<std::byte>(cut[at]);
    FlipChatText(bytes);
    return Xor32C1(
        Network::PacketWriter::C1(MessageOpcode).PutU8(0).PutU8(static_cast<std::uint8_t>(cut.size())).PutBytes(bytes));
}

Outgoing WriteLeave()
{
    return ClearC1(Network::PacketWriter::C1(MemberOpcode));
}

Outgoing WritePing()
{
    return ClearC1(Network::PacketWriter::C1(PingOpcode));
}
} // namespace Client::Protocol::FriendChat
