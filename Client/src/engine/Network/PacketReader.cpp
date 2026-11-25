#include "Network/PacketReader.h"

#include "Core/Expected.h"
#include "Network/IObfuscation.h"
#include "Network/IdentityObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketError.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>

namespace Client::Network
{
PacketReader::PacketReader() : PacketReader(IdentityObfuscation::Shared()) {}

PacketReader::PacketReader(const IObfuscation& obfuscation) : m_obfuscation(&obfuscation) {}

PacketReader::PacketReader(PacketReader&& other) noexcept
    : m_obfuscation(other.m_obfuscation), m_buffer(std::exchange(other.m_buffer, {})),
      m_consumed(std::exchange(other.m_consumed, 0)), m_unmasked(std::exchange(other.m_unmasked, 0)),
      m_mask(std::exchange(other.m_mask, std::byte{0})), m_error(std::exchange(other.m_error, std::nullopt))
{
}

PacketReader& PacketReader::operator=(PacketReader&& other) noexcept
{
    m_obfuscation = other.m_obfuscation;
    m_buffer = std::exchange(other.m_buffer, {});
    m_consumed = std::exchange(other.m_consumed, 0);
    m_unmasked = std::exchange(other.m_unmasked, 0);
    m_mask = std::exchange(other.m_mask, std::byte{0});
    m_error = std::exchange(other.m_error, std::nullopt);
    return *this;
}

void PacketReader::Feed(std::span<const std::byte> bytes)
{
    if (m_error)
    {
        return;
    }
    // Lo que ya salió se borra cuando es al menos tanto como lo que queda: correr lo que queda
    // cuesta a lo sumo lo que se borra, y cada byte se borra una sola vez. Borrarlo en cada Feed
    // correría todo lo pendiente cada vez, y sin sacar paquetes eso crece con cada lectura.
    if (m_consumed >= Pending())
    {
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(m_consumed));
        m_unmasked -= m_consumed;
        m_consumed = 0;
    }
    m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
}

// Mira solo bytes que llegaron: el tipo con el primero, el tamaño con la cabecera entera, y el
// paquete cuando están todos sus bytes. Los errores de tamaño salen apenas llega la cabecera. Antes
// de mirar un byte lo desenmascara, y nunca uno del paquete siguiente, que puede llevar otro XOR.
Core::Expected<std::optional<Packet>, PacketError> PacketReader::Next()
{
    if (m_error)
    {
        return Core::Unexpected(*m_error);
    }
    const std::span<const std::byte> pending = std::span<const std::byte>(m_buffer).subspan(m_consumed);
    if (pending.empty())
    {
        return std::optional<Packet>{};
    }
    const std::optional<PacketType> type = PacketTypeFromByte(UnmaskFirstByte());
    if (!type)
    {
        return Fail(PacketError::InvalidType);
    }
    UnmaskUpTo(m_consumed + HeaderSize(*type));
    const std::optional<std::size_t> size = DeclaredSize(*type, pending);
    if (!size)
    {
        return std::optional<Packet>{};
    }
    if (const std::optional<PacketError> problem = CheckDeclaredSize(*type, *size))
    {
        return Fail(*problem);
    }
    UnmaskUpTo(m_consumed + *size);
    if (pending.size() < *size)
    {
        return std::optional<Packet>{};
    }
    // Con el tipo y el tamaño ya mirados, FromBytes no puede fallar: si fallara, Value() aborta,
    // porque sería un bug de este lector.
    const std::span<const std::byte> bytes = pending.first(*size);
    Packet packet = Packet::FromBytes({bytes.begin(), bytes.end()}).Value();
    m_consumed += *size;
    return std::optional<Packet>{std::move(packet)};
}

std::size_t PacketReader::Pending() const
{
    return m_buffer.size() - m_consumed;
}

// Si el paquete es nuevo, su máscara sale del primer byte tal como llegó del cable.
std::byte PacketReader::UnmaskFirstByte()
{
    if (m_unmasked == m_consumed)
    {
        m_mask = m_obfuscation->InboundMask(m_buffer[m_consumed]);
    }
    UnmaskUpTo(m_consumed + 1);
    return m_buffer[m_consumed];
}

void PacketReader::UnmaskUpTo(std::size_t end)
{
    const std::size_t stop = std::min(end, m_buffer.size());
    for (; m_unmasked < stop; ++m_unmasked)
    {
        m_buffer[m_unmasked] ^= m_mask;
    }
}

// El error queda, y lo que había se descarta: no se va a cortar nunca.
Core::Expected<std::optional<Packet>, PacketError> PacketReader::Fail(PacketError reason)
{
    m_error = reason;
    m_buffer.clear();
    m_consumed = 0;
    m_unmasked = 0;
    return Core::Unexpected(reason);
}
} // namespace Client::Network
