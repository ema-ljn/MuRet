#pragma once

#include "Core/Expected.h"

#include <string>

// Los dibujos del duelo (CL9 hito 7, fase 2; 04 § El duelo): el marcador, el 14.º hijo del HUD (0x5a6cc0), y la caja
// del pedido, desde la pasada 2D de la escena (0x5f37f0, en 0x637683). En 640 × 480, con el estado de GL que dejan y
// los globales de los textos (0x6c8e5c, 0x6c8e64 y la fuente del DC) en HudState.
namespace Client::UI
{
class PanelRegistry;
}

namespace Client::Scenes
{
struct GameHudContext;

// 0x5a6cc0: con el duelo en curso (0x78abad8), la mitad del ancho del mundo (0x5bb8d0, con sus consultas) como centro;
// la franja negra al 0,5 de 300 × 40 desde (centro − 150, 20) con el marco de 1 en (0,65; 0,53; 0,34); en la negrita
// grande, el nombre del héroe (sombra negra en centro − 79, 0x1f y en 0xffff9600 en centro − 80, 0x1e), "vs" en
// 0xff0096ff en el centro y el nombre del rival (sombra en centro + 81 y en 0xff1919ff en centro + 80); en la normal,
// los puntos propios (0x78abaf8, en 0xffff9600) y los del rival (0x78abafc, en 0xff1919ff) en centro ∓ 80, 0x30. Cada
// x de un texto vuelve a llamar a 0x5bb8d0: ocho en total, cada una con sus rand(). Deja la normal, el fondo en 0, el
// texto en 0xff1919ff y la mezcla apagada.
void DrawTheDuelScore(GameHudContext& context, UI::PanelRegistry& panels);

// 0x5f37f0: con el pedido de duelo (0x78abad9), la caja de Message_box1 y 2 (0xfb y 0xfc) en (213, 110) de 213 de
// ancho, la imagen del duelo (DuelWindow.jpg, 0x650) sumada en (256, 120), los botones sí (0xf1, 0xf2 con el mouse en
// 0xed..0x133 × 0xfa..0x10f) y no (0xf3, 0xf4 en 0x14c..0x192), "[nombre]" en la negrita grande (sombra negra en
// (0x140, 0x8d) y en 0xff18cfff en (0x13f, 0x8c)) y los textos 910 y 911 en la negrita (sombra en 0x140 y en
// 0xffdcdcdc en 0x13f). Sin rand(). Deja la negrita, el fondo en 0, el texto en 0xffdcdcdc y la mezcla apagada. Sin
// Text.bmd, corta.
[[nodiscard]] Core::Expected<void, std::string> DrawTheDuelRequest(GameHudContext& context);
} // namespace Client::Scenes
