#pragma once

#include "Core/Expected.h"

#include <string>

// Los dibujos del gremio (CL9 hito 8, fase 2; 06 § El gremio en pantalla): el marcador de la guerra, el 2.º hijo del
// HUD (0x5a7640), y el diálogo de los paneles (0x660830), el último de 0x6579e0. En 640 × 480, con el estado de GL que
// deja y los globales de los textos (0x6c8e5c, 0x6c8e64 y la fuente del DC) en HudState.
namespace Client::UI
{
class PanelRegistry;
struct PanelDialogState;
} // namespace Client::UI

namespace Client::Scenes
{
struct GameHudContext;

// 0x5a7640: siempre 0x620980 (sin mezcla) y el fondo de los textos en 0x80000000. Con la guerra (0x576796c) y el héroe
// con gremio (+0x1f2 distinto de −1): la mezcla normal, x = ancho del mundo (0x5bb8d0, una vez, con sus consultas) ·
// 0,5 − 20 en float y la negrita (0x5763558); el color del texto 0xff003cff (0xffff9600 con el equipo, 0x576796e); la
// marca del gremio del héroe (0x5f16a0 con el fondo transparente) de 8 × 8 en (x, 4), su puntaje (0x57678b4,
// "%d") en (_ftol(x) + 13, 4) y el nombre de su gremio (+0xd de la entrada) en (_ftol(x) + 30, 4); el otro color, la
// marca del rival (la primera entrada de 0 a 1998 con su nombre, 0x5767804; sin ninguna, la 0) en (x, 19), su puntaje
// (0x57678b8) en (_ftol(x) + 13, 19) y su nombre (0x5542f0) en (_ftol(x) + 30, 19). Cada texto vacío no se dibuja,
// pero se mide (0x77f31b8). El segundo marcador (0x576796f, el F3 23 de 0x4d9f00, hito 11) no lo prende nadie en MuRet
// (P-C284). Sin las marcas, la guerra corta.
[[nodiscard]] Core::Expected<void, std::string> DrawTheWarScore(GameHudContext& context, UI::PanelRegistry& panels);

// 0x660830, abierto: con el dibujo propio de 0x6602b0 (+0xc), solo ese (DrawTheGuildPositions). Si no, la caja de
// 0x6576a0 (0x620c10, glColor4f(1, 1, 1, 0,8), Message_box2 (0xfc) de 213 × 5 en
// (214, y), una fila de Message_box1 (0xfb) de 213 × 40 por cada 40 de (alto − 10), el resto escalado y otra vez la
// 0xfc; después el blanco y 0x620980), con y = 0x82 − alto / 2 (nunca menos de 40) o 10 con +0xa24. Después la fuente
// normal (0x5763554), la mezcla normal y los colores −1 y 0; cada renglón en (0x140 − ancho / 2, y + 0x14 + 15 · i)
// con 0x5542f0, que no dibuja uno vacío ni mide; y los botones (0x45a9d0 y 0x45b2e0) en y + 9: Aceptar en 0x127, o
// Sí y No, o Confirmar y Cancelar en 0x104 y 0x14a. El campo de texto (0x10) y el reloj (8) cortan: ningún diálogo de
// MuRet los abre (P-C285). No saca rand().
[[nodiscard]] Core::Expected<void, std::string> DrawThePanelDialog(GameHudContext& context,
                                                                   UI::PanelDialogState& dialog);

// 0x64f1c0, el dibujo de la ventana de los cargos (+0xc del diálogo de los paneles, en lugar de todo): la caja de
// 0x6576a0 de 0x96 de alto, con su esquina (0xd6, 0x37) en 0x7d536d0 y 0x7d536d4 para el paso; 0x620a00(1); las dos
// cajas de 0x471520 de 170 × 20 en (x + 0x14, y + 0x13) y (x + 0x14, y + 0x27), rellena la del cargo de 0x7d536d8 (0x40
// o 0x20); los textos 1311 (y + 0x18), 1312 (y + 0x2e), el 1314 con el nombre de 0x7d535a8 y el 1301 o el 1302
// (y + 0x4b) y el 1315 (y + 0x5d), centrados en _ftol(rx · 170) desde x + 0x14 (0x5542f0 en el modo 2), con la fuente
// y los colores que haya; y los botones 0x7d535b8 en (x + 0x37, y + 0x75) y 0x7d53640 en (x + 0x6e, y + 0x75). No saca
// rand().
[[nodiscard]] Core::Expected<void, std::string> DrawTheGuildPositions(GameHudContext& context);
} // namespace Client::Scenes
