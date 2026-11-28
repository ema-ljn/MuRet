#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// 06 § El paso de ondas de Kalima: a projection of the native slots 0/1 of 0x41dbe0.
namespace Client::MapScripts::Kalima
{
inline constexpr std::size_t KalimaWaveSide = 256;
inline constexpr std::size_t KalimaWaveCellCount = KalimaWaveSide * KalimaWaveSide;
using KalimaWavePlane = std::array<std::uint32_t, KalimaWaveCellCount>;

struct KalimaWaveState
{
    std::array<KalimaWavePlane, 2> planes{};
    bool selected = false;
};

// The caller puts the 512 KiB of planes straight on the heap, with a single owner during the step. Accepts every
// DWORD pattern; updates the selected interior from the opposite plane. Keeps source, edges and selector. Does not
// allocate memory or adapt raw native selectors.
void UpdateKalimaWaves(KalimaWaveState& state);
} // namespace Client::MapScripts::Kalima
