#include "Protocol/Connection/ServerList.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketCode.h"
#include "Network/PacketFields.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Network/ReadList.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Client::Protocol::Connection
{
namespace
{
// The list FoxAnti leaves is a C2; the reply with the address, a C1.
using ServerCount = Network::Field<std::uint16_t, 5, std::endian::big>;
constexpr Network::PacketLayout ListLayout{
    {Network::PacketType::C2, Network::Opcode{ServerListOpcode}, Network::Subcode{ServerListSubcode}},
    ServerCount::End,
    0x4c1860};

// The entries follow the count: the code, the load and a byte 0x4c1860 does not read.
constexpr std::size_t ServerEntrySize = 4;

constexpr std::size_t HostFieldSize = 16;
using AddressHost = Network::Field<std::array<std::byte, HostFieldSize>, 4>;
using AddressPort = Network::Field<std::uint16_t, AddressHost::End>;
constexpr Network::PacketLayout AddressLayout{
    {Network::PacketType::C1, Network::Opcode{ServerListOpcode}, Network::Subcode{ServerSelectSubcode}},
    AddressPort::End,
    0x4e3341};

// 0x4e3341 copies pkt[4..18] to a 16-byte buffer in 0 and the last stays \0: pkt[19] is never read.
constexpr std::size_t HostReadSize = HostFieldSize - 1;

// Una entrada: el código y la carga, de los 4 bytes que mide. Con 4 bytes, las dos lecturas entran.
ServerEntry ReadEntry(std::span<const std::byte, ServerEntrySize> entry)
{
    Core::ByteReader fields(entry);
    const std::optional<std::uint16_t> code = fields.ReadU16Le();
    const std::optional<std::uint8_t> load = fields.ReadU8();
    return {*code, *load};
}

std::vector<ServerEntry> ReadEntries(Network::LayoutReader& reader)
{
    const std::uint16_t count = reader.Read<ServerCount>();
    return Network::ReadList<ServerEntrySize>(reader, ServerCount::End, count, ReadEntry);
}

ServerAddress ReadAddress(Network::LayoutReader& reader)
{
    const std::array<std::byte, HostFieldSize> host = reader.Read<AddressHost>();
    return {Network::TextField(std::span(host).first(HostReadSize)), reader.Read<AddressPort>()};
}
} // namespace

Outgoing WriteServerListRequest()
{
    return ClearC1(Network::PacketWriter::C1(ServerListOpcode).PutU8(ServerListRequestSubcode));
}

Outgoing WriteCreditsNotice()
{
    return ClearC1(Network::PacketWriter::C1(ServerListOpcode).PutU8(ServerListSubcode));
}

Core::Expected<std::vector<ServerEntry>, Network::ProtocolError> ReadServerList(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, ListLayout, ReadEntries);
}

Outgoing WriteServerSelect(std::uint16_t code)
{
    return ClearC1(Network::PacketWriter::C1(ServerListOpcode).PutU8(ServerSelectSubcode).PutU16Le(code));
}

Core::Expected<ServerAddress, Network::ProtocolError> ReadServerAddress(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, AddressLayout, ReadAddress);
}
} // namespace Client::Protocol::Connection
