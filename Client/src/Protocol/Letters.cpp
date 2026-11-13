#include "Protocol/Letters.h"

#include "Core/Expected.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PacketFields.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Protocol::Letters
{
namespace
{
// Los handlers de 0x4dc840..0x4dcd90 leen el paquete por su lugar desde pkt[0].
constexpr std::size_t SendResultSize = 8;
constexpr std::size_t RowSize = 0x4f;
constexpr std::size_t ReadHeaderSize = 0x1c;
constexpr std::size_t DeleteResultSize = 6;

Core::Expected<std::span<const std::byte>, ProtocolError>
BytesOf(const Network::Packet& packet, Network::PacketType type, std::uint8_t opcode, std::size_t size)
{
    if (packet.Type() != type || packet.Opcode() != opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    const std::span<const std::byte> bytes = packet.Bytes();
    if (bytes.size() < size)
        return Core::Unexpected(ProtocolError::TooShort);
    return bytes;
}

std::uint16_t U16At(std::span<const std::byte> bytes, std::size_t at)
{
    return static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(bytes[at]) |
                                      (std::to_integer<std::uint16_t>(bytes[at + 1]) << 8U));
}

std::uint32_t U32At(std::span<const std::byte> bytes, std::size_t at)
{
    return static_cast<std::uint32_t>(U16At(bytes, at)) | (static_cast<std::uint32_t>(U16At(bytes, at + 2)) << 16U);
}

std::uint8_t U8At(std::span<const std::byte> bytes, std::size_t at)
{
    return std::to_integer<std::uint8_t>(bytes[at]);
}

// strncpy de `size` a un buffer en cero: hasta el primer '\0'.
std::string TextAt(std::span<const std::byte> bytes, std::size_t at, std::size_t size)
{
    return TextField(bytes.subspan(at, size));
}

// Un C2 con el XOR32 desde el byte 4, que la Connection cifra como C4.
Outgoing EncryptedC2(const Network::PacketWriter& writer)
{
    return {Network::Crypto::Xor32Encode(writer.Finish().Value()), Network::Delivery::Encrypted};
}
} // namespace

std::vector<std::byte> FixedText(std::string_view text, std::size_t size)
{
    std::vector<std::byte> bytes(size, std::byte{0});
    const std::string_view cut = text.substr(0, text.find('\0'));
    for (std::size_t at = 0; at < size && at < cut.size(); ++at)
        bytes[at] = static_cast<std::byte>(cut[at]);
    return bytes;
}

Core::Expected<LetterSendResult, ProtocolError> ReadLetterSendResult(const Network::Packet& packet)
{
    auto bytes = BytesOf(packet, Network::PacketType::C1, LetterSendOpcode, SendResultSize);
    if (!bytes.HasValue())
        return Core::Unexpected(bytes.Error());
    return LetterSendResult{.result = U8At(bytes.Value(), 3), .window = U32At(bytes.Value(), 4)};
}

Core::Expected<LetterRow, ProtocolError> ReadLetterRow(const Network::Packet& packet)
{
    auto read = BytesOf(packet, Network::PacketType::C1, LetterListOpcode, RowSize);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const std::span<const std::byte> bytes = read.Value();
    return LetterRow{.index = U16At(bytes, 4),
                     .name = TextAt(bytes, 6, LetterNameSize),
                     .date = TextAt(bytes, 0x10, LetterDateSize),
                     .time = TextAt(bytes, 0x1b, LetterTimeSize),
                     .subject = TextAt(bytes, 0x2e, LetterSubjectSize),
                     .type = U8At(bytes, 0x4e)};
}

Core::Expected<LetterRead, ProtocolError> ReadLetterRead(const Network::Packet& packet)
{
    auto read = BytesOf(packet, Network::PacketType::C2, LetterReadOpcode, ReadHeaderSize);
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    return ReadLetterCopy(read.Value());
}

Core::Expected<LetterRead, ProtocolError> ReadLetterCopy(std::span<const std::byte> bytes)
{
    if (bytes.size() < ReadHeaderSize)
        return Core::Unexpected(ProtocolError::TooShort);
    LetterRead letter{.index = U16At(bytes, 4),
                      .length = U16At(bytes, 6),
                      .portraitClass = U8At(bytes, 8),
                      .equipment = {},
                      .direction = U8At(bytes, 0x1a),
                      .action = U8At(bytes, 0x1b),
                      .body = {},
                      .copy = {}};
    // 0x4dcb8a: pkt[0x1c + largo] = 0. Fuera del paquete (o de la copia de 0x404), el original escribe en lo que siga
    // en su buffer (Q-C1000).
    if (ReadHeaderSize + letter.length >= bytes.size())
        return Core::Unexpected(ProtocolError::TooShort);
    std::copy_n(bytes.begin() + 9, PortraitEquipmentSize, letter.equipment.begin());
    letter.copy.assign(bytes.begin(),
                       bytes.begin() + static_cast<std::ptrdiff_t>(std::min(bytes.size(), LetterCopySize)));
    if (ReadHeaderSize + letter.length < letter.copy.size())
        letter.copy[ReadHeaderSize + letter.length] = std::byte{0};
    // strncpy(buf, pkt + 0x1c, 1000) con el 0 en el largo.
    const std::size_t bodySize = std::min<std::size_t>(letter.length, LetterBodyLimit);
    letter.body = TextAt(bytes, ReadHeaderSize, bodySize);
    return letter;
}

Core::Expected<LetterDeleteResult, ProtocolError> ReadLetterDeleteResult(const Network::Packet& packet)
{
    auto bytes = BytesOf(packet, Network::PacketType::C1, LetterDeleteOpcode, DeleteResultSize);
    if (!bytes.HasValue())
        return Core::Unexpected(bytes.Error());
    return LetterDeleteResult{.result = U8At(bytes.Value(), 3), .index = U16At(bytes.Value(), 4)};
}

Outgoing WriteLetterSend(const LetterToSend& letter)
{
    const std::size_t size = std::min(letter.body.size(), LetterBodyLimit);
    std::vector<std::byte> body(size);
    for (std::size_t at = 0; at < size; ++at)
        body[at] = static_cast<std::byte>(letter.body[at]);
    return EncryptedC2(Network::PacketWriter::C2(LetterSendOpcode)
                           .PutU32Le(letter.window)
                           .PutBytes(FixedText(letter.name, LetterNameSize))
                           .PutBytes(FixedText(letter.subject, LetterSubjectSize))
                           .PutU8(letter.direction)
                           .PutU8(letter.action)
                           .PutU16Le(static_cast<std::uint16_t>(size))
                           .PutBytes(body));
}

Outgoing WriteLetterReadRequest(std::uint16_t index)
{
    return Xor32C1(Network::PacketWriter::C1(LetterReadOpcode).PutU8(0).PutU16Le(index));
}

Outgoing WriteLetterDelete(std::uint16_t index)
{
    return Xor32C1(Network::PacketWriter::C1(LetterDeleteOpcode).PutU8(0).PutU16Le(index));
}

Outgoing WriteLetterListRequest()
{
    return ClearC1(Network::PacketWriter::C1(LetterListRequestOpcode));
}
} // namespace Client::Protocol::Letters
