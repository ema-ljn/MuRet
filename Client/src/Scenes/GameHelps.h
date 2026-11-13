#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// Las ayudas de las superposiciones (CL8 hito 6; 06 § HUD y ventanas, "Las ayudas de F1" y "La ayuda del ítem"): los
// modos 1, 2 y 3 de 0x5b0550, que GameHud::DrawTheOverlays elige con 0x77f3b08.
namespace Client::Scenes
{
struct GameHudContext;

// 0x5b0559..0x5b07ef: 0x620980 y los renglones de los tooltips (HudState::tooltip) desde el 0, con 0x78abb30 de
// cuenta: "\n", el título (el texto 120 o el 140, con el color 1 y la negrita), "\n", los textos 121 a 139 o 141 a 156
// (el color 0, sin negrita) y "\n"; los '\n' no tocan su color ni su negrita. 0x77f3b66 en 1 y 0x5af090(1, 1,
// cuántos, 0, 2, 1) (DrawTheTooltipBox): arriba a la izquierda, con marco. `mode` es 1 o 2. Sin Text.bmd, o con un
// texto de 100 bytes o más (Q-C613), corta.
[[nodiscard]] Core::Expected<void, std::string> DrawTheFunctionHelp(GameHudContext& context, std::int32_t mode);

// 0x5b0802..0x5b10c1 (GameItemHelp.cpp): la ayuda del ítem de 0x77f3b0c. 0x620980; con el ítem 0x807 o 0x80f (las
// flechas y los virotes), fuera de las cinco franjas de tipos (0..0x9ff, 0xa00..0xbff, 0xc00..0xdff, 0xe00..0x17ff y
// 0x1e00..0x1fff) o negativo, 0x77f3b08 en 0 (salvo en un dibujo intermedio de MuRet) y nada más. Con las franjas 1 a
// 4 y un ancho de pantalla que no es 640, 800, 1.024 ni 1.280 el original divide por 0 y corta (0x5b097b, Q-C615).
// Si no: la tabla por nivel (0x5afc00, FillTheItemHelpLevels); la caja de arriba con marco, como las de F1 ("\n", el
// texto 160 en azul y negrita, el nombre del ítem en negrita, "\n", " ", "\n", 46 o 51 espacios, 13 renglones " "
// salvo en los pergaminos, una línea verde por pieza de conjunto, las clases de 0x5af590 y "\n"), con el ancho de las
// columnas que tiene el nivel 0; 0x620a00(1), y las columnas (el título de 0x5afb00 y las filas de 0x5af9b0) en cajas
// sin marco contra el borde, desde la x de la franja y la y de la pantalla; 0x620980.
[[nodiscard]] Core::Expected<void, std::string> DrawTheItemHelp(GameHudContext& context);
} // namespace Client::Scenes
