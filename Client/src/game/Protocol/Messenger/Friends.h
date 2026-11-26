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

// The friends (04, "Los amigos"): what arrives (C0 in a C2, C1..C4 in a C1; 0x4dc010..0x4dc720) and what the friends
// window sends (0x48ee60, 0x4a3030, 0x4a2be0, 0x49d0b0 and 0x4a3b80). The GameServer 0.97 reconstruction has no
// friends: the oracle is the client's ASM.
namespace Client::Protocol::Messenger
{
// The names go in 10 bytes; the client copies them with strncpy to a zeroed buffer of 11.
inline constexpr std::size_t FriendNameSize = 10;
// C0: each friend, the name and the state.
inline constexpr std::size_t FriendRowSize = 11;

struct FriendRow
{
    std::string name;
    std::uint8_t state = 0;
    friend bool operator==(const FriendRow&, const FriendRow&) = default;
};

// C2 C0 (0x4dc010): pkt[4] the letters, pkt[5] their cap (0x5767970), pkt[6] how many friends and from pkt[7] the
// friends of 11 bytes. The three unsigned.
struct FriendList
{
    std::uint8_t letters = 0;
    std::uint8_t maxLetters = 0;
    std::vector<FriendRow> friends;
};

// C1 C1 (0x4dc3d0): pkt[3] the result, pkt[4..13] the name and pkt[0xe] the state.
struct FriendAddResult
{
    std::uint8_t result = 0;
    std::string name;
    std::uint8_t state = 0;
};

// C1 C2 (0x4dc5b0): pkt[3..12], the one asking to be a friend.
struct FriendRequest
{
    std::string name;
};

// C1 C3 (0x4dc690): pkt[3] the result and pkt[4..13] the name.
struct FriendDeleteResult
{
    std::uint8_t result = 0;
    std::string name;
};

// C1 C4 (0x4dc720): pkt[3..12] the name and pkt[0xd] the state.
struct FriendState
{
    std::string name;
    std::uint8_t state = 0;
};

[[nodiscard]] Core::Expected<FriendList, Network::ProtocolError> ReadFriendList(const Network::Packet& packet);
[[nodiscard]] Core::Expected<FriendAddResult, Network::ProtocolError>
ReadFriendAddResult(const Network::Packet& packet);
[[nodiscard]] Core::Expected<FriendRequest, Network::ProtocolError> ReadFriendRequest(const Network::Packet& packet);
[[nodiscard]] Core::Expected<FriendDeleteResult, Network::ProtocolError>
ReadFriendDeleteResult(const Network::Packet& packet);
[[nodiscard]] Core::Expected<FriendState, Network::ProtocolError> ReadFriendState(const Network::Packet& packet);

// 0x48ee60, case 3 of the factory with the manager's +0xe8 at 0 (without the list): C1 03 C0 in the clear, without the
// XOR32.
[[nodiscard]] Outgoing WriteFriendListRequest();
// 0x49d0b0 with 11 (the name in the add box): C1 0D C1 and the name, with the XOR32 from byte 3.
[[nodiscard]] Outgoing WriteFriendAdd(std::string_view name);
// 0x4a3b80 with owner −1 (0x4a3bde and 0x4a3fc8): C1 0E C2, 1 (yes) or 0 (no) and the requester's name, with the
// XOR32.
[[nodiscard]] Outgoing WriteFriendAnswer(std::uint8_t answer, std::string_view name);
// 0x49d0b0 with 12 and 1 (delete the chosen one): C1 0D C3 and the name, with the XOR32.
[[nodiscard]] Outgoing WriteFriendDelete(std::string_view name);
// 0x4a2be0 (12 with 1) and 0x4a3030 (the state button): C1 04 C4 and 0 (offline) or 1, with the XOR32.
[[nodiscard]] Outgoing WriteFriendState(std::uint8_t state);
} // namespace Client::Protocol::Messenger
