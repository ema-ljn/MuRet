#pragma once

#include "Core/Expected.h"

#include <string>

// La ventana de las misiones (CL9 hito 2; 06 § Las misiones, "La ventana"): el paso (0x41b5a0, con 0x41a9e0 y 0x419bd0
// para el diálogo con el NPC y 0x41b170 para la lista) y el dibujo (0x41ca40, con 0x41ba20 y 0x41c600), del objeto
// [0x710148] (WorldRuntime::quest) con su ventana en +0x21383 (UI::DelegatedPanels::quest: 1 el diálogo, 2 la lista).
// Mandan el C3 A2 (aceptar o terminar la misión, con los dos rand() del C3) y el C1 03 31 (0x419900, cerrarla).
namespace Client::UI
{
class PanelRegistry;
}

namespace Client::Scenes
{
struct GameHudContext;
struct GameUiContext;

// 0x41b5a0, desde 0x5ee140 (0x5ee20d), después de los comandos: la esquina de la ventana en +0x21390 y +0x21394,
// (0x104, 0) con el personaje abierto y (0x1c2, 0) si no. Con el personaje abierto, el clic en el botón de las misiones
// ([0x1f9, 0x211) × [0x18b, 0x1a3)) lo gasta, pone 0x77f3b10 en 0 y 0x6c8dcc en 6 y cierra (Close(8)) o abre (Open(8),
// la lista) las misiones. Después, con la ventana en 1, 0x41a9e0; con 2, 0x41b170. El mouse de la UI se compara con
// la esquina que acaba de poner.
//
// 0x41a9e0: el mouse sobre la ventana (0xbe × 0x1b1) es de la UI; las respuestas (0x419bd0); con 0x419b10 (la misión
// en curso con sus ítems), el clic en el botón de terminar ([x + 0x23, + 0x78) × [0x163, 0x17b)) manda el C3 A2 con la
// misión y 1, gasta el clic, suelta el botón (0x7d36874) y suena (0x1c); el clic en el de cerrar ([x + 0x19, + 0x18) ×
// [y + 0x18b, + 0x18)) lo gasta y cierra con 0x419900, sin el sonido del registro; y apaga el soltado (0x7d366ec).
//
// 0x419bd0: el clic sobre las respuestas, que empiezan en ((7 − respuestas − renglones) · 0x12) / 2 + 0x42 + renglones
// · 0x12 (en 0xfa con el estado distinto de 1), de 0x12 cada una, a no más de 0x6a de x = 0x236 (Q-C871), lo gasta,
// suelta el botón y pone 0x77f3b10 en 0 y 0x6c8dcc en 6; la respuesta es (y − arriba) / 0x12 con la división de C, y
// la fila de 1 a 17 píxeles por encima de la primera es la 0 (Q-C870). Su acción en Dialog.bmd: 1 mira las condiciones
// con el zen (0x418fb0) y, si no se cumplen, muestra el diálogo que dejaron (0x419710); si se cumplen, el A2; 2 cierra
// con 0x419900; 3 manda el A2. Con cualquiera suena (0x1c), y si la respuesta lleva a un diálogo (mayor que 0) y las
// condiciones no fallaron, 0x419710 lo muestra.
//
// 0x41b170: el mouse sobre la ventana; el clic en la pestaña 0 ([x + 0x14, + 0x31) × [0xeb, 0xfb), o [0xed, 0xfb) si no
// es la elegida) la elige (el C1 9F que le sigue no se alcanza, Q-C845); en la 1 y la 2, nada, y el clic sigue. El clic
// en el botón de cerrar cierra con Close(8), que suena.
[[nodiscard]] Core::Expected<void, std::string> StepTheQuestWindow(const GameUiContext& context,
                                                                   UI::PanelRegistry& panels);

// 0x41ca40, desde 0x5f7440 (0x5f75db), después de la ventana de 0x78abb00: con la ventana en 1, el diálogo con el NPC
// (0x41ba20); con 2, la lista (0x41c600). Después, con el personaje abierto, el botón de las misiones (0x103 con la
// ventana abierta, 0x102 cerrada) de 24 × 24 en (505, y + 0x18b) y, con el mouse encima, su ayuda: el texto 1140 y
// "(T)".
//
// 0x41ba20: el fondo con la línea (0x5ee220(x, y, 1)); con el estado 1, la línea de y + 325, los ítems pedidos con su
// nombre y cuántos (0x41b810(1, 1)), el botón de terminar (gris si falta alguno, más oscuro con el clic encima, que con
// el soltado se gasta) con el texto 699; con el 3, el zen que pide (el texto 198 y 0x5b1130 con el color de 0x5b10f0);
// el botón de cerrar con su ayuda (1002); el título, el nombre del NPC de la misión en la tabla de 0x7681eb8 (sin él,
// nada); el nombre de la misión; y el texto y las respuestas del diálogo (0x41b680(1)). Con el héroe de clase 3 o 4 y
// 0x6eeda8, el NPC de la misión 2 y sin el nombre de la misión.
//
// 0x41c600: el fondo con la línea, el marco de abajo, las tres pestañas (1140 a 1142; la 1 y la 2 en rojo), el botón
// de cerrar con su ayuda (el texto 927 con "T") y la pestaña elegida: la 0 (0x41c2a0) con "Quest", el nombre de la más
// alta, el texto del diálogo sin respuestas y el estado (930 a 932); la 1 y la 2 (0x41c420 y 0x41c4a0) no se alcanzan
// (solo la 0 se elige) y van como el original.
[[nodiscard]] Core::Expected<void, std::string> DrawTheQuestWindow(GameHudContext& context, UI::PanelRegistry& panels);

// 0x41b9e0, lo primero del 3D de la UI (0x5ac120, 0x5ac1c7): con la ventana en 1, los ítems pedidos de la misión en
// curso con el estado 1 (0x41b810(1, 0)), cada uno con 0x5e1480 en (480, 235 + 25 · i) de 20 × 20 con el nivel << 3;
// con la lista y +0x21380 (que nadie prende), los del estado 1 o 2 según +0x21381.
[[nodiscard]] Core::Expected<void, std::string> DrawTheQuestItems(GameHudContext& context);
} // namespace Client::Scenes
