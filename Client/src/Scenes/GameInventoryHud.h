#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// El inventario en el HUD del juego (06 § El inventario, CL8 hito 4): la ventana (0x5f2340 con 0x5ee220, 0x5e2460,
// 0x5e3660 y el cartel del set 0x40e720), los ítems 3D de las grillas (0x5f7610 → 0x5e2fc0 y 0x5e3760), las cifras
// (0x5f7840) y el ítem del cursor (0x5f7b10). En 640 × 480, con el estado de GL que dejan, y con los rand() de la
// tabla de protección y del dibujo de cada ítem en su orden. Las grillas de las otras ventanas (la tienda, el
// almacén, la caja del caos y la caja del entrenador) cortan con su dirección; las del comercio y la de las tiendas
// personales van con su ventana (0x5f3b40 y 0x5f3030).
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

// 0x5f2340: con el inventario abierto, la ventana en (0x1c2, 0), o en (0x104, 0) con el personaje o la party
// abiertos: el fondo, los lugares del equipo con el color de su aviso, la grilla de 8 × 8, el título, el zen, los
// botones de cerrar, reparar y la tienda personal con su ayuda, y el cartel del set. Saca 3.848 rand() por cada
// cierre del registro propio que lo deja sin accesos (los lugares, el zen y el nivel) y uno por cada consulta del
// comercio y de la tienda.
[[nodiscard]] Core::Expected<void, std::string> DrawTheInventory(GameHudContext& context, UI::PanelRegistry& panels);

// 0x5f3b40: con el comercio abierto (IsOpen(0x20), su rand()), la ventana en (0x104, 0) (0x78a5a1c y 0x78a5a18): el
// fondo con la línea (0x5ee220 con 1), las cajas de las dos grillas (0x5e3660: la del otro en y + 70, la propia en
// y + 270), la marca del gremio del otro (nada: la tabla de los gremios es del hito 8), el título (226), el OK del
// otro (accept_box 0x122 o 0x123, en rojo con 0x78abac4 > 0), los botones del zen (Money_01/02), del OK propio (en rojo
// con la espera 0x78abac8) y de cerrar, con su ayuda (227 a 229), el zen de los dos (0x10f, "%d,%03d" y el color por
// cuánto es), el nombre del otro, la flecha que sube y baja (9), su nivel por piso (368 y 369), el nombre del héroe y
// los renglones 370 (que late), 365, 366 y 367. Sin más rand().
[[nodiscard]] Core::Expected<void, std::string> DrawTheTradeWindow(GameHudContext& context, UI::PanelRegistry& panels);

// 0x5f3030 (CL9 hito 7): con la tienda personal propia o la de otro abiertas (0x800 o 0x20000, sin rand()), la ventana
// en (0x104, 0) (0x78aba58 y 0x78aba5c), en la negrita: el fondo (0x5ee220 con 0) y el título (1102); con la propia
// (0x6c931c en 1), la franja de 150 de ancho, verde con la tienda abierta (0x78aba50) o roja, con 1103 o 560; la caja
// del título (Item_Money, 0x10f) con el de 0x78aba64 o, sin él y con la propia, el 1105; las casillas de la grilla
// 0x78ab0b8 (0x5e3660) en (x + 15, y + 110); con la propia, los botones (text_button, 0x659) de abrir o aplicar (1107
// o 1106) y de cerrar (1108, gris con la tienda cerrada); el aviso que late (370) y los renglones de la propia (1109,
// 1111, 1112, 1113 y 1115) o de la de otro (1110, 1111, 1112, 1114 y 1116), con los dos últimos (1134 y 1135) latiendo.
[[nodiscard]] Core::Expected<void, std::string> DrawThePersonalShopWindow(GameHudContext& context,
                                                                          UI::PanelRegistry& panels);

// 0x5f26a7..0x5f2751: el zen con comas cada tres cifras, en divisiones sin signo ("%d,%03d,%03d,%03d" desde mil
// millones, y así hasta "%d"). Lo usa también el precio de la tienda personal en la ayuda del ítem
// (0x5b2ff9..0x5b30ba). Al mergear la CL9 hito 6 queda el Scenes::FormatZen de Scenes/ZenText.h (0x5b1130, la misma
// cuenta) y este se va.
[[nodiscard]] std::string FormatZen(std::uint32_t zen);

// 0x5f7610: los ítems 3D de las grillas abiertas, en su orden. Del inventario, el equipo (0x5e2fc0, con el registro
// propio abierto alrededor, cada lugar en su caja) y la grilla (0x5e3760 con 0, con el tamaño de Item.bmd); los de las
// demás cortan con su dirección.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGridItems(GameHudContext& context, UI::PanelRegistry& panels);

// 0x5f7840: las cifras de las grillas (0x5e3760 con 1): las pociones con más de 1 y los paquetes de joyas.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGridNumbers(GameHudContext& context, UI::PanelRegistry& panels);

// 0x5f7b10: el ítem del cursor (0x7879f80), del tamaño de Item.bmd y centrado en el mouse, con el último argumento
// de 0x5e1480 en 1.
[[nodiscard]] Core::Expected<void, std::string> DrawTheCursorItem(GameHudContext& context);
} // namespace Client::Scenes
