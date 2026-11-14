#pragma once

#include "Core/Expected.h"
#include "Scenes/NpcWindowState.h"

#include <cstdint>
#include <optional>
#include <string>

// La lista de los paquetes que se pueden deshacer, 0x6e5660 (CL9 hito 6, fase 5; 06 § Los diálogos del juego, "El
// modo de la caja del caos y la mezcla de joyas"): la clase de 0x46e290 (vtable 0x6b85f8), una lista de las ventanas
// viejas con filas de 0xc bytes. Su paso (0x483bc0) es el de la lista del registro del chat (0x479960,
// UI/ChatLogWindow) con otras funciones en su vtable: lee las flechas del teclado (+0xbc en 1), no tiene botones ni
// borde y cada fila elige con el clic y pide con el derecho suelto. Ninguna saca rand(): el inventario no está en la
// tabla de protección.
namespace Client::Scenes
{
struct GameHudContext;
struct GameUiContext;

// 0x402a10 → 0x482f80, la vtable + 0x2c: vacía las filas, pone sus banderas en 0 (+0x48, 0x483610, sobre ninguna), el
// elegido en 0 (+0x58) y el primero que se ve en 0.
void ClearTheBundles(JewelBundleList& list);

// 0x46e940, la vtable + 0x94 (desde 0x402a20): agrega {0, casilla, nivel} al final si la casilla está en [0, 0x40] y
// no está ya; el nivel no rechaza nada (07 Q-C954). Después el elegido en 0, el primero que se ve corrido para que la
// última fila quede a la vista y las filas ordenadas por casilla (0x46e4c0 con 0x46e4a0).
void AddTheBundle(JewelBundleList& list, std::int32_t cell, std::uint8_t level);

// 0x483aa0, la vtable + 0x64: la fila elegida, o ninguna (el final del deque) sin filas o sin elegido. Sin filas, pone
// el elegido en 0.
[[nodiscard]] std::optional<JewelBundleList::Row> ChosenBundle(JewelBundleList& list);

// Lo que deja el paso.
struct BundleListStep
{
    // 0x46f297: la fila del derecho suelto que pasa los chequeos de 0x46f27e..0x46f290, para 0x402a90(casilla,
    // nivel). En el original va en medio de la vuelta por las filas; lo que sigue de la vuelta no lee nada de lo que
    // escribe 0x402a90 (el diálogo, la mezcla y el área de mensajes), así que quien llama la pide al volver.
    std::optional<JewelBundleList::Row> asked{};
};

// 0x402af0: 0x5762eb4 en el id de la lista, su paso (0x45aa80(0): la cola con la vtable + 0x28, 0x4834a0, el mouse
// sobre la lista como la de abajo y la vtable + 0x20, 0x483bc0) y 0x5762eb4 en 0. Con el mouse agarrado por la propia
// lista durante el paso, el cursor de la barra no se agarra nunca (07 Q-C1151). Los sonidos (0x41ed10(0x19)) quedan
// en WorldRuntime::lastSoundId (CL10).
[[nodiscard]] BundleListStep StepTheBundleList(const GameUiContext& context);

// 0x45a9d0(x, y) y 0x402b20 → 0x4832e0, la vtable + 0x14: la esquina de abajo a la izquierda en (x, y), el marco con
// las flechas y la barra (0x46ebe0, que la guarda para el paso), y desde el primero que se ve, hasta 8 filas
// (0x46f020) con el nombre de la joya (el 1806 o el 1807, por 0x6c62e0) y las joyas del paquete, en la fuente
// normal. Un dibujo intermedio no escribe la lista. Corta sin el texto de la joya.
[[nodiscard]] Core::Expected<void, std::string> DrawTheBundleList(GameHudContext& context, std::int32_t x,
                                                                  std::int32_t y);
} // namespace Client::Scenes
