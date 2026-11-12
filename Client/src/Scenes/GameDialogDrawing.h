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
//   - los del OK genérico con texto (0x70, 0x6f, 0x1a, 0x1c y 0x8f): el marco 0xf0, el OK (0xf1, o 0xf2 con el mouse)
//     y sus textos de Text.bmd.
// Los demás cortan con la ola que los trae, como su paso. En un dibujo intermedio de MuRet, la esquina de la barra y
// el clic no se escriben. Sin Text.bmd, o sin uno de sus textos, corta.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGameDialogs(GameHudContext& context);
} // namespace Client::Scenes
