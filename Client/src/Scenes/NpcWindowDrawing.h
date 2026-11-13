#pragma once

#include "Core/Expected.h"

#include <string>

// El dibujo de las ventanas de los NPC (CL9 hito 6, fase 3): la tienda (0x5f4720), en 640 × 480, con el estado de GL
// que deja y los globales de los textos en HudState. Los ítems 3D y las cifras de su grilla van después, en 0x5f7610 y
// 0x5f7840 (Scenes/GameInventoryHud.h), desde la esquina que deja este dibujo. Contrato: 06 § "Las ventanas de los
// NPC".
namespace Client::UI
{
class PanelRegistry;
}

namespace Client::Scenes
{
struct GameHudContext;

// 0x5f4720, desde 0x5f7440 (0x5f75ad): con la tienda abierta (0x10000, el byte protegido: 1 rand()), la esquina en
// (0x104, 0) (0x78aba28 y 0x78aba2c), el fondo (0x5ee220), las casillas de la grilla de 8 × 15 (0x5e3660), el título
// (texto 230) y el impuesto (texto 1623 con 0x78aba30) en negrita, los botones de comprar y vender (0x11a..0x11d, con
// 0x78abb27) y, con 0x78abb09, reparar (0x11e o 0x11f con 0x78abb0c) y reparar todo (0x11e en celeste, que con el clic
// pendiente lo gasta y se dibuja apretado), cada uno con su ayuda (231, 232, 233 y 237), y el precio de reparar todo
// (texto 239 y 0x78ababc con comas, en el color de su monto). Deja la fuente gruesa y el blanco.
[[nodiscard]] Core::Expected<void, std::string> DrawTheNpcShop(GameHudContext& context, UI::PanelRegistry& panels);
} // namespace Client::Scenes
