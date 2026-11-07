#include "World/KalimaWaves.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace Client::World
{
void UpdateKalimaWaves(KalimaWaveState& state)
{
    constexpr unsigned DampingShift = 4;
    const std::size_t selected = state.selected ? 1U : 0U;
    auto& destination = state.planes[selected];
    const auto& source = state.planes[selected ^ 1U];
    for (std::size_t row = 1; row < KalimaWaveSide - 1; ++row)
    {
        for (std::size_t column = 1; column < KalimaWaveSide - 1; ++column)
        {
            const std::size_t cell = row * KalimaWaveSide + column;
            std::uint32_t sumBits = source[cell - KalimaWaveSide];
            sumBits += source[cell - 1];
            sumBits += source[cell + 1];
            sumBits += source[cell + KalimaWaveSide];
            // 0x41dc2c..0x41dc58: wrap DWORD antes de cada SAR, aritmético en C++20.
            const std::int32_t half = std::bit_cast<std::int32_t>(sumBits) >> 1;
            const std::uint32_t halfBits = std::bit_cast<std::uint32_t>(half);
            const std::uint32_t differenceBits = halfBits - destination[cell];
            const std::int32_t damp = std::bit_cast<std::int32_t>(differenceBits) >> DampingShift;
            const std::uint32_t dampBits = std::bit_cast<std::uint32_t>(damp);
            destination[cell] = differenceBits - dampBits;
        }
    }
}
} // namespace Client::World
