#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <string>

namespace Client::UI
{
class PanelRegistry;
}

// El clic derecho sobre un ítem de la grilla del inventario desde 0x5c7bc6 (CL9 hito 5, fase 2; 06 § Los ítems del
// piso, "Usar desde la grilla"): lo que 0x5c4110 hace después de las pociones, el pergamino del portal, la caja de la
// suerte y las entradas a los eventos.
namespace Client::Scenes
{
struct InventoryMoveContext;

// 0x5c7bc6..0x5c9b0b con el ítem de la casilla `index` (la que está bajo el mouse; la de arriba a la izquierda sale de
// su +0x42 y su +0x43):
// - los libros y las esferas de skills (0x1e00..0x1fff, 0x1807..0x180e y 0x1810..0x1818; 0x5c8fb9): con el registro
//   propio abierto (sus 3.848 rand() al cerrarlo), el nivel contra el +0x3a de Item.bmd y la energía y la fuerza con
//   lo que suman los ítems contra el +0x22 y el +0x1e del ítem; con los tres, lo de UseInventoryItem; si no, nada;
// - la fruta 0x1a0f (0x5c7c10): el nivel desde el registro propio (abrirlo y cerrarlo); con menos de 10, el aviso 749;
//   si no, el ítem en 0x78a59c8, y con el registro abierto otra vez el equipo y la clase: con algo puesto, el aviso
//   1909; la fruta de nivel 4 (el dword +4 en 0x20) sin la clase 4, el 1905; si no, el diálogo 0x96 con el texto 376,
//   que corta en 0x63330f (CL9.3);
// - el 0x1a0b (0x5c8448): de nivel 0, o de nivel 1 sin el héroe maestro del gremio (+0x368 en 0x80), lo de
//   UseInventoryItem; si no, nada;
// - los demás (0x5c8e37): la copia en el cursor (0x7879f80) con la grilla del inventario en 0x78ab9b8 y 0x78abb28 en
//   1; con el almacén abierto (IsOpen(0x40), sin rand()) corta en 0x5c8e8f (hito 6); si no, como 0x7d368a1 está en 0,
//   el cursor vuelve a quedar en ceros.
[[nodiscard]] Core::Expected<void, std::string> UseTheLaterGridItem(const InventoryMoveContext& context,
                                                                    UI::PanelRegistry& panels, std::size_t index);
} // namespace Client::Scenes
