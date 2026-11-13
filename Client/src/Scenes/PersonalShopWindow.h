#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>
#include <string_view>

// La ventana de la tienda personal propia (CL9 hito 7, fase 5; 04 § Las tiendas personales y 06 § Lo social en
// pantalla, "Las tiendas personales"): su paso (0x5ea290), el diálogo 0x91 del título y del precio (0x5eba70), la caja
// de confirmación 0x9c (0x4716e0, su paso 0x471930 y su respuesta 0x623750) y las respuestas de los avisos que 0x5d8d50
// mira antes del ítem del cursor (0x5d8dee..0x5deac5). Lo que mandan sale por InventoryServices::sendGamePacket, con
// los dos rand() de cada C3 antes. El GameServer 0.97 no tiene el 3F: el oráculo es el ASM del cliente (*estático*).
namespace Client::UI
{
class PanelRegistry;
struct DialogState;
} // namespace Client::UI

namespace Client::Scenes
{
struct ConfirmBoxState;
struct GameUiContext;
struct InventoryMoveContext;
struct WorldRuntime;

// 0x5eba70: los dos modos del diálogo 0x91, que quedan en 0x78aba60.
inline constexpr std::int32_t ShopTitleMode = 1;
inline constexpr std::int32_t ShopPriceMode = 2;
// 0x5ea9c8..0x5eaa1c: la caja 0x9c de abrir la tienda, que responde a 0x623750 con el 1000.
inline constexpr std::int32_t OpenShopConfirm = 1000;

// 0x5eba70(modo): con 1 o 2, el diálogo 0x91 (0x6236f0), 0x78aba60 en el modo, el campo del chat cerrado (0x6c8e68), el
// modo del zen (0x77f3b6a) en 0 con el título y en 1 con el precio, los campos de texto vacíos (0x553570(0)) y el largo
// del campo (0x6c8e78) en 0x1c o en 8. Con 0x6c7b8c en 1 (la .data) arma además el EDIT de [0x5763504] (su largo, su
// tamaño de 0x85 o 0x3c por 10 y su lugar en (0xfd o 0x125, 0x8a)), que es del hito 6 y MuRet no tiene. Con otro modo,
// nada. Sin rand().
void OpenTheShopDialog(WorldRuntime& runtime, std::int32_t mode);

// 0x4716e0(id, texto, tipo) sobre 0x575d9d0: con el 0x9c abierto (0x7d3a10c), false. Si no, el 0x9c por 0x6236f0 (con
// otro abierto, esperando) y, con el tipo fuera de 1..2, false con el diálogo ya pedido. Si no, el id, el tipo y los
// renglones de nuevo: el texto en pedazos que se cortan al pasar de 24 bytes (los de 0x80 o más cuentan 2, sin mirar
// el que sigue) o en el final justo; el pedazo que termina con un byte de 0x80 o más pasado del final se pierde
// (Q-C966). true.
[[nodiscard]] bool OpenTheConfirmBox(ConfirmBoxState& box, UI::DialogState& dialogs, std::int32_t id,
                                     std::string_view text, std::int32_t type);

// 0x471930, el paso del 0x9c (0x6260d1): con el 0x9c abierto y el clic, los botones en y de h + 140 a h + 156 con los
// bordes (h: los renglones por el paso de la letra (0x7758568) + 2, comparados en la x87). Con dos botones, el de x de
// 0xff a 0x133 responde 1 y el de 0x14a a 0x17e responde 2; con uno, el de 0x127 a 0x15b responde 1. Cada uno gasta
// el clic y llama a 0x623750 con el id de la caja.
[[nodiscard]] Core::Expected<void, std::string> StepTheConfirmBox(const GameUiContext& context);

// 0x623750(id, respuesta): con el 1000 y el 1, el C1 28 3F 02 con el título (0x78aba64) en C3, todos los paneles
// cerrados (0x657a90) y el diálogo que espera abierto (0x7d3a110 a 0x7d3a10c); con el 1000 y el 2, solo esto último.
// Con otro id o respuesta, nada. No es el diálogo del título (ese es el 0x91 de 0x5eba70).
[[nodiscard]] Core::Expected<void, std::string> AnswerTheConfirmBox(const GameUiContext& context, std::int32_t id,
                                                                    std::int32_t answer);

// 0x5ea290 (desde 0x5ee1b1): con la tienda propia o la ajena abiertas (0x800 y 0x20000, sin rand()) y 0x6c931c en 1:
// - el título respondido (0x78aba60 en 1 con 0x6c932e en 1 o 2) deja los dos en 0 y 0xff;
// - la esquina de la ventana en (0x104, 0) (0x78aba58 y 0x78aba5c);
// - el botón de abrir (x de 0x122 a 0x15a e y de 0x18c a 0x1a4, sin los bordes de la derecha y de abajo, con el clic,
//   que gasta): cada ítem de la grilla 0x78ab0b8 (tipo con signo de 0 o más) necesita un precio de más de 0 en el mapa
//   del modo (0x5f8110 y 0x5f80c0, en su lugar columna + fila · 8 + 0x4c), y tiene que haber alguno y un título; si no,
//   el aviso 1119 con el tipo 2. Con la tienda abierta (0x78aba50), el C1 28 3F 02 con el título en C3 y el inventario
//   cerrado (0x65cae0(0x4000000)); cerrada, la caja 0x9c con el 1000, el texto 1131 y dos botones;
// - el de cerrar (x de x + 0x69 a x + 0xa1, la misma y, con el clic, que gasta): con la tienda abierta, el C1 04 3F 03
//   en C3;
// - el del título (x de x + 0x14 a x + 0xaa e y de y + 0x41 a y + 0x59, con el clic, que no gasta): 0x5eba70(1).
[[nodiscard]] Core::Expected<void, std::string> StepThePersonalShopWindow(const GameUiContext& context,
                                                                          UI::PanelRegistry& panels);

// 0x5d8dee..0x5dc798 y 0x5deaa7..0x5deac0: lo que 0x5d8d50 hace antes del ítem del cursor, ya sin la traba de C3 24.
// Con 0x78abb14 de 1 a 6 (en ese orden) o, si no, con 0x78aba60 en 2, la respuesta de 0x6c932e: con 1 o 2 la atiende
// y la borra; con otra el paso termina sin hacer nada más (el aviso sigue esperando). Devuelve si el paso sigue con el
// ítem del cursor (0x5deac5):
// - 1, 2 y 4 son de las ventanas de NPC (CL9.3): el 1 con el no vuelve el ítem del cursor (0x5be150), el 2 con el no
//   solo se borra, y lo demás corta;
// - 3, el aviso del OK del comercio (0x5ecf67): con el sí, el OK propio da vuelta, 0x78abaa5 en 1 y el C1 04 3C con el
//   OK nuevo en C3; con el no, se borra;
// - 5, la compra en la tienda de otro (0x5c5181): con el sí y la clave del dueño (0x78a6514), el C1 11 3F 06 con la
//   clave, el nombre y el lugar [0x78a6758] en C3; con el no, se borra;
// - 6, el precio menor que el valor del ítem (0x5dc7e9): con el sí, el precio (abajo); con el no, 0x5eba70(2);
// - 0x78aba60 en 2, el diálogo del precio: con el sí, 0x78aba60 en 0 y el precio escrito (0x77f3b6c, sin signo) contra
//   el valor del ítem de 0x78a59c8 (0x54d0f0 con el modo 2): menor, el aviso 0x633300(2, 1132, 0x99) y 0x78abb14 en
//   6; si no, el precio. Con el no, 0x78aba60 en 0 y 0x5be150.
// El precio (0x5da5ac.. y 0x5dc807..): con la tienda abierta, el C1 04 3F 03 en C3; con un ítem en el cursor, el C1 09
// 3F 01 con el lugar [0x78a6758] y el precio en C3, el 24 de la grilla 0 a la 4 (con la traba suelta) y el precio en el
// mapa del modo en 0x77f3cc8; sin ítem, el 3F 01 y el precio en el mapa en [0x78a6758]. Al final 0x77f3b6c en 0.
[[nodiscard]] Core::Expected<bool, std::string> StepTheWarningAnswers(const GameUiContext& context,
                                                                      const InventoryMoveContext& moves);
} // namespace Client::Scenes
