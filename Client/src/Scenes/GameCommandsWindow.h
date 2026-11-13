#pragma once

#include "Core/Expected.h"

#include <string>

// La ventana de los comandos (D, CL8 hito 6, 08 P-C92): el objeto [0x6e592c], con su paso (0x40a450) y su dibujo
// (0x40a650). Elegir un comando, el aviso que da y cerrarla se portan; lo que hace el comando elegido sobre el
// personaje elegido (0x406050 desde 0x406097, y el nombre de 0x40aa8d) necesita un personaje elegido (0x6c8e30, que
// deja el picking desde el hito 1 de la CL9): corta con su dirección (hito 7, CL9.4).
namespace Client::Scenes
{
struct GameHudContext;
struct GameUiContext;

// 0x40a450, desde 0x5ee140 con ECX = [0x6e592c]: con +4 en 1, el mouse sobre la ventana (de la esquina que dejó el
// dibujo, 0xbe × 0x1b1) es de la UI (0x775857c); 0x406050 pone los doce estados en 1 y, con un comando elegido y un
// personaje elegido, corta; el clic sobre los botones del 1 al 11 (108 × 20, de 28 en 28, sin los bordes) lo gasta,
// suelta el botón (0x7d36874) y elige el comando (+0x20); el clic en el botón de cerrar la cierra sin Close: los dos
// sonidos, 0x77f3b10 en 0, 0x6c8dcc en 6 y ResetTheCommands. No saca rand().
[[nodiscard]] Core::Expected<void, std::string> StepTheCommandsWindow(const GameUiContext& context);

// 0x40a650, desde 0x5f7440 (0x5f75fa): con +4 en 1, la esquina en (0x1c2, 0), el fondo (0x5ee220), el título (texto
// 938), los once botones (Message_box, el elegido en gris), el botón de cerrar con su ayuda (el texto 927 con "D"), los
// nombres de los comandos centrados con su casilla (0x463090), la fuente grande, el aviso del comando elegido según
// su estado y la mezcla sin alfa al final.
[[nodiscard]] Core::Expected<void, std::string> DrawTheCommandsWindow(GameHudContext& context);
} // namespace Client::Scenes
