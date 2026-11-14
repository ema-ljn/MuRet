#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"

#include <cstdint>
#include <string>

// Los pasos de las ventanas de los NPC (CL9 hito 6, fase 2): la tienda (0x5ed960) con lo que cuesta reparar todo
// (0x5b1570), el almacén (0x5ea060), la caja del caos (0x5fe610), vender el ítem del cursor (0x5deca7..0x5df448), las
// dos confirmaciones del ítem del cursor (0x5d8dee..0x5d9659) y el OK del zen del almacén (0x6270ed..0x6282c8). Los
// llaman 0x5ee140 (Scenes::StepTheGameUi) y 0x623e70 (Scenes::StepTheOpenDialog). Contrato:
// Client/docs/research/04-protocolo.md, "Las ventanas de los NPC", y 06 § El inventario.
namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::UI
{
class PanelRegistry;
}

namespace Client::Scenes
{
class WorldState;
struct GameUiContext;
struct WorldRuntime;

// 0x5e7aa0(modo): el teclado del almacén barajado (Scenes::ShuffleTheKeypad, 40 rand()), lo tecleado (0x78ab9fc..) en
// 0 y el modo en 0x78abb20. En línea en soltar en el inventario con la traba (0x5cb3af..) y en el botón 2 del almacén
// (0x5ea1aa..); lo llaman el OK del 0x73 (0x627c33, con 1) y el del 0x8f (0x628276, con 6).
void OpenTheKeypad(WorldRuntime& runtime, Render::Models::CrtRandom& random, std::int32_t mode);

// 0x5ed960, con la tienda abierta (IsOpen(0x10000), su rand()). Con 0x78abb09: los dos botones del modo de reparar
// (x + 0x19 y x + 0x55, y + 0x16d), el de reparar todo (x + 0x73), que manda C1 05 34 FF 00 en C3 sin gastar el clic,
// y 0x5b1570. Siempre, el botón de cerrar en la esquina del inventario (x + 0x19, y + 0x18b), que cierra el inventario.
[[nodiscard]] Core::Expected<void, std::string> StepTheNpcShop(const GameUiContext& context, UI::PanelRegistry& panels);

// 0x5b1570: 0x78ababc en 0 y, con el registro propio abierto (3.848 rand() al cerrarlo), la suma de 0x5b13e0 sobre
// 0x54d0f0 en el modo 2 de cada ítem gastado (la durabilidad debajo de la máxima, 0x5b1ba0) de los doce lugares del
// equipo (con tipo) y de las 64 casillas del inventario (con +0x3c), salvo lo que no se repara (0x1a00 a 0x1a05,
// 0x1a0a, 0x1a1f, 0x807, 0x80f, de 0x1c00 en adelante, 0x1807 a 0x1813, 0x1a0e a 0x1a14, 0x1c15 y 0x1a25).
[[nodiscard]] Core::Expected<void, std::string>
PriceTheRepairOfEverything(WorldState& world, Render::Models::CrtRandom& random, const Data::Tables::ItemTable* items);

// 0x5ea060, con el almacén abierto: el primero de los cuatro botones (x + 0x1a, 0x4c y 0x7e en y + 0x186, y x + 0xd7 en
// y + 0x18b, de 0x18 × 0x18) bajo el mouse; con el clic, lo gasta y suena 0x19. 0 y 1: el diálogo 0x73 para meter o
// sacar zen (0x78abacc), los campos de texto en 0 y el campo numérico; 2: el teclado barajado y 0x78abb20 en 4, o 3
// con la traba (0x6c932f); 3: cerrar el almacén.
[[nodiscard]] Core::Expected<void, std::string> StepTheWarehouse(const GameUiContext& context,
                                                                 UI::PanelRegistry& panels);

// 0x5fe610, con la caja del caos abierta: el botón de mezclar (x + 0x4b, y + 300, de 0x2c × 0x20) con 0x7d2dc80 en 0
// corta, porque el tipo de mezcla (0x7d2dc98) lo arma 0x5fd4f0 y su precio (0x7d2dca0) el dibujo; el botón de cerrar
// en la esquina del inventario cierra la caja.
[[nodiscard]] Core::Expected<void, std::string> StepTheChaosBox(const GameUiContext& context,
                                                                UI::PanelRegistry& panels);

// 0x5d8dee..0x5d9659, el principio de 0x5d8d50 sin la traba del 24: con 0x78abb14 en 1, la venta que espera el
// diálogo 0x98 (0x6c932e en 1: el C1 04 33 en C3; en 2, 0x5be150); con 2, la mezcla (en 1: 0x7d2dc80 en 1 y el
// C1 04 86 en claro; en 2, nada); con 4, el del panel 0x2000 (0x5d9b82: en 1, el teclado del almacén barajado en
// 6; en 2, 0x78abb08 en 0). En todos, 0x78abb14 en 0 y 0x6c932e en 0xff al contestar. Devuelve verdadero si
// 0x5d8d50 sale (0x78abb14 de 1 a 6). El 3, el 5 y el 6 son del hito 7: cortan el sí de los tres (0x5d966b, 0x5d9c82,
// 0x5da5ac) y el no del 6 (0x5dc765); el no del 3 y del 5 es el de la mezcla (0x5d9634).
[[nodiscard]] Core::Expected<bool, std::string> StepTheCursorChoice(const GameUiContext& context);

// 0x5deca7..0x5df448, con el clic fuera de las grillas: con la tienda abierta (su rand()) y el mouse en su grilla
// ([x + 0xf, x + 0xc3) × [y + 0x32, y + 0x172)), lo que no se vende no hace nada, lo valioso abre el diálogo 0x98 con
// 0x78abb14 en 1, y lo demás sale con C1 04 33 en C3. Devuelve falso si sigue al piso (0x5df449).
[[nodiscard]] Core::Expected<bool, std::string> SellTheCursorItem(const GameUiContext& context,
                                                                  UI::PanelRegistry& panels);

// 0x626460..0x6282c8, el OK del diálogo 0x73 (con Enter): más de 100.000.000 abre el 0x75; con el comercio abierto (su
// rand()), el zen del comercio corta (CL9.4); si no, el zen del almacén (0x6270ed): con el registro propio abierto,
// meter (0x78abacc en 0) o sacar, el C1 08 81 en claro o el 0x74; sacar con la traba espera la contraseña (−1, 1, el
// zen y 0x77f3cc8) y abre el teclado (0x5e7aa0(1)). Después, los campos de texto como antes del 0x73 y el sonido.
[[nodiscard]] Core::Expected<void, std::string> ConfirmTheMoneyDialog(const GameUiContext& context);
} // namespace Client::Scenes
