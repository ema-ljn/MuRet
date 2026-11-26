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
    // What already left is erased once it is at least as much as what remains: shifting the rest then costs at most
    // what is erased, and each byte is erased once. Erasing on every Feed would shift everything pending each time.
    if (m_consumed >= Pending())
    {
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(m_consumed));
        m_unmasked -= m_consumed;
        m_consumed = 0;
    }
    m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
}

// Only looks at bytes that arrived; size errors come out as soon as the header arrives. Each byte is unmasked before
// it is looked at, never one of the next packet, which may carry another XOR.
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
    // With the type and the size already checked FromBytes cannot fail; if it did, Value() aborts: a bug in this
    // reader.
    const std::span<const std::byte> bytes = pending.first(*size);
    Packet packet = Packet::FromBytes({bytes.begin(), bytes.end()}).Value();
    m_consumed += *size;
    return std::optional<Packet>{std::move(packet)};
}

std::size_t PacketReader::Pending() const
{
    return m_buffer.size() - m_consumed;
}

// A new packet's mask comes from its first byte as it came off the wire.
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

// The error stays and what was pending is dropped: it will never be cut.
Core::Expected<std::optional<Packet>, PacketError> PacketReader::Fail(PacketError reason)
{
    m_error = reason;
    m_buffer.clear();
    m_consumed = 0;
    m_unmasked = 0;
    return Core::Unexpected(reason);
}
} // namespace Client::Network
