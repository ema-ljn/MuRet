#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// Las ayudas de las superposiciones (CL8 hito 6; 06 § HUD y ventanas, "Las ayudas de F1"): los modos 1, 2 y 3 de
// 0x5b0550, que GameHud::DrawTheOverlays elige con 0x77f3b08.
namespace Client::Scenes
{
struct GameHudContext;

// 0x5b0559..0x5b07ef: 0x620980 y la lista de la caja de ayuda en limpio: "\n", el título (el texto 120 o el 140, con
// el color 1 y la negrita), "\n", los textos 121 a 139 o 141 a 156 (el color 0, sin negrita) y "\n"; los '\n' no
// tocan su color ni su negrita. 0x77f3b66 en 1 y 0x5af090(1, 1, cuántos, 0, 2, 1): arriba a la izquierda, con marco.
// `mode` es 1 o 2. Sin Text.bmd, o con un texto de 100 bytes o más (Q-C613), corta.
[[nodiscard]] Core::Expected<void, std::string> DrawTheFunctionHelp(GameHudContext& context, std::int32_t mode);

// 0x5b0802..0x5b09a8: la ayuda del ítem de 0x77f3b0c. 0x620980; con el ítem 0x807 o 0x80f (las flechas y los
// virotes), fuera de las cinco franjas de tipos (0..0x9ff, 0xa00..0xbff, 0xc00..0xdff, 0xe00..0x17ff y
// 0x1e00..0x1fff) o negativo, 0x77f3b08 en 0 (salvo en un dibujo intermedio de MuRet) y nada más. Si no, corta: con
// las franjas 1 a 4 y un ancho de pantalla que no es 640, 800, 1.024 ni 1.280 el original divide por 0 (0x5b097b,
// Q-C615); con los demás, el dibujo desde 0x5b09a8 (0x5afc00, 0x5af590, 0x5af9b0 y 0x5afb00) es del hito 5:
// P-C105.
[[nodiscard]] Core::Expected<void, std::string> DrawTheItemHelp(GameHudContext& context);
} // namespace Client::Scenes
