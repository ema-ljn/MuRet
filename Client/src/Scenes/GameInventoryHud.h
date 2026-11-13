#pragma once

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "World/ItemState.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// El inventario en el HUD del juego (06 § El inventario, CL8 hito 4): la ventana (0x5f2340 con 0x5ee220, 0x5e2460,
// 0x5e3660 y el cartel del set 0x40e720), los ítems 3D de las grillas (0x5f7610 → 0x5e2fc0 y 0x5e3760), las cifras
// (0x5f7840) y el ítem del cursor (0x5f7b10). En 640 × 480, con el estado de GL que dejan, y con los rand() de la
// tabla de protección y del dibujo de cada ítem en su orden. Las grillas de la tienda y del almacén van desde la
// esquina de la ventana del NPC (CL9 hito 6); las del comercio, la caja del caos, las tiendas personales y la caja del
// entrenador cortan con su dirección.
//
// El modelo de un ítem de la UI (0x5e1480 → 0x5e0720) va en el pase 3D de 0x5ac120 (Scenes/UiItemDrawing.h).
namespace Client::UI
{
class PanelRegistry;
}

namespace Client::Scenes
{
struct GameHudContext;
struct UiItemBox;

// 0x5e1480(x, y, w, h, tipo, nivel, excelente, set, siempre): el ítem de la caja `box` con el tipo y el nivel (+4, en
// 32 bits con signo). Con los modelos del contexto (GameHudContext::uiItems), lo pone delante de la cámara de la UI y
// lo dibuja en su pase (Scenes/UiItemDrawing.h), con los rand() de 0x607b60, 0x609640 y el pase del set de 0x60b3d0;
// sin ellos, saca los mismos rand() sin dibujar. Con algunos tipos y niveles no dibuja nada y no saca ninguno. El
// 0x1a18 corta antes de sus uniones (0x530860).
[[nodiscard]] Core::Expected<void, std::string> DrawUiItem(GameHudContext& context, const UiItemBox& box,
                                                           std::int32_t type, std::uint32_t level,
                                                           std::uint8_t excellent, std::uint8_t set, bool always);

// 0x5e3660(x, y, grilla, columnas, filas): una casilla de 20 × 20 por lugar, con el color del aviso de lo que haya
// (también de las vacías): Item_box (0x115) vacía, InventoryBox2 (0x116) con un tipo. La usan el inventario y las
// ventanas de los NPC (Scenes/NpcWindowDrawing.h).
void DrawTheGridBoxes(Render::SpriteBatch& sprites, float x, float y, std::span<const World::ItemState> cells,
                      std::size_t columns, std::size_t rows);

// 0x5f2340: con el inventario abierto, la ventana en (0x1c2, 0), o en (0x104, 0) con el personaje o la party
// abiertos: el fondo, los lugares del equipo con el color de su aviso, la grilla de 8 × 8, el título, el zen, los
// botones de cerrar, reparar y la tienda personal con su ayuda, y el cartel del set. Saca 3.848 rand() por cada
// cierre del registro propio que lo deja sin accesos (los lugares, el zen y el nivel) y uno por cada consulta del
// comercio y de la tienda.
[[nodiscard]] Core::Expected<void, std::string> DrawTheInventory(GameHudContext& context, UI::PanelRegistry& panels);

// 0x5f7610: los ítems 3D de las grillas abiertas, en su orden. Del inventario, el equipo (0x5e2fc0, con el registro
// propio abierto alrededor, cada lugar en su caja) y la grilla (0x5e3760 con 0, con el tamaño de Item.bmd); la de la
// tienda (con su rand()) y la del almacén, 8 × 15 desde la esquina de la ventana del NPC más (15, 50); las de las
// demás cortan con su dirección.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGridItems(GameHudContext& context, UI::PanelRegistry& panels);

// 0x5f7840: las cifras de las grillas (0x5e3760 con 1): las pociones con más de 1 y los paquetes de joyas.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGridNumbers(GameHudContext& context, UI::PanelRegistry& panels);

// 0x5f7b10: el ítem del cursor (0x7879f80), del tamaño de Item.bmd y centrado en el mouse, con el último argumento
// de 0x5e1480 en 1.
[[nodiscard]] Core::Expected<void, std::string> DrawTheCursorItem(GameHudContext& context);
} // namespace Client::Scenes
