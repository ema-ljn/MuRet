#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// La ventana de la party (P), vacía (CL8 hito 6, 08 P-C92): su paso (0x5e3c80) y su dibujo (0x5f0be0). Con miembros
// (0x78aba48 distinto de 0), los dos cortan: la lista llega con el C1 42 (0x4d67e0), y los nombres, las barras y el
// botón de echar (C1 04 43) son de la CL9.4.
namespace Client::UI
{
class PanelRegistry;
}

namespace Client::Scenes
{
struct GameHudContext;
struct GameUiContext;

// 0x5e3c80, desde 0x5ee140 (0x5ee18d): con la party abierta, el mouse sobre la ventana (de la esquina que dejó el
// dibujo, 0xbe × 0x1b1) es de la UI (0x775857c), y el clic en el botón de cerrar (0x19, 0x18b, de 0x18 × 0x18) lo
// gasta, pone 0x77f3b10 en 0 y 0x6c8dcc en 6 y cierra la party con Close(4), que suena. No saca rand().
[[nodiscard]] Core::Expected<void, std::string> StepThePartyWindow(const GameUiContext& context,
                                                                   UI::PanelRegistry& panels);

// 0x5f0be0(x, y), lo primero de 0x5f7440, que lo llama con (0x1c2, 0). Con la party cerrada, el ancho del mundo
// (0x5bb8d0, que saca rand() con el inventario abierto), los colores −1 y 0 y nada más sin miembros; abierta, la
// esquina, el fondo sin mezcla, el título (texto 190) en negrita, los siete renglones de la party vacía (191 a 197) y
// el botón de cerrar con su ayuda (221). Deja la mezcla normal.
[[nodiscard]] Core::Expected<void, std::string> DrawThePartyWindow(GameHudContext& context, UI::PanelRegistry& panels,
                                                                   std::int32_t x, std::int32_t y);
} // namespace Client::Scenes
