#include "Protocol/Connection/Logout.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Opcodes.h"

#include <cstdint>

namespace Client::Protocol::Connection
{
namespace
{
using LogoutResult = Network::Field<std::uint8_t, 4>;
// It arrives as a C1, already decrypted.
constexpr Network::PacketLayout LogoutResultLayout{
    {Network::PacketType::C1, JoinOpcode, Join::LogoutSubcode}, LogoutResult::End, 0x4c2710};
// 0x4c30ce y 0x4c31b9: lo que manda el F1 02 con 1 que llega sin cifrar, el 0 y el F1 de su paquete.
constexpr std::uint8_t ClearLogoutFirst = 0x00;
constexpr std::uint8_t ClearLogoutHead = Network::ByteOf(JoinOpcode);
constexpr Network::Opcode WindowCloseOpcode = NpcItemListOpcode;
} // namespace

Core::Expected<std::uint8_t, Network::ProtocolError> ReadLogoutResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LogoutResultLayout,
                               [](Network::LayoutReader& reader) { return reader.Read<LogoutResult>(); });
}

Outgoing WriteLogoutRequest(std::uint8_t kind)
{
    return EncryptedC1(Network::PacketWriter::C1(JoinOpcode, Join::LogoutSubcode).PutU8(kind));
}

Outgoing WriteClearLogoutReport()
{
    return EncryptedC1(Network::PacketWriter::C1(JoinOpcode, Join::DecryptErrorSubcode)
                           .PutU8(ClearLogoutFirst)
                           .PutU8(ClearLogoutHead));
}

Outgoing WriteWindowClose()
{
    return ClearC1(Network::PacketWriter::C1(WindowCloseOpcode));
}
} // namespace Client::Protocol::Connection
