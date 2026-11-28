#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// La ventana de la party (P): su paso (0x5e3c80) y su dibujo (0x5f0be0), vacía desde la CL8 (hito 6, 08 P-C92) y con
// miembros desde la CL9 (hito 7: la lista llega con el C1 42, 0x4d67e0), y las barras de la party sobre las cabezas
// (0x5a89b0). Contrato: 06 § Lo social en pantalla.
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
// (0x5bb8d0, que saca rand() con el inventario abierto), los colores −1 y 0 y, con miembros, la lista de la derecha
// (0x5f130a): por cada uno, desde (ancho − 50, 5) de a 19, el fondo de 50 × 19 (más claro con el mouse; ninguno con
// +0x1c en −1), la barra de 43 × 4 y sus décimos (+0x18, hasta 10), el rojo encima si no está en el viewport o el verde
// con +0x20 & 8, y el nombre en la normal; los bordes (Message_box2) y los costados. Abierta, la esquina, el fondo sin
// mezcla, el título (texto 190) en negrita, los siete renglones de la party vacía (191 a 197) o, por miembro de a 0x23,
// el nombre en negrita (el primero, el jefe, con fondo rojo), el mapa (0x5f0b40) y la celda, la barra de la vida (la
// menor de +0x10 y +0x14 sobre +0x14, de 90) con sus números y, si el héroe es el jefe o ese miembro, el botón de echar
// con su ayuda (189); al final, el botón de cerrar con la suya (221). Deja la mezcla normal. Sin rand() más que los del
// ancho.
[[nodiscard]] Core::Expected<void, std::string> DrawThePartyWindow(GameHudContext& context, UI::PanelRegistry& panels,
                                                                   std::int32_t x, std::int32_t y);

// 0x5a89b0, el hijo 3 del HUD: con miembros (0x78aba48 en 1 o más), cada uno con personaje (+0x1c en 0 o más) tiene su
// barra sobre la cabeza (+0x138 + z + 100, proyectada con 0x6205b0), 19 a la izquierda: con el mouse encima, "HP :
// %d0%" 6 arriba; la sombra (0, 0, 0, 0,5) con la mezcla normal, y sumados el borde de 42 × 5, el fondo de 38 × 1 y
// los décimos (+0x18, hasta 10) de 3 × 2. Termina opaca y en blanco. Sin rand(). Sin la proyección del pase del
// mundo, corta.
[[nodiscard]] Core::Expected<void, std::string> DrawThePartyLife(GameHudContext& context);
} // namespace Client::Scenes
