#include "Data/ByteReader.h"

namespace
{

constexpr std::size_t BitsPerByte = 8;

} // namespace

namespace Client::Data
{

ByteReader::ByteReader(std::span<const std::byte> bytes) : buffer(bytes) {}

std::optional<std::uint8_t> ByteReader::ReadU8()
{
    const std::optional<std::uint32_t> value = ReadLittleEndian(sizeof(std::uint8_t));
    if (!value)
    {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(*value);
}

std::optional<std::uint16_t> ByteReader::ReadU16Le()
{
    const std::optional<std::uint32_t> value = ReadLittleEndian(sizeof(std::uint16_t));
    if (!value)
    {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(*value);
}

std::optional<std::uint32_t> ByteReader::ReadU32Le()
{
    return ReadLittleEndian(sizeof(std::uint32_t));
}

std::size_t ByteReader::Remaining() const
{
    return buffer.size() - position;
}

// El byte k del campo va en los bits 8k a 8k+7 del resultado. std::to_integer lo lee sin signo:
// 0xFF es 255 y no se extiende como un -1.
std::optional<std::uint32_t> ByteReader::ReadLittleEndian(std::size_t size)
{
    if (Remaining() < size)
    {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (std::size_t k = 0; k < size; ++k)
    {
        const std::uint32_t byte = std::to_integer<std::uint32_t>(buffer[position + k]);
        value |= byte << (BitsPerByte * k);
    }
    position += size;
    return value;
}

} // namespace Client::Data
