#include "Core/ByteReader.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace
{
// Cuánto se corre cada byte del campo para llegar a su lugar en el resultado.
constexpr std::size_t BitsPerByte = 8;
} // namespace

namespace Client::Core
{
ByteReader::ByteReader(std::span<const std::byte> buffer) : m_buffer(buffer) {}

std::optional<std::uint8_t> ByteReader::ReadU8()
{
    const std::optional<std::uint32_t> value = ReadUnsigned(sizeof(std::uint8_t), std::endian::little);
    if (!value)
    {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(*value);
}

std::optional<std::uint16_t> ByteReader::ReadU16Le()
{
    const std::optional<std::uint32_t> value = ReadUnsigned(sizeof(std::uint16_t), std::endian::little);
    if (!value)
    {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(*value);
}

std::optional<std::uint32_t> ByteReader::ReadU32Le()
{
    return ReadUnsigned(sizeof(std::uint32_t), std::endian::little);
}

std::optional<std::uint16_t> ByteReader::ReadU16Be()
{
    const std::optional<std::uint32_t> value = ReadUnsigned(sizeof(std::uint16_t), std::endian::big);
    if (!value)
    {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(*value);
}

std::optional<std::span<const std::byte>> ByteReader::ReadBytes(std::size_t count)
{
    if (Remaining() < count)
    {
        return std::nullopt;
    }
    const std::span<const std::byte> field = m_buffer.subspan(m_position, count);
    m_position += count;
    return field;
}

std::size_t ByteReader::Remaining() const
{
    return m_buffer.size() - m_position;
}

// En little-endian el byte k del campo va en los bits 8k a 8k+7 del resultado; en big-endian, el
// orden de significancia se da vuelta y el byte k va en el lugar size - 1 - k. std::to_integer lo
// lee sin signo: 0xFF es 255 y no se extiende como un -1.
std::optional<std::uint32_t> ByteReader::ReadUnsigned(std::size_t size, std::endian order)
{
    if (Remaining() < size)
    {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (std::size_t k = 0; k < size; ++k)
    {
        const std::uint32_t byte = std::to_integer<std::uint32_t>(m_buffer[m_position + k]);
        const std::size_t significance = order == std::endian::little ? k : size - 1 - k;
        value |= byte << (BitsPerByte * significance);
    }
    m_position += size;
    return value;
}
} // namespace Client::Core
