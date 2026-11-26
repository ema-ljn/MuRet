#include "Protocol/Session/MapServerMove.h"

#include "Core/Expected.h"
#include "Crypto/Xor3.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/Session/BuildConstants.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Opcodes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Client::Protocol::Session
{
namespace
{
// 0x4e5076: the 0x24 bytes start at pkt+4; the host takes the first 0x10, the port is at +0x10 and the codes at +0x14.
using MovedBytes = Network::Field<std::array<std::byte, MapServerMoveBytes>, 4>;
using MovedPort = Network::Field<std::uint16_t, MovedBytes::Offset + 0x10>;
using MovedCodes = Network::Field<MapServerCodes, MovedBytes::Offset + 0x14>;

// 0x4e506a: the subcode is pkt[3] in a C1 and pkt[4] otherwise; the rest is at the same offsets in both.
constexpr Network::PacketLayout MoveC1Layout{
    {Network::PacketType::C1, MapServerOpcode, MapServer::MoveSubcode}, MovedBytes::End, 0x4e5056};
constexpr Network::PacketLayout MoveC2Layout{
    {Network::PacketType::C2, MapServerOpcode, MapServer::MoveSubcode}, MovedBytes::End, 0x4e5056};

MapServerMove ReadMove(Network::LayoutReader& reader)
{
    const std::array<std::byte, MapServerMoveBytes> moved = reader.Read<MovedBytes>();
    MapServerMove move;
    const auto end = std::ranges::find(moved, std::byte{0});
    move.hostEnds = end != moved.end();
    for (auto at = moved.begin(); at != end; ++at)
    {
        move.host.push_back(std::to_integer<char>(*at));
    }
    move.port = reader.Read<MovedPort>();
    move.codes = reader.Read<MovedCodes>();
    return move;
}

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

Core::Expected<MapServerMove, Network::ProtocolError> ReadMapServerMove(const Network::Packet& packet)
{
    const Network::PacketLayout& layout = packet.Type() == Network::PacketType::C2 ? MoveC2Layout : MoveC1Layout;
    return Network::ReadPacket(packet, layout, ReadMove);
}

Core::Expected<Outgoing, Network::ProtocolError> WriteMapServerAuth(const MapServerAuth& auth)
{
    if (auth.account.size() > AuthNameMax)
    {
        return Core::Unexpected(Network::ProtocolError::FieldTooLong);
    }
    AuthNameBytes account = NameField(auth.account);
    Crypto::Xor3(account);
    const AuthNameBytes character = NameField(auth.character.substr(0, AuthNameMax));
    Network::PacketWriter writer = Network::PacketWriter::C1(MapServerOpcode, MapServer::AuthSubcode);
    writer.PutBytes(account)
        .PutBytes(character)
        .PutBytes(auth.codes)
        .PutU32Le(auth.tick)
        .PutBytes(BytesOf(ClientVersion))
        .PutBytes(BytesOf(ClientSerial));
    return EncryptedC1(writer);
}
} // namespace Client::Protocol::Session
