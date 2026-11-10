#include "Core/Crash/Crc32.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Core::Crash
{
namespace
{
constexpr std::uint32_t Polynomial = 0xEDB88320;
constexpr std::size_t TableSize = 256;
constexpr int BitsPerByte = 8;

// Lo que arma 0x683e00: para cada byte, ocho pasos de corrimiento con el polinomio.
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

// El paso de 0x683e75: el byte con el de abajo del estado elige la entrada de la tabla.
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
} // namespace Client::Core::Crash
