#include "Protocol/Connection/MapServerMove.h"

#include "Core/Expected.h"
#include "Core/Xor3.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Client::Protocol::Connection
{
namespace
{
// 0x4e5076: los 0x24 bytes empiezan en pkt+4; el host ocupa los primeros 0x10, el puerto va en +0x10 y los
// códigos en +0x14.
constexpr std::size_t MoveBytesAt = 4;
constexpr std::size_t PortAt = 0x10;
constexpr std::size_t CodesAt = 0x14;
// 0x412da3..0x412dba: la cuenta y el nombre van cada uno en 12 bytes en 0 (EBP − 0x40 y EBP − 0x4c); entran 10.
// 0x412c1e corta el nombre en 10 (MOV byte [EBP − 0x4e], 0 sobre la copia de EBP − 0x58); la cuenta no.
constexpr std::size_t AuthNameField = 12;
constexpr std::size_t AuthNameMax = 10;

using AuthNameBytes = std::array<std::byte, AuthNameField>;

AuthNameBytes NameField(std::string_view text)
{
    AuthNameBytes field{};
    std::ranges::copy(std::as_bytes(std::span(text)), field.begin());
    return field;
}

std::span<const std::byte> BytesOf(std::string_view text)
{
    return std::as_bytes(std::span(text));
}
} // namespace

Core::Expected<MapServerMove, ProtocolError> ReadMapServerMove(const Network::Packet& packet)
{
    const std::span<const std::byte> bytes = packet.Bytes();
    const std::size_t subcodeAt = Network::MinPacketSize(packet.Type());
    if (packet.Opcode() != MapServerOpcode)
    {
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
    if (bytes.size() <= subcodeAt)
    {
        return Core::Unexpected(ProtocolError::TooShort);
    }
    if (std::to_integer<std::uint8_t>(bytes[subcodeAt]) != MapServerMoveSubcode)
    {
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
    if (bytes.size() < MoveBytesAt + MapServerMoveBytes)
    {
        return Core::Unexpected(ProtocolError::TooShort);
    }
    const std::span<const std::byte> moved = bytes.subspan(MoveBytesAt, MapServerMoveBytes);
    MapServerMove move;
    const auto end = std::ranges::find(moved, std::byte{0});
    move.hostEnds = end != moved.end();
    for (auto at = moved.begin(); at != end; ++at)
    {
        move.host.push_back(std::to_integer<char>(*at));
    }
    move.port = static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(moved[PortAt]) |
                                           (std::to_integer<std::uint16_t>(moved[PortAt + 1]) << 8u));
    std::ranges::copy(moved.subspan(CodesAt, MapServerCodesSize), move.codes.begin());
    return move;
}

Core::Expected<Outgoing, ProtocolError> WriteMapServerAuth(const MapServerAuth& auth)
{
    if (auth.account.size() > AuthNameMax)
    {
        return Core::Unexpected(ProtocolError::FieldTooLong);
    }
    AuthNameBytes account = NameField(auth.account);
    Core::Xor3(account);
    const AuthNameBytes character = NameField(auth.character.substr(0, AuthNameMax));
    Network::PacketWriter writer = Network::PacketWriter::C1(MapServerOpcode);
    writer.PutU8(MapServerAuthSubcode)
        .PutBytes(account)
        .PutBytes(character)
        .PutBytes(auth.codes)
        .PutU32Le(auth.tick)
        .PutBytes(BytesOf(ClientVersion))
        .PutBytes(BytesOf(ClientSerial));
    return EncryptedC1(writer);
}
} // namespace Client::Protocol::Connection
