#include "Protocol/Messenger/Letters.h"

#include "Core/Expected.h"
#include "Network/Delivery.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketFields.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Network/Xor32Packet.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Protocol::Messenger
{
namespace
{
// The handlers of 0x4dc840..0x4dcd90 read the packet by position from pkt[0].
using SendResult = Network::Field<std::uint8_t, 3>;
using SendWindow = Network::Field<std::uint32_t, 4>;
constexpr Network::PacketLayout LetterSendResultLayout{
    {Network::PacketType::C1, LetterSendOpcode, std::nullopt}, SendWindow::End, 0x4dc840};

using RowIndex = Network::Field<std::uint16_t, 4>;
using RowType = Network::Field<std::uint8_t, 0x4e>;
constexpr std::size_t RowNameAt = 6;
constexpr std::size_t RowDateAt = 0x10;
constexpr std::size_t RowTimeAt = 0x1b;
constexpr std::size_t RowSubjectAt = 0x2e;
constexpr Network::PacketLayout LetterRowLayout{
    {Network::PacketType::C1, LetterListOpcode, std::nullopt}, RowType::End, 0x4dc9e0};

constexpr std::size_t ReadHeaderSize = 0x1c;
constexpr Network::PacketLayout LetterReadLayout{
    {Network::PacketType::C2, LetterReadOpcode, std::nullopt}, ReadHeaderSize, 0x4dcb70};

using DeleteResult = Network::Field<std::uint8_t, 3>;
using DeleteIndex = Network::Field<std::uint16_t, 4>;
constexpr Network::PacketLayout LetterDeleteResultLayout{
    {Network::PacketType::C1, LetterDeleteOpcode, std::nullopt}, DeleteIndex::End, 0x4dcd90};

std::uint16_t U16At(std::span<const std::byte> bytes, std::size_t at)
{
    return static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(bytes[at]) |
                                      (std::to_integer<std::uint16_t>(bytes[at + 1]) << 8U));
}

std::uint8_t U8At(std::span<const std::byte> bytes, std::size_t at)
{
    return std::to_integer<std::uint8_t>(bytes[at]);
}

// strncpy of `size` to a zeroed buffer: up to the first '\0'.
std::string TextAt(std::span<const std::byte> bytes, std::size_t at, std::size_t size)
{
    return Network::TextField(bytes.subspan(at, size));
}

// A C2 with the XOR32 from byte 4, which the Connection encrypts as a C4.
Outgoing EncryptedC2(const Network::PacketWriter& writer)
{
    return {Network::Xor32Encode(writer.Finish().Value()), Network::Delivery::Encrypted};
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

Core::Expected<LetterSendResult, Network::ProtocolError> ReadLetterSendResult(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, LetterSendResultLayout, [](Network::LayoutReader& reader)
        { return LetterSendResult{.result = reader.Read<SendResult>(), .window = reader.Read<SendWindow>()}; });
}

Core::Expected<LetterRow, Network::ProtocolError> ReadLetterRow(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LetterRowLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return LetterRow{
                                       .index = reader.Read<RowIndex>(),
                                       .name = Network::TextField(reader.Bytes(RowNameAt, LetterNameSize)),
                                       .date = Network::TextField(reader.Bytes(RowDateAt, LetterDateSize)),
                                       .time = Network::TextField(reader.Bytes(RowTimeAt, LetterTimeSize)),
                                       .subject = Network::TextField(reader.Bytes(RowSubjectAt, LetterSubjectSize)),
                                       .type = reader.Read<RowType>()};
                               });
}

Core::Expected<LetterRead, Network::ProtocolError> ReadLetterRead(const Network::Packet& packet)
{
    const auto opened = Network::LayoutReader::Open(packet, LetterReadLayout);
    if (!opened.HasValue())
        return Core::Unexpected(opened.Error());
    return ReadLetterCopy(packet.Bytes());
}

Core::Expected<LetterRead, Network::ProtocolError> ReadLetterCopy(std::span<const std::byte> bytes)
{
    if (bytes.size() < ReadHeaderSize)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    LetterRead letter{.index = U16At(bytes, 4),
                      .length = U16At(bytes, 6),
                      .portraitClass = U8At(bytes, 8),
                      .equipment = {},
                      .direction = U8At(bytes, 0x1a),
                      .action = U8At(bytes, 0x1b),
                      .body = {},
                      .copy = {}};
    // 0x4dcb8a: pkt[0x1c + length] = 0 without looking at the size. A body that fills the packet exactly (the C5 sends
    // it without the 0) or 0x404's copy (a letter of 1000) is read; one past the packet, MuRet rejects (Q-C1000).
    if (ReadHeaderSize + letter.length > bytes.size())
        return Core::Unexpected(Network::ProtocolError::TooShort);
    std::copy_n(bytes.begin() + 9, PortraitEquipmentSize, letter.equipment.begin());
    letter.copy.assign(bytes.begin(),
                       bytes.begin() + static_cast<std::ptrdiff_t>(std::min(bytes.size(), LetterCopySize)));
    if (ReadHeaderSize + letter.length < letter.copy.size())
        letter.copy[ReadHeaderSize + letter.length] = std::byte{0};
    // strncpy(buf, pkt + 0x1c, 1000) with the 0 at the length.
    const std::size_t bodySize = std::min<std::size_t>(letter.length, LetterBodyLimit);
    letter.body = TextAt(bytes, ReadHeaderSize, bodySize);
    return letter;
}

Core::Expected<LetterDeleteResult, Network::ProtocolError> ReadLetterDeleteResult(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, LetterDeleteResultLayout, [](Network::LayoutReader& reader)
        { return LetterDeleteResult{.result = reader.Read<DeleteResult>(), .index = reader.Read<DeleteIndex>()}; });
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
} // namespace Client::Protocol::Messenger
