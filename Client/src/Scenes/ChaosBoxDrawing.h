#pragma once

#include "Core/Expected.h"

#include <string>

// El dibujo de la caja del caos (0x5febf0, CL9 hito 6; 06 § La caja del caos), que 0x5f7440 llama siempre después de
// la tienda: la ventana en (0x104, 0), la grilla de 8 × 4, el título del modo (0x7d3a114) con el impuesto (0x78aba34),
// y con la mezcla sin pedir (0x7d2dc80 menor que 2) el nombre, la chance y el precio de la mezcla que dejó 0x5fd4f0
// (Scenes/ChaosMix.h), el aviso de la joya del caos, los de cada tipo y el botón de mezclar con su ayuda. Escribe el
// precio (0x7d2dca0) que lee el botón de mezclar del paso siguiente. No saca rand().
namespace Client::UI
{
class PanelRegistry;
}

namespace Client::Scenes
{
struct GameHudContext;

// 0x5febf0: sin la caja abierta (IsOpen(0x100), que no saca rand()), nada. Abierta, pone la esquina de la ventana
// del NPC (0x78aba28 y 0x78aba2c) en (0x104, 0) y dibuja. Deja la fuente gruesa y los colores del último renglón, o,
// con el mouse sobre el botón de mezclar, la normal y el blanco sobre transparente de su ayuda (0x554340). Un texto
// de Text.bmd que falta, o uno que desborda su buffer de la pila, corta.
[[nodiscard]] Core::Expected<void, std::string> DrawTheChaosBox(GameHudContext& context, UI::PanelRegistry& panels);
} // namespace Client::Scenes
