#include "BitStream.h"

namespace Labs
{
namespace
{
// Bits de un byte, y el más alto de ellos: el primer bit del flujo va en el 0x80.
constexpr std::size_t ByteBits = 8;
constexpr unsigned HighBit = 0x80;

// Los bits de un std::uint32_t: lo que trae el valor de BitWriter::Write y lo más que entra en el
// resultado de BitReader::Read.
constexpr std::size_t ValueBits = 32;
} // namespace

void BitWriter::Write(std::uint32_t value, std::size_t bitCount)
{
    // Del bit bitCount - 1 al 0. Los que están por encima de los 32 de value son 0.
    for (std::size_t i = bitCount; i > 0; --i)
    {
        const std::size_t bit = i - 1;
        WriteBit(bit < ValueBits && ((value >> bit) & 1u) != 0);
    }
}

std::span<const std::byte> BitWriter::Bytes() const
{
    return m_bytes;
}

std::size_t BitWriter::BitCount() const
{
    return m_writtenBits;
}

// El bit n del flujo va en el byte n / 8, en la posición n % 8 contando desde el 0x80.
void BitWriter::WriteBit(bool bit)
{
    const std::size_t inByte = m_writtenBits % ByteBits;
    if (inByte == 0)
    {
        m_bytes.push_back(std::byte{0});
    }
    if (bit)
    {
        m_bytes.back() |= std::byte{static_cast<std::uint8_t>(HighBit >> inByte)};
    }
    ++m_writtenBits;
}

BitReader::BitReader(std::span<const std::byte> bytes) : m_buffer(bytes) {}

std::optional<std::uint32_t> BitReader::Read(std::size_t bitCount)
{
    if (bitCount > ValueBits || bitCount > RemainingBits())
    {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (std::size_t k = 0; k < bitCount; ++k)
    {
        value = (value << 1) | (BitAt(m_position + k) ? 1u : 0u);
    }
    m_position += bitCount;
    return value;
}

std::size_t BitReader::RemainingBits() const
{
    return m_buffer.size() * ByteBits - m_position;
}

bool BitReader::BitAt(std::size_t index) const
{
    const unsigned mask = HighBit >> (index % ByteBits);
    return (std::to_integer<unsigned>(m_buffer[index / ByteBits]) & mask) != 0;
}
} // namespace Labs
