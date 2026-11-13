#pragma once

#include "Core/Expected.h"
#include "Scenes/MonsterBodyPasses.h"

#include <string>

namespace Client::Scenes
{
// 0x6002e0 desde 0x600436, con +0x58 distinto de −2 y el cuarto argumento de 0x602e90 fuera del 10: el cuerpo de un
// monstruo o un NPC con sus casos por tipo y por mapa, sobre el modelo que dejó 0x5ffce0 (su color y su luz por
// vértice) y las mallas de 0x4ebc80. En orden:
// - los colores del 0x171 en el 10, el 0x164 en el 9 y el 0x16d con el cuarto argumento (0x600436..0x6004a4);
// - el 0x16c (0x6004ed): con (0,4; 0,6; 1) y el +0x88 en 0, RenderBody(2) con Chrome01 (0x5c0); con el color en 1 y
//   el +0x88 en 0xff, RenderBody(2), y sale sin lo de después del cuerpo;
// - los colores de +0x78 & 1 y & 2 (0x600e69) y la luz del 0x175, el 0x161, el 0x184 y el 0x185 (0x600ec4);
// - la cadena (0x438fe0, 0x42cc10, 0x4403b0, 0x42f170, 0x425d90 y 0x4306a0, MonsterBodyChain.h): la primera que
//   dibuja va directo a 0x602365;
// - los casos de después de la cadena (0x600f74..0x60235c) o el RenderBody(2) común (0x60235e);
// - 0x602365: con +0x78 & 2, RenderBody(2) con el blendMesh en −2 y la luz en 1; el 0x177 (0x602391) y el 0x1f3 y el
//   0x1f4 (0x60243e).
// Los casos de los tipos que ninguna alta de monstruo da (MonsterKinds) quedan cortados con su dirección, y también
// lo que no está portado: los efectos de 0x4ee770 (CL11), la clase de los bordes del 0x183 (0x6011f1) y la onda de
// 0x4ecc30 (la bandera 0x400, 0x4ed775) del 0x185.
[[nodiscard]] Core::Expected<void, std::string> DrawMonsterBody(MonsterBodyFrame& frame);
} // namespace Client::Scenes
