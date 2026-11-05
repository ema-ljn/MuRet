#pragma once

namespace Client::Data::Terrain
{
struct TerrainAttributes;
struct TerrainHeights;
} // namespace Client::Data::Terrain

// La altura del terreno en un punto del mundo (CL7.6 B3): 0x5f93c0, que piden las brasas del login al
// caer. Verificado contra el código real emulado (tests/vectors/world55/leaves.txt).
namespace Client::World
{
// La de una celda con el 0x40 de su atributo, o fuera de la grilla (0x6c9d5c).
inline constexpr float FixedTerrainHeight = 1200.0f;

// 0x5f93c0: con x o y negativos, 0; la celda de (x · 0,01, y · 0,01), guardados en float y con ftol, fuera
// de la grilla o con el 0x40 de su atributo, 1.200; si no, entre las alturas de las cuatro esquinas (la
// fila y la siguiente, con la vuelta de 255 a 0), primero por la fracción de y (guardada en float) en cada
// columna y después por la de x (en la pila), todo en 53 bits. Devuelve lo que queda en la pila de la
// x87. Sin la bandera de escena de 0x6d5dc8 (en el login, la escena 2, no la mira).
[[nodiscard]] double TerrainHeightAt(const Data::Terrain::TerrainHeights& heights,
                                     const Data::Terrain::TerrainAttributes& attributes, float x, float y);
} // namespace Client::World
