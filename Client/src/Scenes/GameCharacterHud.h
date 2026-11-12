#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// La ventana del personaje en el HUD del juego (06 § La ventana del personaje, CL8 hito 5): 0x5ee2e0(0x1c2, 0) y lo
// que 0x5f7440 dibuja después, el botón de cerrar 0x118 con la ayuda del texto 222. En 640 × 480, con el estado de GL
// que deja y con los rand() de la tabla de protección en su orden: 3.848 por cada cierre del registro propio que lo
// deja sin accesos (entre 12 y 18 por frame, según el equipo y la clase) y 60 por el de la entrada de la skill en
// Skill.bmd (0x5513d0, las clases 0 y 3).
//
// Los textos van por ITextDrawer::DrawColumns, con la caja del ancho que les da 0x554180 (W · k / 640): el fondo la
// llena entera, y el que no entra se recorta como en 0x467060 (el modo 1 muestra el final y el 2 el centro).
namespace Client::Scenes
{
struct GameHudContext;

// 0x555a70(pk): el color del nombre (0x6c8e5c) por el +0x375 del héroe.
[[nodiscard]] std::uint32_t PkNameColor(std::uint8_t pk);

// 0x5ee2e0(0x1c2, 0) y el botón de 0x5f7440 (0x5f74a3..0x5f759a), con la ventana del personaje abierta. Corta con su
// dirección sin el servidor elegido (`chosenServer`, 0x5ee629), sin Text.bmd, Item.bmd o Skill.bmd donde los lee,
// con el nombre del registro sin su 0 en sus 11 bytes, con la skill del héroe fuera de los 64 bytes de +0x6c que tiene
// MuRet y con +0x46 en 0 y +0x44 en más de 10 (la división de 0x5f08ac, que en el original es una excepción).
[[nodiscard]] Core::Expected<void, std::string> DrawTheCharacterWindow(GameHudContext& context);
} // namespace Client::Scenes
