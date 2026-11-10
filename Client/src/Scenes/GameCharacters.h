#pragma once

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Scenes/ActorState.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// Los personajes en el dibujo del juego (0x6374dd, 0x510d00), hasta donde MuRet los tiene. Contrato: 06
// § El dibujo del juego.
namespace Client::Scenes
{
// 0x510d00: el bit del CtlCode (+0x1d8) con el que el héroe no se dibuja en la escena 5.
inline constexpr std::uint8_t UndrawnHeroControlBit = 0x04;

// 0x510d00 en la escena 5 (*estático*), lugar por lugar de los 400 del pool, sin rand(). Con otro que el
// héroe, la rama del asedio (0x4294b0 lee 0x7239ec, que MuRet deja en 0, P-C41 c) no corre. El héroe
// (`hero`) con +0x1d8 & 4 no se dibuja: 0x510e57 le pone +0x13c en 1.000 y +0x148, +0x158 y +0x168 en 1.
// Los demás, vivos y vistos (+0x16c), van a 0x50ba30, que MuRet tiene para el login y la selección
// (DrawCharacter) pero no en el juego: corta ahí (0x510ea1), con los de antes ya mirados. Al final,
// 0x42da60: 0x7239dc y 0x7239e0 en 0 (MuRet no los tiene) y, con 0x7239d9 en 0 (solo el mapa 30 lo pone
// en 1), World::OpenTheGates sobre `walls` (Q-C276).
[[nodiscard]] Core::Expected<void, std::string> DrawGameCharacters(std::span<ActorState> actors, std::size_t hero,
                                                                   Data::Terrain::TerrainLayer<std::uint16_t>& walls);
} // namespace Client::Scenes
