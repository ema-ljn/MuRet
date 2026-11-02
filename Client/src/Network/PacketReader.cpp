#include "Network/PacketReader.h"

#include <iterator>
#include <utility>

namespace Client::Network
{

void PacketReader::Feed(std::span<const std::byte> bytes)
{
    if (error)
    {
        return;
    }
    // Lo que ya salió se borra antes de agregar. Si quien lo usa saca todos los paquetes después de
    // cada Feed, queda a lo sumo un paquete a medio llegar, y correrlo cuesta poco.
    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed));
    consumed = 0;
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
}

// Mira solo bytes que llegaron: el tipo con el primero, el tamaño con la cabecera entera, y el
// paquete cuando están todos sus bytes. Los errores de tamaño salen apenas llega la cabecera.
Core::Expected<std::optional<Packet>, PacketError> PacketReader::Next()
{
    if (error)
    {
        return Core::Unexpected(*error);
    }
    const std::span<const std::byte> pending = std::span<const std::byte>(buffer).subspan(consumed);
    if (pending.empty())
    {
        return std::optional<Packet>{};
    }
    const std::optional<PacketType> type = PacketTypeFromByte(pending.front());
    if (!type)
    {
        return Fail(PacketError::InvalidType);
    }
    const std::optional<std::size_t> size = DeclaredSize(*type, pending);
    if (!size)
    {
        return std::optional<Packet>{};
    }
    if (*size < MinPacketSize(*type))
    {
        return Fail(PacketError::SizeTooSmall);
    }
    if (*size > MaxPacketSize)
    {
        return Fail(PacketError::TooLarge);
    }
    if (pending.size() < *size)
    {
        return std::optional<Packet>{};
    }
    const std::span<const std::byte> bytes = pending.first(*size);
    Core::Expected<Packet, PacketError> packet = Packet::FromBytes({bytes.begin(), bytes.end()});
    if (!packet.HasValue())
    {
        return Fail(packet.Error());
    }
    consumed += *size;
    return std::optional<Packet>{std::move(packet).Value()};
}

std::size_t PacketReader::Pending() const
{
    return buffer.size() - consumed;
}

// El error queda, y lo que había se descarta: no se va a cortar nunca.
Core::Expected<std::optional<Packet>, PacketError> PacketReader::Fail(PacketError reason)
{
    error = reason;
    buffer.clear();
    consumed = 0;
    return Core::Unexpected(reason);
}

} // namespace Client::Network
