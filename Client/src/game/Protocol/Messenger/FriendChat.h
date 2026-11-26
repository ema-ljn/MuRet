#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Outgoing.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The chat with a friend (04, "Las cartas y el chat con un amigo"). With the GameServer: the request for a room (CA)
// and the invitation (CB), which go and come back (0x4dce10 and 0x4dd020). With the chat server, a separate connection
// per room to port 55980 (0x49de50): what arrives (00, 01, 02, 04 and 0D, which 0x4e1d50 dispatches with the window to
// 0x4dd1b0..0x4dd540) and what the chat window sends (the entry, 0x493530; the message, 0x4944c0; the exit, 0x493a70).
// The GameServer 0.97 reconstruction has none of this: the oracle is the client's ASM. The chat server's handlers read
// the packet by position from pkt[0], C1 or C2.
namespace Client::Protocol::Messenger
{
// The chat server's.
inline constexpr Network::Opcode RoomJoinOpcode{0x00};
inline constexpr Network::Opcode RoomMemberOpcode{0x01};
inline constexpr Network::Opcode RoomMembersOpcode{0x02};
inline constexpr Network::Opcode RoomMessageOpcode{0x04};
inline constexpr Network::Opcode RoomNoticeOpcode{0x0d};
// What the timer 1002 sends to each room (0x4a6550).
inline constexpr Network::Opcode RoomPingOpcode{0x05};
// 0x49df17, PUSH 0xdaac.
inline constexpr std::uint16_t ChatPort = 55980;
inline constexpr std::size_t ChatNameSize = 10;
// CA: the IP in 15 bytes.
inline constexpr std::size_t ChatAddressSize = 15;
// 04: the message's cap (0x4dd3fc, with more it does nothing) and the chat EDIT's (0x493220).
inline constexpr std::uint8_t MessageLimit = 100;
// 02: each member, the number and the name.
inline constexpr std::size_t MemberRowSize = 11;
// 0D: 0x4dd540 cuts the text at pkt[0x68].
inline constexpr std::size_t NoticeEnd = 0x68;

// C1 CA (0x4dce10): pkt[3] the chat server's IP (15), pkt[0x12] the room (u16), pkt[0x14] the ticket (u32), pkt[0x18]
// the type (0, I asked for it; 1, I am invited or called; 2, another), pkt[0x19] the name (10) and pkt[0x23] the result
// (0, not there; 1, fine; 2, offline).
struct RoomAnswer
{
    std::string address;
    std::uint16_t room = 0;
    std::uint32_t ticket = 0;
    std::uint8_t type = 0;
    std::string name;
    std::uint8_t result = 0;
};

// C1 CB (0x4dd020): pkt[3] the result and pkt[4] the chat window (u32).
struct InviteResult
{
    std::uint8_t result = 0;
    std::uint32_t window = 0;
};

// 00 (0x4dd1b0): pkt[3] the result of entering the room.
struct JoinResult
{
    std::uint8_t result = 0;
};

// 01 (0x4dd1e0): pkt[3] 0 comes in or 1 leaves, pkt[4] its number and pkt[5] its name (strncpy of 10: up to the first
// '\0').
struct Member
{
    std::uint8_t kind = 0;
    std::uint8_t index = 0;
    std::string name;
};

// 02 (0x4dd350): pkt[6] how many and from pkt[8] the members of 11 bytes (the number and the name, with strncpy of 10).
struct MemberRow
{
    std::uint8_t index = 0;
    std::string name;
    friend bool operator==(const MemberRow&, const MemberRow&) = default;
};
struct Members
{
    std::vector<MemberRow> members;
};

// 04 (0x4dd3d0): pkt[3] the speaker's number, pkt[4] the length and from pkt[5] the text with the 3-byte XOR
// (0x6c82e4). With a length over 100, the handler does nothing and the text stays empty.
struct Message
{
    std::uint8_t index = 0;
    std::uint8_t length = 0;
    std::string text;
};

// 0D (0x4dd540): the text from pkt[5] up to the first '\0', cut at pkt[0x68].
struct Notice
{
    std::string text;
};

[[nodiscard]] Core::Expected<RoomAnswer, Network::ProtocolError> ReadRoomAnswer(const Network::Packet& packet);
[[nodiscard]] Core::Expected<InviteResult, Network::ProtocolError> ReadInviteResult(const Network::Packet& packet);
[[nodiscard]] Core::Expected<JoinResult, Network::ProtocolError> ReadJoinResult(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Member, Network::ProtocolError> ReadMember(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Members, Network::ProtocolError> ReadMembers(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Message, Network::ProtocolError> ReadMessage(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Notice, Network::ProtocolError> ReadNotice(const Network::Packet& packet);
// The handlers of 0x4e2a60 read each packet by position without looking at its length: with one shorter than what they
// read (a name or a text without its '\0' inside, or fewer bytes than the 04's length), the original goes on into the
// receive buffer (Q-C1011). TooShort with those; nothing with one read whole or with another opcode. The texts that end
// inside the packet are read even if it is shorter than the fixed place (the 0D of length 5 + text + 1).
[[nodiscard]] Core::Expected<void, Network::ProtocolError> CheckRoomPacket(const Network::Packet& packet);

// 0x4c1690 and 0x4dd3d0: each byte with FC CF AB (0x6c82e4) by its place % 3. It is its own inverse.
void FlipChatText(std::vector<std::byte>& bytes);

// 0x49d0b0 with 3 (the chat with the chosen friend): C1 0D CA and the name, with the XOR32.
[[nodiscard]] Outgoing WriteRoomRequest(std::string_view name);
// 0x4944c0 with 10 and 2: C1 14 CB, the invitee's name, a 0, the room (u16) and the window (u32), with the XOR32.
[[nodiscard]] Outgoing WriteInvite(std::string_view name, std::uint16_t room, std::uint32_t window);
// 0x493530, through the room's connection: C1 10 00, a 0, the room (u16) and the ticket (_itoa in base 10 to 10 zeroed
// bytes, with the 3-byte XOR), with the XOR32.
[[nodiscard]] Outgoing WriteJoin(std::uint16_t room, std::uint32_t ticket);
// 0x4944c0 with 7: C1 04, a 0, the length and the text with the 3-byte XOR, with the XOR32.
[[nodiscard]] Outgoing WriteMessage(std::string_view text);
// 0x493a70, on letting the window go: C1 03 01, which goes out as is (it has no bytes after the opcode).
[[nodiscard]] Outgoing WriteLeave();
// 0x4a6550, every 15 s (the timer 1002 of 0x48e470): C1 03 05, which goes out as is.
[[nodiscard]] Outgoing WritePing();
} // namespace Client::Protocol::Messenger
