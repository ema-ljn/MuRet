#include "Network/LayoutReader.h"

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketCode.h"
#include "Network/PacketLayout.h"
#include "Network/ProtocolError.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace
{
constexpr std::size_t BitsPerByte = 8;
} // namespace

namespace Client::Network
{
LayoutReader::LayoutReader(std::span<const std::byte> bytes) : m_bytes(bytes) {}

ProtocolResult<LayoutReader> LayoutReader::Open(const Packet& packet, const PacketLayout& layout)
{
    const PacketCode& code = layout.code;
    // Opcode() is std::nullopt in a C3/C4, which carries it encrypted: also another packet.
    if (packet.Type() != code.type || packet.Opcode() != code.opcode)
    {
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
    const std::span<const std::byte> bytes = packet.Bytes();
    if (code.subcode)
    {
        const std::size_t subcodeAt = SubcodeOffset(code.type);
        if (bytes.size() <= subcodeAt)
        {
            return Core::Unexpected(ProtocolError::TooShort);
        }
        if (std::to_integer<std::uint8_t>(bytes[subcodeAt]) != ByteOf(*code.subcode))
        {
            return Core::Unexpected(ProtocolError::WrongPacket);
        }
    }
    if (bytes.size() < layout.minimumSize)
    {
        return Core::Unexpected(ProtocolError::TooShort);
    }
    return LayoutReader(bytes);
}

std::span<const std::byte> LayoutReader::Bytes(std::size_t offset, std::size_t count)
{
    if (offset > m_bytes.size() || count > m_bytes.size() - offset)
    {
        if (!m_error)
        {
            m_error = ProtocolError::TooShort;
        }
        return {};
    }
    return m_bytes.subspan(offset, count);
}

std::size_t LayoutReader::Size() const
{
    return m_bytes.size();
}

// Little-endian puts byte k at bits 8k to 8k+7; big-endian at place size - 1 - k.
std::uint32_t LayoutReader::Unsigned(std::size_t offset, std::size_t size, std::endian order)
{
    const std::span<const std::byte> bytes = Bytes(offset, size);
    std::uint32_t value = 0;
    for (std::size_t k = 0; k < bytes.size(); ++k)
    {
        const std::size_t place = order == std::endian::little ? k : bytes.size() - 1 - k;
        value |= std::to_integer<std::uint32_t>(bytes[k]) << (place * BitsPerByte);
    }
    return value;
}
} // namespace Client::Network
