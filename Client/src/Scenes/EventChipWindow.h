#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// CL9 hito 11, fase 4: la ventana de 0x78abb00 (06 § La ventana de los chips), que abre el 94 (0x4dd8a0) con su tipo
// + 1 en la esquina del inventario (0x1c2, 0): con 1 la Rena y con 2 la piedra (el ítem 0x1c15 de nivel 0 o 1, que
// el botón registra con el C1 05 95), con 3 la entrada del número de serie (un EDIT y el C1 12 9D) y con 4 el pedido
// del número del Muto (el C1 04 98 01); el botón de cerrar manda el C1 03 97. Todo *estático*: el GS 0.97 atiende el
// 95, el 97 y el 98, pero no el 9D, que sale del ASM. MuRet tiene 0x6c7b8c (.data) en 1: lo que el original hace con
// él en 0 (la entrada sin EDIT, 0x553ae0) no se alcanza.
namespace Client::UI
{
class PanelRegistry;
}

namespace Client::Scenes
{
struct GameHudContext;
struct GameUiContext;
struct WorldRuntime;

// 0x78abb00: lo que abre el 94 con el tipo + 1.
inline constexpr std::int32_t RenaChipWindow = 1;
inline constexpr std::int32_t StoneChipWindow = 2;
inline constexpr std::int32_t SerialNumberWindow = 3;
inline constexpr std::int32_t MutoNumberWindow = 4;

// 0x4dd8ce..0x4dd920, el 94 con el tipo 2: los 0x40 bytes del 9D (0x78ab078) en cero, 0x553570(0), el campo del chat
// cerrado (0x6c8e68), Tab con un solo campo (0x6c8e6c en 1) y 0x77f3b6b en 1. 0x77f3b6a, 0x77f3b6c, 0x78abacc (en 0) y
// 0x6c8e78 (en 0xc, el tope de la entrada sin EDIT) no los tiene MuRet: nadie los lee con 0x6c7b8c en 1.
void OpenTheSerialEntry(WorldRuntime& runtime);

// 0x56b6a9..0x56b72a, SendMove con 0x78abb00 en 3 después del 97: el campo elegido en 0, los buffers de 0x7798bf8 en
// cero con sus largos salvo el del susurro (el segundo, 0x7798cf8), el EDIT de [0x5763504] vacío (la vtable + 0x4c con
// 0) y el chat cerrado, con 0x77f3b6b en 0. No toca el foco: el EDIT lo sigue teniendo. 0x6c8e74 (−1), las marcas de
// 0x77f31c0, el EDIT de [0x5763508] y los otros ocho buffers no los tiene MuRet.
void CloseTheSerialEntry(WorldRuntime& runtime);

// 0x5e6520, desde 0x5ee1ec después del ítem del cursor, con 0x78abb00 distinto de 0. El mouse sobre la ventana es de la
// UI (0x775857c). Con el soltado (0x7d366ec) sobre el botón: con 4, el C1 04 98 01; con 1, 2 u otro que no sea 3, la
// casilla del 0x1c15 de nivel 1 (con 2) o 0 y, si hay, el C1 05 95; con 3, sin casillas libres (0x558740(−1, −1)) el
// aviso 896 y si no el C1 12 9D con el texto del EDIT, y el izquierdo suelto (0x7d36874). Después, el clic (0x7d366d4)
// en el botón de cerrar (0x19, 0x18b de la esquina del inventario, 24 × 24): el 97, 0x78abb00 en 0, el inventario
// cerrado (0x65cae0, con sus rand()), 0x553570(0), el chat cerrado, 0x77f3b6b en 0 y el foco a la ventana del juego. Al
// final apaga el soltado. Los paquetes salen con el send() del paso (context.chat).
[[nodiscard]] Core::Expected<void, std::string> StepTheEventChipWindow(const GameUiContext& context,
                                                                       UI::PanelRegistry& panels);

// 0x5f6550, desde 0x5f75d0 después de los NPC de evento: la esquina del inventario en (0x1c2, 0), el fondo, el título
// (el nombre del NPC 236 de NpcName(%s).txt, 0x54ebb0), el botón de su tipo (el 4 en y 220 con 903; los otros, salvo
// el 3, en 231 con 243 u 818, en blanco con la fuente negrita si hay ítems y en gris si no), y con 3 la entrada: los
// textos 893, 894, 916 y 917, el marco 0x10f y el EDIT de [0x5763504] reconfigurado en cada frame (+0x4c en 2, su
// fondo en 0, el tope en 12, 150 × 14 en (495, 265), el foco y su dibujo 0x468320) y el botón 895. Con el botón
// apretado (0x7d366d4) y el soltado (0x7d366ec), el dibujo los apaga con el izquierdo (0x7d36874). Después el botón de
// cerrar con su ayuda (1002), los textos de cada tipo (700..703; 882..885; 891, 892, 897 y 898 con el resultado del 9D
// en negrita, que titila con el reloj; 900, 706, 901 y 902), y salvo con 3, los rótulos (245 o 819 y 246 u 820) con
// "x%d" de la cuenta de ítems y de 0x78abb04 en la fuente grande. No saca rand(). Un dibujo intermedio no escribe el
// foco, el EDIT ni el clic.
[[nodiscard]] Core::Expected<void, std::string> DrawTheEventChipWindow(GameHudContext& context);

// 0x5f70a0, desde el 3D de la UI (0x5ac1da) con 0x78abb00 en otro valor que 0 y 3: el modelo del 0x1c15 (de nivel 0, o
// 0xf en +4 con 2 y 4; con 5, el tipo 0) del tamaño de Item.bmd, en (540, 298) salvo con 4 y en (540, 333), con los
// rand() de 0x5e1480. Después 0x5ac120 no dibuja las grillas, el ítem del cursor ni las ranuras rápidas.
[[nodiscard]] Core::Expected<void, std::string> DrawTheEventChipItems(GameHudContext& context);
} // namespace Client::Scenes
