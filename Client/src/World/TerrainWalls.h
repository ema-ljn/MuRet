#pragma once

#include "Data/Terrain/TerrainGrid.h"

#include <cstdint>
#include <span>

// El mapa vivo de 0x7d0d348: los WORD del .att y el bit de personajes (0x508f00; 06 § Caminar).
namespace Client::World
{
inline constexpr std::uint16_t CharacterWallBit = 0x02;
inline constexpr std::uint16_t SafeZoneWallBit = 0x01;

// Coordenadas lógicas ya decodificadas (+0x41c/+0x420), no la posición dibujada. Las condiciones
// son las de 0x508f49..0x508f67; también se incluye al héroe.
struct TerrainOccupant
{
    int logicalX = 0;
    int logicalY = 0;
    bool live = false;
    bool dying = false;
    std::uint8_t kind = 0;
};

struct TerrainWalls
{
    Data::Terrain::TerrainLayer<std::uint16_t> cells{};

    // El dueño de las variables protegidas abre/cierra las coordenadas y consume sus rand().
    // Esta parte pura borra el 0x02 de toda la grilla y marca los ocupantes.
    void Rebuild(std::span<const TerrainOccupant> occupants);
};
} // namespace Client::World
