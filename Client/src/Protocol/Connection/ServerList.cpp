#include "Protocol/Connection/ServerList.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PacketFields.h"
#include "Protocol/ProtocolError.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Client::Protocol::Connection
{
namespace
{
// La lista que deja FoxAnti es un C2; la respuesta con la dirección, un C1.
constexpr PacketKind ListKind{Network::PacketType::C2, ServerListOpcode, ServerListSubcode};
constexpr PacketKind AddressKind{Network::PacketType::C1, ServerListOpcode, ServerSelectSubcode};

// Lo que mide una entrada de la lista: el código, la carga y un byte que 0x4c1860 no lee.
constexpr std::size_t ServerEntrySize = 4;

// Lo que mide el host en la respuesta: el puerto va en pkt[20].
constexpr std::size_t HostFieldSize = 16;

// Lo que se lee del host: los primeros 15 bytes. El dispatcher (0x4e3341) copia pkt[4..18] a un
// buffer de 16 bytes en 0, y el último queda como \0: pkt[19] no se lee nunca.
constexpr std::size_t HostReadSize = HostFieldSize - 1;

// Una entrada: el código y la carga, de los 4 bytes que mide. Con 4 bytes, las dos lecturas entran.
ServerEntry ReadEntry(std::span<const std::byte, ServerEntrySize> entry)
{
    Core::ByteReader fields(entry);
    const std::optional<std::uint16_t> code = fields.ReadU16Le();
    const std::optional<std::uint8_t> load = fields.ReadU8();
    return {*code, *load};
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

Core::Expected<std::vector<ServerEntry>, ProtocolError> ReadServerList(const Network::Packet& packet)
{
    auto fields = FieldsAfterSubcode(packet, ListKind);
    if (!fields.HasValue())
    {
        return Core::Unexpected(fields.Error());
    }
    Core::ByteReader& reader = fields.Value();
    const std::optional<std::uint16_t> count = reader.ReadU16Be();
    if (!count || *count > reader.Remaining() / ServerEntrySize)
    {
        return Core::Unexpected(ProtocolError::TooShort);
    }
    // La cantidad ya se comparó con lo que queda: cada entrada entra.
    std::vector<ServerEntry> entries;
    entries.reserve(*count);
    for (std::size_t k = 0; k < *count; ++k)
    {
        const std::optional<std::span<const std::byte>> entry = reader.ReadBytes(ServerEntrySize);
        entries.push_back(ReadEntry(entry->first<ServerEntrySize>()));
    }
    return entries;
}

Outgoing WriteServerSelect(std::uint16_t code)
{
    return ClearC1(Network::PacketWriter::C1(ServerListOpcode).PutU8(ServerSelectSubcode).PutU16Le(code));
}

Core::Expected<ServerAddress, ProtocolError> ReadServerAddress(const Network::Packet& packet)
{
    auto fields = FieldsAfterSubcode(packet, AddressKind);
    if (!fields.HasValue())
    {
        return Core::Unexpected(fields.Error());
    }
    Core::ByteReader& reader = fields.Value();
    const std::optional<std::span<const std::byte>> host = reader.ReadBytes(HostFieldSize);
    const std::optional<std::uint16_t> port = reader.ReadU16Le();
    if (!host || !port)
    {
        return Core::Unexpected(ProtocolError::TooShort);
    }
    return ServerAddress{TextField(host->first(HostReadSize)), *port};
}
} // namespace Client::Protocol::Connection
