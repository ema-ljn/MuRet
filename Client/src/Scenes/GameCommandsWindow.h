#pragma once

#include "Core/Expected.h"
#include "UI/ChatCommands.h"

#include <string>

// La ventana de los comandos (D, CL8 hito 6, 08 P-C92): el objeto [0x6e592c], con su paso (0x40a450) y su dibujo
// (0x40a650). Elegir un comando, el aviso que da, cerrarla y el cartel del personaje elegido (0x6c8e30, que deja el
// picking desde el hito 1 de la CL9; 0x40aa80..0x40abd8) se portan, y lo que hace el comando elegido sobre él con el
// clic derecho (0x406050 desde 0x406097, CL9 hito 7, fase 3): el comercio, la tienda personal, la party, el susurro,
// seguir y el duelo; los cuatro del gremio cortan hacia el hito 8 (08 P-C209).
namespace Client::Scenes
{
struct GameChatContext;
struct GameHudContext;
struct GameUiContext;

// 0x40a450, desde 0x5ee140 con ECX = [0x6e592c]: con +4 en 1, el mouse sobre la ventana (de la esquina que dejó el
// dibujo, 0xbe × 0x1b1) es de la UI (0x775857c); 0x406050 pone los doce estados en 1 y, con un comando elegido y un
// personaje elegido, lee las cuatro celdas, elige el destino y los estados del comando y, con el clic derecho, lo corre
// (con Text.bmd y el cable de `chat`; sin ellos, el comando corta); el clic sobre los botones del 1 al 11 (108 × 20, de
// 28 en 28, sin los bordes) lo gasta, suelta el botón (0x7d36874) y elige el comando (+0x20); el clic en el botón de
// cerrar la cierra sin Close: los dos sonidos, 0x77f3b10 en 0, 0x6c8dcc en 6 y ResetTheCommands. No saca rand().
[[nodiscard]] Core::Expected<void, std::string> StepTheCommandsWindow(const GameUiContext& context);

// 0x40a650, desde 0x5f7440 (0x5f75fa): con +4 en 1, la esquina en (0x1c2, 0), el fondo (0x5ee220), el título (texto
// 938), los once botones (Message_box, el elegido en gris), el botón de cerrar con su ayuda (el texto 927 con "D"), los
// nombres de los comandos centrados con su casilla (0x463090), la fuente grande, el cartel del jugador elegido con su
// nombre (0x40aa80..0x40abd8), el aviso del comando elegido según su estado y la mezcla sin alfa al final.
[[nodiscard]] Core::Expected<void, std::string> DrawTheCommandsWindow(GameHudContext& context);

// 0x573f85..0x574bd5 (/guild fuera de Chaos Castle, CL9 hito 8): con gremio, el aviso 255; si no, el elegido o el de
// enfrente (Scenes::TheOneToAsk) y, con el mundo cargado, 0x78aba40 con su clave, el C1 05 50 por el cable de `chat` y
// el aviso 477 con su nombre. Sin Text.bmd o sin el cable cuando hace falta, corta.
[[nodiscard]] Core::Expected<UI::ChatCommandOutcome, std::string> RequestAGuildJoin(const GameChatContext& chat);

// 0x570d7e..0x573f48 (/union, /rival y /rivaloff fuera de Chaos Castle, CL9 hito 8): sin gremio, el aviso 1355; si no,
// el elegido o el de enfrente y el E5 de `command` (01 01, 02 01 o, después de la animación 0xb7 y el C1 05 18 del
// comando 8, 02 02) con su aviso (1358, 1359 o 1360). Sin Text.bmd, sin el cable o sin los modelos (/rivaloff), corta.
[[nodiscard]] Core::Expected<UI::ChatCommandOutcome, std::string>
RequestAGuildRelation(const GameChatContext& chat, UI::GuildRelationCommand command);
} // namespace Client::Scenes
