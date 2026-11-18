#pragma once

#include "Data/Terrain/TerrainGrid.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

// El mapa vivo de 0x7d0d348: los WORD del .att y el bit de personajes (0x508f00; 06 § Caminar).
namespace Client::World
{
inline constexpr std::uint16_t CharacterWallBit = 0x02;
inline constexpr std::uint16_t SafeZoneWallBit = 0x01;
// El bit de la pared que no se pasa, el que 0x42a110 abre.
inline constexpr std::uint16_t BlockedWallBit = 0x04;
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

// Una de las seis celdas de 0x6c72bc, de a dos bytes (x, y).
struct GateCell
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

// 0x6c72bc: las seis celdas que recorre 0x42a110. *Inferido*: las puertas del castillo del mapa 30 (el
// único que pone 0x7239d9 en 1, 0x42db10).
inline constexpr std::array<GateCell, 6> GateCells{
    {{67, 114}, {93, 114}, {119, 114}, {81, 161}, {107, 161}, {93, 204}}};

// 0x42a110(0, 0, 0, 1), el que llama 0x42da40 al final de 0x510d00 con 0x7239d9 en 0 (*estático*): con
// cada celda (x, y) de GateCells, si la de abajo (x, y + 1) tiene el 0x04 en su byte bajo, 0x5f8750(x − 1,
// y + 1, 4, 1, 4, 0) lo saca (0x5f8730, and con ~0x04) de las cuatro de x − 1 a x + 2 en esa fila. Corre
// en todos los mapas (Q-C276).
void OpenTheGates(Data::Terrain::TerrainLayer<std::uint16_t>& cells);

// 0x558800(x, y, tx, ty): si se ve la celda (tx, ty) desde (x, y), con el Bresenham de los WORD de 0x7d0d348 desde el
// índice de 0x5f82a0(x, y) (los ocho bits bajos de cada una). El eje largo es el de la diferencia mayor (con las dos
// iguales, la Y); cada paso suma la corta a un error que, mayor que la mitad de la larga (división con signo hacia el
// 0), mueve también por la corta y le resta la larga. Mira las celdas del 0 a la larga, las dos puntas incluidas: una
// con el WORD en 4 o más y sin 0x20, 0x40 ni 0x80 en su byte bajo tapa. Los pasos son lineales (±1 y ±0x100), así
// que una punta fuera de la grilla sigue por la fila de al lado o sale del arreglo: nullopt donde el original leería
// fuera de los 65.536 WORD.
[[nodiscard]] std::optional<bool> CanSeeTheCell(const Data::Terrain::TerrainLayer<std::uint16_t>& cells, int x, int y,
                                                int targetX, int targetY);

struct TerrainWalls
{
    Data::Terrain::TerrainLayer<std::uint16_t> cells{};

    // El dueño de las variables protegidas abre/cierra las coordenadas y consume sus rand().
    // Esta parte pura borra el 0x02 de toda la grilla y marca los ocupantes.
    void Rebuild(std::span<const TerrainOccupant> occupants);
};
} // namespace Client::World
