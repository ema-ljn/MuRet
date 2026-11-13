#include "Protocol/Messages.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Client::Protocol::Messages
{
namespace
{
// El C1 del opcode, con el lector parado en pkt[3].
Core::Expected<Core::ByteReader, ProtocolError> FieldsOf(const Network::Packet& packet, std::uint8_t opcode)
{
    if (packet.Type() != Network::PacketType::C1 || packet.Opcode() != opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    return Core::ByteReader(packet.Bytes().subspan(Network::MinPacketSize(Network::PacketType::C1)));
}

// Los bytes crudos que siguen, hasta `limit` o hasta el final del paquete, con sus '\0'.
std::string RawText(Core::ByteReader& reader, std::size_t limit)
{
    const std::size_t count = std::min(limit, reader.Remaining());
    const auto bytes = reader.ReadBytes(count);
    std::string text;
    if (!bytes)
        return text;
    text.reserve(bytes->size());
    for (const std::byte value : *bytes)
        text.push_back(std::to_integer<char>(value));
    return text;
}

template <typename Spoken>
Core::Expected<Spoken, ProtocolError> ReadSpoken(const Network::Packet& packet, std::uint8_t opcode)
{
    auto fields = FieldsOf(packet, opcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    // 0x4c6098: en la escena 2, 0x4c6070 pide la lista sin leer el paquete; un 00 corto también llega. El nombre que
    // no llegó queda en '\0' y, sin el nombre entero, no hay texto (Q-C510).
    const bool wholeName = reader.Remaining() >= SpeakerNameSize;
    const auto name = reader.ReadBytes(std::min(SpeakerNameSize, reader.Remaining()));
    if (!name)
        return Core::Unexpected(ProtocolError::TooShort);
    Spoken spoken;
    std::copy(name->begin(), name->end(), spoken.name.begin());
    if (wholeName)
        spoken.text = RawText(reader, SpokenTextSize);
    return spoken;
}
} // namespace

Core::Expected<Chat, ProtocolError> ReadChat(const Network::Packet& packet)
{
    return ReadSpoken<Chat>(packet, ChatOpcode);
}

Core::Expected<Whisper, ProtocolError> ReadWhisper(const Network::Packet& packet)
{
    return ReadSpoken<Whisper>(packet, WhisperOpcode);
}

Core::Expected<KeyChat, ProtocolError> ReadKeyChat(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, KeyChatOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    // 0x4e3403..0x4e340c: pkt[3] << 8 + pkt[4].
    const auto key = reader.ReadU16Be();
    if (!key)
        return Core::Unexpected(ProtocolError::TooShort);
    return KeyChat{.key = *key, .text = RawText(reader, reader.Remaining())};
}

Core::Expected<WhisperResult, ProtocolError> ReadWhisperResult(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, WhisperResultOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    const auto result = fields.Value().ReadU8();
    if (!result)
        return Core::Unexpected(ProtocolError::TooShort);
    return WhisperResult{*result};
}

Core::Expected<Notice, ProtocolError> ReadNotice(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, NoticeOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto type = reader.ReadU8();
    const auto byte4 = reader.ReadU8();
    const auto byte5 = reader.ReadU8();
    // 0x4c66a4 y 0x4c667d: cargas x86 de WORD y DWORD, sin invertir bytes.
    const auto word6 = reader.ReadU16Le();
    const auto color = reader.ReadU32Le();
    const auto speed = reader.ReadU8();
    if (!type || !byte4 || !byte5 || !word6 || !color || !speed)
        return Core::Unexpected(ProtocolError::TooShort);
    return Notice{.type = *type,
                  .byte4 = *byte4,
                  .word6 = *word6,
                  .color = *color,
                  .speed = *speed,
                  .text = RawText(reader, reader.Remaining())};
}
} // namespace Client::Protocol::Messages
