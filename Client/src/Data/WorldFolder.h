#pragma once

#include <array>
#include <cstdint>
#include <string>

// La carpeta World<N> de cada mapa, como la arma 0x61a560 al cargar un mundo (05 § La carpeta de
// cada mapa; leída otra vez en el paso 1 de la CL5.4, *estático*). Con ese N se piden también sus
// archivos: "Data\%s\EncTerrain%d.map" (0x6d12e8), .att y .obj. Un mundo es un dato: nada en
// MuRet arma "World1" a mano.
namespace Client::Data
{
// Los mapas que llegan a 0x61a560: del 0 al 36, los del juego (los handlers que mandan un mapa
// pasan el byte del paquete sin mirar el rango, Q-C58), el 54 de la selección (0x633f44, en
// 0x633f20) y el 55 del login (0x6349b2, en 0x6349a0). Los tests de todos los mapas van por esta
// lista.
inline constexpr std::array<std::uint8_t, 39> OriginalMaps = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                                              13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                                                              26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 54, 55};

// Proyección local de 0x61a560 para un byte: argumento y mapa observado coinciden y no hay
// mutación entre las dos fases (05 § La carpeta de cada mapa). Compone MapAtWorldLoadStart y
// WorldFolderAfterModels: 32 → World10 (Q-C58: la guarda modifica el mapa global 0x6c9dd0);
// los alias de evento comparten carpeta, no identidad.
// Recibe cualquier byte y devuelve más de uno porque el 255 da 256.
[[nodiscard]] std::uint16_t WorldFolderNumber(std::uint8_t map);

// "World" y WorldFolderNumber, como el "World%d" de 0x6c7330: "World56" para el 55.
[[nodiscard]] std::string WorldFolder(std::uint8_t map);
} // namespace Client::Data
