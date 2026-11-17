#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// CL9 hito 11, fase 2: las ventanas de los NPC de evento (06 § Las ventanas de los NPC de evento), la de Devil Square
// (el panel 0x1000) y la de Blood Castle (0x4000), con la esquina en (0x104, 0) a la derecha del inventario. El dibujo
// es *estático*, sin traza de GL (lo que la VM tiene que confirmar está en 08, P-C294).
namespace Client::UI
{
class PanelRegistry;
}

namespace Client::Scenes
{
struct GameHudContext;
struct GameUiContext;
struct OwnInventory;

// 0x5f58f0, desde 0x5f7440 sin condición: con el 0x1000, el fondo (0x5ee220), los seis botones (0xf0 de 140 × 35 desde
// (x + 25, 187), de a 40) con 645 o 646 y los niveles de la tabla 0x6ba4c8 (0x6ba4f8 con la clase 3 o 4 del registro
// propio, que se abre y cierra en cada fila), el título 39 y los textos 670 a 675. Con el 0x4000, los siete de Blood
// Castle (140 × 30 desde 170, de a 35) con 847 o 848 y la tabla 0x6ba528 (siete más con la clase 3 o 4 del héroe), los
// que no son del nivel del registro propio en gris, el título 846 y 832 en renglones de 26 bytes. Un dibujo de MuRet
// que proyecta no escribe la esquina ni los niveles de Devil Square.
[[nodiscard]] Core::Expected<void, std::string> DrawTheEventNpcWindow(GameHudContext& context,
                                                                      UI::PanelRegistry& panels);

// 0x5e5920, una de las hermanas del inventario (0x5ee1a3): con el clic sobre un cuadrado, el nivel contra los que dejó
// el dibujo (686 o 687 y el inventario cerrado) o la invitación 0x1c13 de nivel 0 o del cuadrado (677 sin ella) y el
// C1 05 90; sobre un puente de su nivel, la capa 0x1a12 del puente (la 7 vale también con la de nivel 6; 854 sin
// ella) y el C1 05 9A. Los dos con el XOR32, en claro.
[[nodiscard]] Core::Expected<void, std::string> StepTheEventNpcWindow(const GameUiContext& context,
                                                                      UI::PanelRegistry& panels);

// 0x5583a0(tipo, nivel): la casilla del inventario (0 a 63) de un ítem del tipo con +0x3c mayor que 0 y el nivel ((+4
// >> 3) & 0xf), o cualquiera con −1, recorriendo de la columna 7 a la 0 y en cada una de la fila 7 a la 0; −1 sin
// ninguno.
[[nodiscard]] std::int32_t FindInventoryItem(const OwnInventory& inventory, std::uint16_t type, std::int32_t level);

// 0x558740(tipo, nivel): cuántas casillas del inventario tienen el tipo (el word con signo), +0x3c mayor que 0 salvo
// con el tipo −1 y el nivel, o cualquiera con −1. Con (−1, −1) cuenta las libres.
[[nodiscard]] std::int32_t CountInventoryItems(const OwnInventory& inventory, std::int32_t type, std::int32_t level);
} // namespace Client::Scenes
