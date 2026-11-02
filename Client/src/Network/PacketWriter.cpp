#include "Network/PacketWriter.h"

#include <utility>

namespace Client::Network
{

namespace
{

// Dónde va el tamaño, después del byte del tipo, y los bits de un byte para partir el de un C2.
constexpr std::size_t SizeOffset = 1;
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

PacketWriter::PacketWriter(PacketType packetType, std::uint8_t opcode)
    : type(packetType), bytes(HeaderSize(packetType), std::byte{0})
{
    bytes.front() = static_cast<std::byte>(packetType);
    bytes.push_back(static_cast<std::byte>(opcode));
}

PacketWriter& PacketWriter::PutU8(std::uint8_t value)
{
    bytes.push_back(static_cast<std::byte>(value));
    return *this;
}

PacketWriter& PacketWriter::PutU16Le(std::uint16_t value)
{
    PutLittleEndian(value, sizeof(value));
    return *this;
}

PacketWriter& PacketWriter::PutU16Be(std::uint16_t value)
{
    bytes.push_back(static_cast<std::byte>(value >> BitsPerByte));
    bytes.push_back(static_cast<std::byte>(value));
    return *this;
}

PacketWriter& PacketWriter::PutU32Le(std::uint32_t value)
{
    PutLittleEndian(value, sizeof(value));
    return *this;
}

PacketWriter& PacketWriter::PutBytes(std::span<const std::byte> more)
{
    bytes.insert(bytes.end(), more.begin(), more.end());
    return *this;
}

void PacketWriter::PutLittleEndian(std::uint32_t value, std::size_t size)
{
    for (std::size_t k = 0; k < size; ++k)
    {
        bytes.push_back(static_cast<std::byte>(value >> (BitsPerByte * k)));
    }
}

Core::Expected<Packet, PacketError> PacketWriter::Finish() const
{
    if (bytes.size() > MaxSize(type))
    {
        return Core::Unexpected(PacketError::TooLarge);
    }
    std::vector<std::byte> packet = bytes;
    const std::size_t size = packet.size();
    if (type == PacketType::C1)
    {
        packet[SizeOffset] = static_cast<std::byte>(size);
    }
    else
    {
        packet[SizeOffset] = static_cast<std::byte>(size >> BitsPerByte);
        packet[SizeOffset + 1] = static_cast<std::byte>(size);
    }
    return Packet::FromBytes(std::move(packet));
}

} // namespace Client::Network
