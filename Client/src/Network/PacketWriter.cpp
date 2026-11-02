#include "Network/PacketWriter.h"

#include <utility>

namespace Client::Network
{
namespace
{
// Los bits de un byte: un entero de un campo se escribe de a un byte.
constexpr unsigned BitsPerByte = 8;

// Lo que mide el tipo de cada uno.
std::size_t MaxSize(PacketType type)
{
    return type == PacketType::C1 ? MaxC1Size : MaxWrittenPacketSize;
}
} // namespace

PacketWriter PacketWriter::C1(std::uint8_t opcode)
{
    return PacketWriter(PacketType::C1, opcode);
}

PacketWriter PacketWriter::C2(std::uint8_t opcode)
{
    return PacketWriter(PacketType::C2, opcode);
}

PacketWriter::PacketWriter(PacketType type, std::uint8_t opcode) : m_type(type), m_opcode(opcode) {}

PacketWriter& PacketWriter::PutU8(std::uint8_t value) &
{
    PutUnsigned(value, std::endian::little);
    return *this;
}

PacketWriter& PacketWriter::PutU16Le(std::uint16_t value) &
{
    PutUnsigned(value, std::endian::little);
    return *this;
}

PacketWriter& PacketWriter::PutU16Be(std::uint16_t value) &
{
    PutUnsigned(value, std::endian::big);
    return *this;
}

PacketWriter& PacketWriter::PutU32Le(std::uint32_t value) &
{
    PutUnsigned(value, std::endian::little);
    return *this;
}

PacketWriter& PacketWriter::PutBytes(std::span<const std::byte> more) &
{
    m_fields.insert(m_fields.end(), more.begin(), more.end());
    return *this;
}

// Adentro de cada uno, *this es un lvalue: la llamada va a la versión de arriba.
PacketWriter PacketWriter::PutU8(std::uint8_t value) &&
{
    PutU8(value);
    return std::move(*this);
}

PacketWriter PacketWriter::PutU16Le(std::uint16_t value) &&
{
    PutU16Le(value);
    return std::move(*this);
}

PacketWriter PacketWriter::PutU16Be(std::uint16_t value) &&
{
    PutU16Be(value);
    return std::move(*this);
}

PacketWriter PacketWriter::PutU32Le(std::uint32_t value) &&
{
    PutU32Le(value);
    return std::move(*this);
}

PacketWriter PacketWriter::PutBytes(std::span<const std::byte> more) &&
{
    PutBytes(more);
    return std::move(*this);
}

template <std::unsigned_integral T> void PacketWriter::PutUnsigned(T value, std::endian order)
{
    for (std::size_t k = 0; k < sizeof(T); ++k)
    {
        const std::size_t shift = order == std::endian::little ? k : sizeof(T) - 1 - k;
        m_fields.push_back(static_cast<std::byte>(value >> (BitsPerByte * shift)));
    }
}

Core::Expected<Packet, PacketError> PacketWriter::Finish() const
{
    const std::size_t size = MinPacketSize(m_type) + m_fields.size();
    if (size > MaxSize(m_type))
    {
        return Core::Unexpected(PacketError::TooLarge);
    }
    std::vector<std::byte> packet;
    packet.reserve(size);
    AppendHeader(packet, m_type, size);
    packet.push_back(static_cast<std::byte>(m_opcode));
    packet.insert(packet.end(), m_fields.begin(), m_fields.end());
    return Packet::FromBytes(std::move(packet));
}
} // namespace Client::Network
