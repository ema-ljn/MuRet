#pragma once

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"

#include <functional>
#include <string>

// La mezcla de joyas (CL9 hito 6, fase 3b; 06 § Los diálogos del juego, "La mezcla de joyas"): los diálogos que abre
// el 30 con 9 (0x4d25aa) desde 0xa1 y lo que hacen en el paso de 0x623e70, con las funciones de 0x402a10..0x402eb0 y
// el BC (Protocol::JewelMix). Ninguno saca rand(): el inventario (0x78a9bb0) no está en la tabla de protección y el BC
// y el 31 salen en claro. Los dibujos van en Scenes/GameDialogDrawing; del 0xa4, la lista de 0x6e5660 (su paso, su
// elegido y su dibujo) corta (08 P-C338).
namespace Client::Scenes
{
struct GameUiContext;
class WorldState;

// Lo que manda los paquetes de la mezcla (el C1 03 31 de 0x402eb0): GameUiContext::sendNpcWindow en el paso y
// WorldEntryResources::sendNpcWindowPacket al recibir.
using JewelMixSender = std::function<Core::Expected<void, std::string>(Protocol::Outgoing)>;

// 0x402e50: de vuelta a elegir: 0x6e5798 en 0 si estaba en 1, el diálogo cerrado (0x6236f0(0)), 0x6e5799 en 0 y la
// joya sin elegir (0x6c62e0 en 0xff), y el 0xa2 (juntar, 0x6e5794 en 0) o el 0xa4 (deshacer, en 1); con otro modo,
// todo cerrado (0x402eb0: 0x402e10, el diálogo y el C1 03 31 en claro por `send`).
[[nodiscard]] Core::Expected<void, std::string> ReturnToTheJewelChoice(WorldState& world, const JewelMixSender& send);

// 0x6259c6..0x625b22, el 0xa1: con el clic (que apaga), los dos botones de x en [245, 395] e y en [190, 225] y
// [255, 290] cierran y abren el 0xa2 (0x6e5794 en 0, juntar) o el 0xa4 (en 1, deshacer), con el MouseUpdateTime en 0
// y su máximo en 6, sin sonido. Después, x en [279, 360] e y en [320, 345] cierra todo (0x402eb0: 0x402e10, el
// diálogo y el C1 03 31 en claro).
[[nodiscard]] Core::Expected<void, std::string> StepTheJewelMenu(const GameUiContext& context);

// 0x625b23..0x625cf3, el 0xa2: con el clic (que apaga), las dos joyas (y en [185, 210], x en [245, 325] y [325, 405])
// eligen 0x6c62e0 (0 bendición y 1 alma) y, con una elegida, los tres paquetes (x en [245, 395], y en [225, 260],
// [270, 305] y [315, 350]) ponen 0x6c62e1 en 10, 20 o 30 y preguntan con 0x402b70: si alcanza, cierran y abren el
// 0xa3 con su texto (0x633300(0, 0, 0xa3)), con el MouseUpdateTime en 0 y su máximo en 6; si no, el aviso de
// 0x402b70 y la vuelta de 0x402e50. Después, x en [279, 360] e y en [365, 390] cierra todo (0x402eb0).
[[nodiscard]] Core::Expected<void, std::string> StepTheJewelChoice(const GameUiContext& context);

// 0x625cf4..0x625f96, el 0xa4: con el clic (leído antes, y que apaga solo si da), las dos joyas (y en [160, 185], x
// en [235, 315] y [315, 395]) eligen 0x6c62e0 y llenan la lista de 0x6e5660 (0x402a10 y 0x402a20); sin paquetes, el
// aviso 1818 de error y la vuelta de 0x402e50. Sin joya, x en [279,5; 359,5] e y en [200, 225] con el clic (que no
// apaga) cierra todo (0x402eb0). Con una, el paso de la lista corta (0x625dbe, 08 P-C338).
[[nodiscard]] Core::Expected<void, std::string> StepTheJewelUnmix(const GameUiContext& context);

// 0x625f98..0x62601d, el 0xa6: con el clic (que apaga), x en [287, 352] e y en [170, 195] vuelve con 0x402e50.
[[nodiscard]] Core::Expected<void, std::string> StepTheJewelResult(const GameUiContext& context);

// 0x62846f..0x628500, cada botón elegido del 0xa3 (el paso de sí y no ya dejó el clic y el botón en 0): el sí deja 1
// en 0x6c932e, cierra y abre el 0xa5 y manda el BC (0x4029e0); el no deja 2 y vuelve con 0x402e50. Los dos con el
// MouseUpdateTime en 0, su máximo en 6 y el sonido 0x19 (CL10).
[[nodiscard]] Core::Expected<void, std::string> AnswerTheJewelMix(const GameUiContext& context, bool yes);
} // namespace Client::Scenes
