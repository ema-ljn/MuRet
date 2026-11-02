#include "Network/PacketReader.h"

#include "Network/IdentityObfuscation.h"

#include <algorithm>
#include <utility>

namespace Client::Network
{

PacketReader::PacketReader() : PacketReader(IdentityObfuscation::Shared()) {}

PacketReader::PacketReader(const Obfuscation& connectionObfuscation) : obfuscation(&connectionObfuscation) {}

PacketReader::PacketReader(PacketReader&& other) noexcept
    : obfuscation(other.obfuscation), buffer(std::exchange(other.buffer, {})),
      consumed(std::exchange(other.consumed, 0)), unmasked(std::exchange(other.unmasked, 0)),
      mask(std::exchange(other.mask, std::byte{0})), error(std::exchange(other.error, std::nullopt))
{
}

PacketReader& PacketReader::operator=(PacketReader&& other) noexcept
{
    obfuscation = other.obfuscation;
    buffer = std::exchange(other.buffer, {});
    consumed = std::exchange(other.consumed, 0);
    unmasked = std::exchange(other.unmasked, 0);
    mask = std::exchange(other.mask, std::byte{0});
    error = std::exchange(other.error, std::nullopt);
    return *this;
}

void PacketReader::Feed(std::span<const std::byte> bytes)
{
    if (error)
    {
        return;
    }
    // Lo que ya salió se borra cuando es al menos tanto como lo que queda: correr lo que queda
    // cuesta a lo sumo lo que se borra, y cada byte se borra una sola vez. Borrarlo en cada Feed
    // correría todo lo pendiente cada vez, y sin sacar paquetes eso crece con cada lectura.
    if (consumed >= Pending())
    {
        buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed));
        unmasked -= consumed;
        consumed = 0;
    }
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
}

// Mira solo bytes que llegaron: el tipo con el primero, el tamaño con la cabecera entera, y el
// paquete cuando están todos sus bytes. Los errores de tamaño salen apenas llega la cabecera. Antes
// de mirar un byte lo desenmascara, y nunca uno del paquete siguiente, que puede llevar otro XOR.
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
    if (unmasked == consumed)
    {
        mask = obfuscation->InboundMask(pending.front());
    }
    UnmaskUpTo(consumed + 1);
    const std::optional<PacketType> type = PacketTypeFromByte(pending.front());
    if (!type)
    {
        return Fail(PacketError::InvalidType);
    }
    UnmaskUpTo(consumed + HeaderSize(*type));
    const std::optional<std::size_t> size = DeclaredSize(*type, pending);
    if (!size)
    {
        return std::optional<Packet>{};
    }
    if (const std::optional<PacketError> problem = CheckDeclaredSize(*type, *size))
    {
        return Fail(*problem);
    }
    UnmaskUpTo(consumed + *size);
    if (pending.size() < *size)
    {
        return std::optional<Packet>{};
    }
    // Con el tipo y el tamaño ya mirados, FromBytes no puede fallar: si fallara, Value() aborta,
    // porque sería un bug de este lector.
    const std::span<const std::byte> bytes = pending.first(*size);
    Packet packet = Packet::FromBytes({bytes.begin(), bytes.end()}).Value();
    consumed += *size;
    return std::optional<Packet>{std::move(packet)};
}

std::size_t PacketReader::Pending() const
{
    return buffer.size() - consumed;
}

void PacketReader::UnmaskUpTo(std::size_t end)
{
    const std::size_t stop = std::min(end, buffer.size());
    for (; unmasked < stop; ++unmasked)
    {
        buffer[unmasked] ^= mask;
    }
}

// El error queda, y lo que había se descarta: no se va a cortar nunca.
Core::Expected<std::optional<Packet>, PacketError> PacketReader::Fail(PacketError reason)
{
    error = reason;
    buffer.clear();
    consumed = 0;
    unmasked = 0;
    return Core::Unexpected(reason);
}

} // namespace Client::Network
