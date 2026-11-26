#include "Protocol/Chat/Messages.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Protocol::Chat
{
namespace
{
constexpr std::size_t SpeakerNameAt = 3;
constexpr Network::PacketLayout ChatLayout{
    {Network::PacketType::C1, ChatOpcode, std::nullopt}, SpeakerNameAt, 0x4c6070};
constexpr Network::PacketLayout WhisperLayout{
    {Network::PacketType::C1, WhisperOpcode, std::nullopt}, SpeakerNameAt, 0x4e34ea};

// 0x4e3403..0x4e340c: pkt[3] << 8 + pkt[4].
using ChatKey = Network::Field<std::uint16_t, 3, std::endian::big>;
constexpr Network::PacketLayout KeyChatLayout{
    {Network::PacketType::C1, KeyChatOpcode, std::nullopt}, ChatKey::End, 0x4e33fc};

using Result = Network::Field<std::uint8_t, 3>;
constexpr Network::PacketLayout WhisperResultLayout{
    {Network::PacketType::C1, WhisperResultOpcode, std::nullopt}, Result::End, 0x4e366f};

using NoticeType = Network::Field<std::uint8_t, 3>;
using Byte4 = Network::Field<std::uint8_t, 4>;
// 0x4c66a4 and 0x4c667d: x86 WORD and DWORD loads, with no byte swap.
using Word6 = Network::Field<std::uint16_t, 6>;
using Color = Network::Field<std::uint32_t, 8>;
using Speed = Network::Field<std::uint8_t, 0xc>;
constexpr Network::PacketLayout NoticeLayout{
    {Network::PacketType::C1, NoticeOpcode, std::nullopt}, Speed::End, 0x4c65b0};

// The raw bytes from `at`, up to `limit` or to the packet's end, with their '\0'. `at` is within the layout's minimum.
std::string RawText(Network::LayoutReader& reader, std::size_t at, std::size_t limit)
{
    const std::span<const std::byte> bytes = reader.Bytes(at, std::min(limit, reader.Size() - at));
    std::string text;
    text.reserve(bytes.size());
    for (const std::byte value : bytes)
    {
        text.push_back(std::to_integer<char>(value));
    }
    return text;
}

// 0x4c6098: in scene 2, 0x4c6070 asks for the list without reading the packet, and a short 00 arrives too. The name
// that did not arrive stays '\0' and, without the whole name, there is no text (Q-C510).
template <typename Spoken> Spoken ReadSpoken(Network::LayoutReader& reader)
{
    const std::size_t arrived = std::min(SpeakerNameSize, reader.Size() - SpeakerNameAt);
    Spoken spoken;
    std::ranges::copy(reader.Bytes(SpeakerNameAt, arrived), spoken.name.begin());
    if (arrived == SpeakerNameSize)
    {
        spoken.text = RawText(reader, SpokenTextOffset, SpokenTextSize);
    }
    return spoken;
}
} // namespace

Core::Expected<Chat, Network::ProtocolError> ReadChat(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, ChatLayout, ReadSpoken<Chat>);
}

Core::Expected<Whisper, Network::ProtocolError> ReadWhisper(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, WhisperLayout, ReadSpoken<Whisper>);
}

Core::Expected<KeyChat, Network::ProtocolError> ReadKeyChat(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, KeyChatLayout, [](Network::LayoutReader& reader)
        { return KeyChat{.key = reader.Read<ChatKey>(), .text = RawText(reader, ChatKey::End, reader.Size())}; });
}

Core::Expected<WhisperResult, Network::ProtocolError> ReadWhisperResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, WhisperResultLayout,
                               [](Network::LayoutReader& reader) { return WhisperResult{reader.Read<Result>()}; });
}

Core::Expected<Notice, Network::ProtocolError> ReadNotice(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, NoticeLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return Notice{.type = reader.Read<NoticeType>(),
                                                 .byte4 = reader.Read<Byte4>(),
                                                 .word6 = reader.Read<Word6>(),
                                                 .color = reader.Read<Color>(),
                                                 .speed = reader.Read<Speed>(),
                                                 .text = RawText(reader, Speed::End, reader.Size())};
                               });
}
} // namespace Client::Protocol::Chat
