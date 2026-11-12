#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// El inventario en el HUD del juego (06 § El inventario, CL8 hito 4): la ventana (0x5f2340 con 0x5ee220, 0x5e2460,
// 0x5e3660 y el cartel del set 0x40e720), los ítems 3D de las grillas (0x5f7610 → 0x5e2fc0 y 0x5e3760), las cifras
// (0x5f7840) y el ítem del cursor (0x5f7b10). En 640 × 480, con el estado de GL que dejan, y con los rand() de la
// tabla de protección y del dibujo de cada ítem en su orden. Las grillas de las otras ventanas (la tienda, el
// comercio, el almacén, la caja del caos, las tiendas personales y la caja del entrenador) cortan con su dirección.
//
// El modelo de un ítem de la UI (0x5e1480 → 0x5e0720) no se dibuja (08, P-C70): MuRet saca sus rand(), los de
// 0x607b60, 0x609640 y el pase del set de 0x60b3d0, en su orden.
namespace Client::UI
{
class PanelRegistry;
}

namespace Client::Scenes
{
struct GameHudContext;

// 0x5e1480(x, y, w, h, tipo, nivel, excelente, set, siempre) sin el dibujo: el modelo que elige por el tipo y el
// nivel (+4, en 32 bits con signo), y con él los rand() de 0x607b60 (1 con 0x220 y 0xc19, 2 con 0xc17 y 0xe20), los
// de 0x609640 (2 con 0xe21 y 0xe22) y el del pase del set (0x60cc29: con el set en 1 o 2, sin excelente y con
// sin(0,001 · T) · 0,5 + 0,4 en 0,01 o menos, rand() % 100 en el +0xc0 del modelo). Con algunos tipos y niveles no
// dibuja nada y no saca ninguno. El 0x1a18 corta antes de sus uniones (0x530860).
[[nodiscard]] Core::Expected<void, std::string>
DrawUiItem(GameHudContext& context, std::int32_t type, std::uint32_t level, std::uint8_t excellent, std::uint8_t set);

// 0x5f2340: con el inventario abierto, la ventana en (0x1c2, 0), o en (0x104, 0) con el personaje o la party
// abiertos: el fondo, los lugares del equipo con el color de su aviso, la grilla de 8 × 8, el título, el zen, los
// botones de cerrar, reparar y la tienda personal con su ayuda, y el cartel del set. Saca 3.848 rand() por cada
// cierre del registro propio que lo deja sin accesos (los lugares, el zen y el nivel) y uno por cada consulta del
// comercio y de la tienda.
[[nodiscard]] Core::Expected<void, std::string> DrawTheInventory(GameHudContext& context, UI::PanelRegistry& panels);

// 0x5f7610: los ítems 3D de las grillas abiertas, en su orden. Del inventario, el equipo (0x5e2fc0, con el registro
// propio abierto alrededor) y la grilla (0x5e3760 con 0); los de las demás cortan con su dirección.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGridItems(GameHudContext& context, UI::PanelRegistry& panels);

// 0x5f7840: las cifras de las grillas (0x5e3760 con 1): las pociones con más de 1 y los paquetes de joyas.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGridNumbers(GameHudContext& context, UI::PanelRegistry& panels);

// 0x5f7b10: el ítem del cursor (0x7879f80), centrado en el mouse, con el último argumento de 0x5e1480 en 1.
[[nodiscard]] Core::Expected<void, std::string> DrawTheCursorItem(GameHudContext& context);
} // namespace Client::Scenes
