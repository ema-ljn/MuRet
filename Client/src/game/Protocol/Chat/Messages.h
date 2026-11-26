#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// The chat that arrives, as 0x4e2b10 hands it out (04, "El chat que llega"). The original reads its fields at fixed
// offsets without checking the packet's size; the texts keep the raw bytes it copies, but only those that arrived
// (Q-C510): what lies past the packet is like a '\0', in the 00's and the 02's name too.
namespace Client::Protocol::Chat
{
// pkt[3..12]: the speaker's name. Ten full bytes carry no '\0'; 0x4c6070 and 0x4e34ea add one.
inline constexpr std::size_t SpeakerNameSize = 10;
// 0x4c6070 and 0x4e34ea: the texts go to a zeroed 61-byte buffer, and the longest copy is 60 bytes from pkt[0xd].
inline constexpr std::size_t SpokenTextSize = 60;
// pkt[0xd]: where the 00's, the 02's and the 0D's texts start.
inline constexpr std::size_t SpokenTextOffset = 0xd;

// 00 (0x4c6070) and 02 (0x4e34ea): the name and up to 60 raw bytes from pkt[0xd], with their '\0'.
struct SpokenText
{
    std::array<std::byte, SpeakerNameSize> name{};
    std::string text;
};

struct Chat : SpokenText
{
};

struct Whisper : SpokenText
{
};

// 01 (0x4e33fc): the character's key at pkt[3] (high) and pkt[4] (low), and the text from pkt[5]. 0x557170 reads it
// in the packet itself, with strlen and strcpy: here, the raw bytes to the packet's end.
struct KeyChat
{
    std::uint16_t key = 0;
    std::string text;
};

// 0C (0x4e366f): pkt[3]. Only 0 does something.
struct WhisperResult
{
    std::uint8_t result = 0;
};

// 0D (0x4c65b0): the type at pkt[3] and, for the sign (10 to 15), pkt[4], the WORD at pkt[6], the DWORD at pkt[8] and
// the speed at pkt[0xc]. pkt[5] is not read. The text, raw from pkt[0xd] to the packet's end.
struct Notice
{
    std::uint8_t type = 0;
    std::uint8_t byte4 = 0;
    std::uint16_t word6 = 0;
    std::uint32_t color = 0;
    std::uint8_t speed = 0;
    std::string text;
};

[[nodiscard]] Core::Expected<Chat, Network::ProtocolError> ReadChat(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Whisper, Network::ProtocolError> ReadWhisper(const Network::Packet& packet);
[[nodiscard]] Core::Expected<KeyChat, Network::ProtocolError> ReadKeyChat(const Network::Packet& packet);
[[nodiscard]] Core::Expected<WhisperResult, Network::ProtocolError> ReadWhisperResult(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Notice, Network::ProtocolError> ReadNotice(const Network::Packet& packet);
} // namespace Client::Protocol::Chat
