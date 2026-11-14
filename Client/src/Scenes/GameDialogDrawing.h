#pragma once

#include "Core/Expected.h"

#include <string>

// Los diálogos del juego en el dibujo (06 § HUD y ventanas, "Los diálogos del juego", CL8 hito 6): 0x62db60, que la
// pasada 2D llama en la escena 5 (0x637696) entre el pedido de duelo y los tooltips. Dibuja el diálogo abierto
// (0x7d3a10c) con su marco y sus textos en 640 × 480, con el estado de GL que dejan sus primitivas.
namespace Client::Scenes
{
struct GameHudContext;

// 0x62db60: siempre deja la mezcla sin mezcla (0x620980), el color en blanco, los colores del texto en (−1, 0) y la
// fuente normal en el DC, y termina con la mezcla normal (0x620a00(1), 0x63140b). Con el diálogo abierto:
//   - 0x6e, el menú de Esc: los 5 botones (0xf0 en (260, 60 + 30i, 120, 22)), el del mouse otra vez en aditiva con
//     (0,8; 0,6; 0,4), y los textos 381, 382, 383, 385 y 384 centrados;
//   - 0x93, las opciones: las 7 filas, los textos 385, "%s  On"/"%s  Off" con 386, 387 y 919, el 389 con la barra del
//     volumen (su esquina queda en 0x7d3a120 y 0x7d3a124 para el paso), el marco y las 5 casillas del brillo con sus
//     cifras, y los textos 1840 y 388;
//   - las cajas (0x8a, 0x8c, 0x8b, 0x97 y 0x9b): el marco (0xfc, 4 o 7 filas de 0xfb y 0xfc), los botones con estado
//     (0xf0 + estado), las flechas del 0x8c (que gastan el clic, Q-C553) y los renglones del 0x8a y del 0x8c
//     centrados; los del 0x8b, el 0x97 y el 0x9b los dibuja el objeto del evento ([0x723c60] + 0x10, CL9.5): sin él,
//     nada, y con él corta;
//   - los del OK genérico con texto (0x70, 0x6f, 0x1a, 0x1c, 0x87, 0x88 y 0x8f): el marco 0xf0, el OK (0xf1, o 0xf2
//     con el mouse) y sus textos de Text.bmd;
//   - los de sí y no (0x94, 0x96, 0x98, 0x99, 0x9a, 0xa3, 0xa8, 0xa9 y 0xaa, CL9 hito 6): el marco en y = 100 (210 el
//     0xa3; 0xfc, 3 filas de 0xfb, 4 el 0x9a, y 0xfc), los botones con estado de 1 a 4 (el sí del 0x9a en rojo sin el
//     ítem; los tres del 0xa8 en 0x646 con sus textos) y los renglones en la fuente gruesa, con la x, la y y los
//     colores que dice 0x78abb14 (0x632637), con el título de la mezcla antes si está en 2; o en amarillo los del 0xa9
//     (0x632a45); o centrados como los de las cajas los del 0xaa (0x632472);
//   - el modo de la caja del caos (0x8e) y la mezcla de joyas (0xa1, 0xa2, 0xa4, 0xa5 y 0xa6, CL9 hito 6): el marco
//     desde y = 120, los botones 0xf0 en blanco con el mouse, la joya elegida en (1; 1; 0,2), los textos de Text.bmd
//     y el título de la cola (734, 1801 o 1800) en negrita con 0xff0080ff; con una joya elegida el 0xa4 dibuja la
//     lista de 0x6e5660 (0x62f739, Scenes/JewelBundleList) y su OK y su cancelar;
//   - el 0x91: el marco desde y = 100, y corta en el EDIT del diálogo (0x6305c9, CL9.4).
// Los demás cortan con la ola que los trae, como su paso. En un dibujo intermedio de MuRet, la esquina de la barra y
// el clic no se escriben. Sin Text.bmd, o sin uno de sus textos, corta.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGameDialogs(GameHudContext& context);
} // namespace Client::Scenes
