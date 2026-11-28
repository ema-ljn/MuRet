#pragma once

#include "Core/Expected.h"

#include <string>

// La ventana /move (CL8 hito 6; 06 § HUD y ventanas, "La ventana /move"): el modo 4 de 0x77f3b08 (UI::MovePanel), con
// los destinos de movereq.bmd (WorldRuntime::moves, la lista de 0x723c68). *Estático*.
namespace Client::Scenes
{
struct GameHudContext;
struct GameUiContext;

// 0x5ecdc0 → 0x449850 → 0x448580, en el paso de la UI (0x636e24) con la ventana abierta y sin nadie que agarró el
// mouse. El mouse sobre la ventana (0 < x ≤ ancho, 0 < y ≤ alto) es de la UI (0x775857c). Cada destino, en su orden,
// abre y cierra el registro propio (3.848 rand() al cerrarlo, Q-C183) y pasa con el nivel (2/3 del pedido para las
// clases 3 y 4, con ftol), el zen y el PK debajo de 6; el primero que pasa con el botón izquierdo abajo (0x7d36874)
// sobre su renglón dice el texto 260, un espacio y su nombre como el chat de Enter (Scenes::SayInTheChat, en línea en
// 0x4488d3..0x448ef8), cierra la ventana y apaga el botón y el flanco (0x7d366d4). Después, el renglón de abajo, con
// el botón, la cierra igual. Sin la entrada del chat (`context.chat`), el clic sobre un destino corta.
[[nodiscard]] Core::Expected<void, std::string> StepTheMoveWindow(const GameUiContext& context);

// 0x5b10e0 → 0x449860 → 0x448fa0, desde las superposiciones (0x5b0550): el fondo negro al 80 %, el renglón rojo de
// abajo, el título (933, en negrita) y las columnas (934 a 936), y por cada destino, con el registro propio abierto y
// cerrado como en el paso, el resaltado bajo el mouse si pasa (sin nadie que agarró el mouse), su nombre, el nivel y
// el zen en sus colores; al final, el 1002 en el renglón de abajo. Sin Text.bmd, corta.
[[nodiscard]] Core::Expected<void, std::string> DrawTheMoveWindow(GameHudContext& context);
} // namespace Client::Scenes
