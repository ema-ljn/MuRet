#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// 06 § El paso de ondas de Kalima: proyección de los slots nativos 0/1 de 0x41dbe0.
namespace Client::World
{
inline constexpr std::size_t KalimaWaveSide = 256;
inline constexpr std::size_t KalimaWaveCellCount = KalimaWaveSide * KalimaWaveSide;
using KalimaWavePlane = std::array<std::uint32_t, KalimaWaveCellCount>;

struct KalimaWaveState
{
    std::array<KalimaWavePlane, 2> planes{};
    bool selected = false;
};

// El caller aloja los 512 KiB de planos directamente en heap, con un único dueño durante el paso.
// Acepta todos los patrones DWORD; actualiza el interior seleccionado desde el plano opuesto.
// Conserva fuente, bordes y selector. No reserva memoria ni adapta selectores raw nativos.
void UpdateKalimaWaves(KalimaWaveState& state);
} // namespace Client::World
