#pragma once

#include "Core/Expected.h"

#include <string>

// El nombre de un ítem del piso (CL9 hito 5; 06 § Los ítems del piso): 0x5ba790, que llaman el hover con el elegido
// (0x5bbd62) y Alt con los demás (0x5bbdd2). *Estático*, leído en el ASM.
namespace Client::Scenes
{
struct FloorItemState;
struct GameHudContext;

// 0x5ba790(índice, objeto, +8, +0x1f, +0x20, enElÍtem): la fuente normal y los colores (0xffffffff, 0xff000000); el
// texto y su color según el modelo (+2 del objeto, el tipo + 0x212 o el que le dio 0x608350 por el nivel): el zen, las
// joyas, las cajas, los fuegos, las frutas, el 0x1a1d (la skill nivel + 30 de Skill.bmd, con su acceso) y los demás con
// el color del nivel y de las opciones, el nombre del conjunto (0x40cf30, que pasa a blanco, negrita y a
// 0xff00ff00 sobre 0xffc83c3c), " +nivel" y los textos de la skill, la opción y la suerte. Lo dibuja con 0x554180
// centrado (la mitad del ancho con signo, por 640 y por W sin signo) y 15 más arriba del mouse o, con `atTheItem`, de
// la proyección del ítem (+0x5c y +0x5e). Termina con los colores en (0xffc8e6ff, 0xff000000). Dos casos cortan: el
// 0x1a0a, que lee los monstruos (0x7681eb8, CL9 hito 2), y los modelos y niveles con los que el original no escribe el
// texto y dibuja lo que quedó en su pila (07 Q-C1242).
[[nodiscard]] Core::Expected<void, std::string> DrawTheFloorItemName(GameHudContext& context,
                                                                     const FloorItemState& item, bool atTheItem);
} // namespace Client::Scenes
