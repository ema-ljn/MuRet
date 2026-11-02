#include "Network/Packet.h"

#include "Core/ByteReader.h"

#include <utility>

namespace Client::Network
{
namespace
{
// Cabecera de cada clase de tipo: el byte del tipo y el tamaño, en uno o en dos bytes.
constexpr std::size_t OneByteSizeHeader = 2;
constexpr std::size_t TwoByteSizeHeader = 3;

// Después de la cabecera va el opcode, en claro en C1/C2 y cifrado en C3/C4.
constexpr std::size_t OpcodeSize = 1;

// Dónde empieza el tamaño: después del byte del tipo.
constexpr std::size_t SizeOffset = 1;

// Los bits de un byte: un tamaño de dos bytes se escribe el de arriba primero.
constexpr unsigned BitsPerByte = 8;

bool HasOneByteSize(PacketType m_type)
{
    return m_type == PacketType::C1 || m_type == PacketType::C3;
}
} // namespace

std::optional<PacketType> PacketTypeFromByte(std::byte first)
{
    const auto m_type = static_cast<PacketType>(std::to_integer<std::uint8_t>(first));
    switch (m_type)
    {
    case PacketType::C1:
    case PacketType::C2:
    case PacketType::C3:
    case PacketType::C4:
        return m_type;
    }
    return std::nullopt;
}

std::size_t HeaderSize(PacketType m_type)
{
    return HasOneByteSize(m_type) ? OneByteSizeHeader : TwoByteSizeHeader;
}

std::size_t MinPacketSize(PacketType m_type)
{
    return HeaderSize(m_type) + OpcodeSize;
}

std::optional<std::size_t> DeclaredSize(PacketType m_type, std::span<const std::byte> header)
{
    if (header.size() < HeaderSize(m_type))
    {
        return std::nullopt;
    }
    Core::ByteReader reader(header.subspan(SizeOffset));
    if (HasOneByteSize(m_type))
    {
        return reader.ReadU8();
    }
    return reader.ReadU16Be();
}

// La conversión a std::byte se queda con el byte de abajo: es módulo 256.
void AppendHeader(std::vector<std::byte>& out, PacketType m_type, std::size_t size)
{
    out.push_back(static_cast<std::byte>(m_type));
    if (!HasOneByteSize(m_type))
    {
        out.push_back(static_cast<std::byte>(size >> BitsPerByte));
    }
    out.push_back(static_cast<std::byte>(size));
}

std::optional<PacketError> CheckDeclaredSize(PacketType m_type, std::size_t declared)
{
    if (declared < MinPacketSize(m_type))
    {
        return PacketError::SizeTooSmall;
    }
    if (declared > MaxPacketSize)
    {
        return PacketError::TooLarge;
    }
    return std::nullopt;
}

Core::Expected<Packet, PacketError> Packet::FromBytes(std::vector<std::byte> m_bytes)
{
    if (m_bytes.empty())
    {
        return Core::Unexpected(PacketError::SizeTooSmall);
    }
    const std::optional<PacketType> m_type = PacketTypeFromByte(m_bytes.front());
    if (!m_type)
    {
        return Core::Unexpected(PacketError::InvalidType);
    }
    const std::optional<std::size_t> declared = DeclaredSize(*m_type, m_bytes);
    if (!declared)
    {
        return Core::Unexpected(PacketError::SizeTooSmall);
    }
    if (const std::optional<PacketError> problem = CheckDeclaredSize(*m_type, *declared))
    {
        return Core::Unexpected(*problem);
    }
    if (*declared != m_bytes.size())
    {
        return Core::Unexpected(PacketError::SizeMismatch);
    }
    return Packet(*m_type, std::move(m_bytes));
}

Packet::Packet(PacketType type, std::vector<std::byte> bytes) : m_type(type), m_bytes(std::move(bytes)) {}

PacketType Packet::Type() const
{
    return m_type;
}

std::span<const std::byte> Packet::Bytes() const
{
    return m_bytes;
}

std::span<std::byte> Packet::MutableBody()
{
    if (m_bytes.size() < HeaderSize(m_type))
    {
        return {};
    }
    return std::span<std::byte>(m_bytes).subspan(HeaderSize(m_type));
}

std::vector<std::byte> Packet::TakeBytes() &&
{
    return std::exchange(m_bytes, {});
}

std::optional<std::uint8_t> Packet::Opcode() const
{
    if (m_type == PacketType::C3 || m_type == PacketType::C4 || m_bytes.size() < MinPacketSize(m_type))
    {
        return std::nullopt;
    }
    return std::to_integer<std::uint8_t>(m_bytes[HeaderSize(m_type)]);
}
} // namespace Client::Network
