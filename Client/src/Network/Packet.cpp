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

bool HasOneByteSize(PacketType type)
{
    return type == PacketType::C1 || type == PacketType::C3;
}
} // namespace

std::optional<PacketType> PacketTypeFromByte(std::byte first)
{
    const auto type = static_cast<PacketType>(std::to_integer<std::uint8_t>(first));
    switch (type)
    {
    case PacketType::C1:
    case PacketType::C2:
    case PacketType::C3:
    case PacketType::C4:
        return type;
    }
    return std::nullopt;
}

std::size_t HeaderSize(PacketType type)
{
    return HasOneByteSize(type) ? OneByteSizeHeader : TwoByteSizeHeader;
}

std::size_t MinPacketSize(PacketType type)
{
    return HeaderSize(type) + OpcodeSize;
}

std::optional<std::size_t> DeclaredSize(PacketType type, std::span<const std::byte> header)
{
    if (header.size() < HeaderSize(type))
    {
        return std::nullopt;
    }
    Core::ByteReader reader(header.subspan(SizeOffset));
    if (HasOneByteSize(type))
    {
        return reader.ReadU8();
    }
    return reader.ReadU16Be();
}

// La conversión a std::byte se queda con el byte de abajo: es módulo 256.
void AppendHeader(std::vector<std::byte>& out, PacketType type, std::size_t size)
{
    out.push_back(static_cast<std::byte>(type));
    if (!HasOneByteSize(type))
    {
        out.push_back(static_cast<std::byte>(size >> BitsPerByte));
    }
    out.push_back(static_cast<std::byte>(size));
}

std::optional<PacketError> CheckDeclaredSize(PacketType type, std::size_t declared)
{
    if (declared < MinPacketSize(type))
    {
        return PacketError::SizeTooSmall;
    }
    if (declared > MaxPacketSize)
    {
        return PacketError::TooLarge;
    }
    return std::nullopt;
}

Core::Expected<Packet, PacketError> Packet::FromBytes(std::vector<std::byte> bytes)
{
    if (bytes.empty())
    {
        return Core::Unexpected(PacketError::SizeTooSmall);
    }
    const std::optional<PacketType> type = PacketTypeFromByte(bytes.front());
    if (!type)
    {
        return Core::Unexpected(PacketError::InvalidType);
    }
    const std::optional<std::size_t> declared = DeclaredSize(*type, bytes);
    if (!declared)
    {
        return Core::Unexpected(PacketError::SizeTooSmall);
    }
    if (const std::optional<PacketError> problem = CheckDeclaredSize(*type, *declared))
    {
        return Core::Unexpected(*problem);
    }
    if (*declared != bytes.size())
    {
        return Core::Unexpected(PacketError::SizeMismatch);
    }
    return Packet(*type, std::move(bytes));
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
