#pragma once

#include "Data/Terrain/TerrainGrid.h"

#include <cstdint>
#include <span>

// El mapa vivo de 0x7d0d348: los WORD del .att y el bit de personajes (0x508f00; 06 § Caminar).
namespace Client::World
{
inline constexpr std::uint16_t CharacterWallBit = 0x02;
inline constexpr std::uint16_t SafeZoneWallBit = 0x01;
// 0x508f60: un personaje con +0x88 en 8 no ocupa su celda.
inline constexpr std::uint8_t NonBlockingKind = 8;

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

// 0x508f14..0x508f34: el 0x02 fuera de los 65.536 WORD, al principio de cada paso de los personajes.
void ClearCharacterWalls(Data::Terrain::TerrainLayer<std::uint16_t>& cells);
// 0x509165: el 0x02 en la celda lógica de un personaje, por 0x5f82a0, que se queda con los ocho bits
// bajos de cada coordenada.
void MarkCharacterWall(Data::Terrain::TerrainLayer<std::uint16_t>& cells, int logicalX, int logicalY);

struct TerrainWalls
{
    Data::Terrain::TerrainLayer<std::uint16_t> cells{};

    // El dueño de las variables protegidas abre/cierra las coordenadas y consume sus rand().
    // Esta parte pura borra el 0x02 de toda la grilla y marca los ocupantes.
    void Rebuild(std::span<const TerrainOccupant> occupants);
};
} // namespace Client::World
