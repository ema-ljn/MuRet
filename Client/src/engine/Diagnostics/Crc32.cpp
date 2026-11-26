#include "Diagnostics/Crc32.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Diagnostics
{
namespace
{
constexpr std::uint32_t Polynomial = 0xEDB88320;
constexpr std::size_t TableSize = 256;
constexpr int BitsPerByte = 8;

// As 0x683e00 builds it: for each byte, eight shift steps with the polynomial.
constexpr std::array<std::uint32_t, TableSize> MakeTable()
{
    std::array<std::uint32_t, TableSize> table{};
    for (std::uint32_t index = 0; index < TableSize; ++index)
    {
        std::uint32_t value = index;
        for (int bit = 0; bit < BitsPerByte; ++bit)
        {
            value = (value & 1) != 0 ? (value >> 1) ^ Polynomial : value >> 1;
        }
        table[index] = value;
    }
    return table;
}

constexpr std::array<std::uint32_t, TableSize> Table = MakeTable();
} // namespace

// The 0x683e75 step: the byte with the state's low byte picks the table entry.
void Crc32::Update(std::span<const std::byte> bytes)
{
    constexpr std::uint32_t LowByte = 0xFF;
    for (const std::byte value : bytes)
    {
        const std::uint32_t index = (m_state ^ std::to_integer<std::uint32_t>(value)) & LowByte;
        m_state = (m_state >> BitsPerByte) ^ Table[index];
    }
}

std::uint32_t Crc32::Value() const
{
    return ~m_state;
}
} // namespace Client::Diagnostics
