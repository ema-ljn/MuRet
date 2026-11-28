#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Los tres objetos de 0x412140: base de 0x20 bytes, tabla de 7 × 5 en la clase 1 y vector más tabla en la 2.
namespace Client::CastleSiege
{
inline constexpr std::size_t SiegeStatusSlots = 7;
struct SiegePanelState
{
    std::int32_t offsetX = 0; // +4 y +8, la ventana del mapa que deja el dibujo
    std::int32_t offsetY = 0;
    std::int32_t x = 489;     // +0xc
    std::int32_t y = 259;     // +0x10
    std::int32_t page = 1;    // +0x14, escala 1 o 2
    std::uint8_t visible = 1; // +0x19
    float alpha = 1.0f;       // +0x1c
    // +0x20 (clase 1), +0x34 (clase 2). Orden: número, x, y, estado, duración.
    std::array<std::array<std::uint8_t, 5>, SiegeStatusSlots> status{};
    // Clase 2: +0x20 inicialmente 1; 0x411ed0 lo deja en 0 después de cada append.
    std::uint8_t membersHidden = 1;
    std::vector<std::array<std::uint8_t, 3>> members{};
    // +0x57..+0x5b de la clase 2: fila elegida, coordenadas, orden y duración.
    std::uint8_t selected = 0;
    std::uint8_t targetX = 0;
    std::uint8_t targetY = 0;
    std::uint8_t command = 3;
    std::uint8_t targetTtl = 0;
};
} // namespace Client::CastleSiege
