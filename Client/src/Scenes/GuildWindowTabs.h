#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// Los dibujos de las tres pestañas de la ventana del gremio, [0x7d536f0] (CL9 hito 8, fase 5; 06 § La ventana del
// gremio): 0x64fdf0 (la 0, el gremio y su aviso), 0x6507d0 (los miembros) y 0x651400 (la alianza), con las listas
// sobre UI::OldList y su dibujo de la plantilla (0x47a810 y sus iguales 0x480520 y 0x481bf0).
namespace Client::Scenes
{
struct GameHudContext;

// 0x652291..0x6522c0: el dibujo de la pestaña `tab` después de las pestañas (0x651f1c..0x652275), con la negrita en
// blanco sobre 0 que dejaron.
//
// 0x64fdf0, la 0: el marco de 53 en (x + 0x45, y + 0x68) con la marca de 48 del gremio del héroe (0x5f16a0(+0x1f2,
// 1)); el marco de 140 × 62 en (x + 0x19, y + 0xaa) y en x + 0x1e, de a 0xd desde y + 0xb2, cada renglón en una caja de
// _ftol(rx · 140) (0x5542f0 en el modo 1): "%s :" con el 1332, el 1256 con el puntaje (0x78aba3c), los miembros
// (0x78aba38: el maestro, con el registro propio abierto (0x57630c8, su cierre saca los rand()), el 1362 con el tope
// de la clase 4, liderazgo / 10 + nivel / 10 hasta 0x50, o nivel / 10 en las otras; los demás, el 1310) y "%s : %s"
// con el 1321 y el nombre de +0x88 (o el 1361); el 1323 en y + 0xfa en _ftol(rx · 136); la lista del aviso de 0x88 ×
// 0x50 colgada de y + 0x154 en x + 0x1c; el marco de 140 × 95 en (x + 0x19, y + 0xf6), y el botón +0x42c con el 188
// (el maestro) o el 189.
//
// 0x6507d0, la de los miembros: con 0x78aba38 de más de 0, la lista +0x94 (0x47a810 con 0x46cb20 y 0x46d050); con el
// héroe maestro, los tres botones.
//
// 0x651400, la de la alianza: sin alianza (+4 de la entrada del héroe, sin mirar el −1), los textos 1257 a 1267 en
// x + 0x19 desde y + 0x6a (0x554180), los seis últimos en blanco sobre 0xff0000cc; con alianza, la lista +0x2f8 de
// 0xa0 × 0x50 en x + 0xf, colgada de y + 0x6e más el alto de la lista del aviso (+0x224, no el suyo: Q-C1293) y, con el
// héroe maestro, +0x64c con el 1324 y +0x6d4 a la vista si es el maestro de la alianza, o +0x64c con el 1325 y +0x6d4
// apagado (6).
[[nodiscard]] Core::Expected<void, std::string> DrawTheGuildTab(GameHudContext& context, std::int32_t tab);
} // namespace Client::Scenes
