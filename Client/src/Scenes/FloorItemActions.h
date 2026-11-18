#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <span>
#include <string>

// Elegir y levantar los ítems del piso (CL9 hito 5; 06 § Los ítems del piso): la búsqueda del picking del frame
// (0x59e2e0), el lugar en la grilla (0x5c9d20), la rama del ítem de 0x5650c0 (su caso 1, 0x566755) y la barra
// (0x5ac928..0x5ac9c2). La rama del ítem de MoveHero (0x59dc5e..0x59ddea) está en GameMovement, con su camino.
namespace Client::World
{
struct ItemState;
struct TerrainPickingSegment;
} // namespace Client::World

namespace Client::Scenes
{
struct FloorItemState;
struct InventoryMoveContext;

// 0x59e2e0, sin rand(): cada ítem vivo y a la vista (+0x16c) con la luz del pase de vértices (+0xe8 en 1) y 0,2
// (+0xf4..+0xfc); después, el primero vivo y a la vista cuya caja (+0x13c, la del último dibujo) toca el segmento
// del mouse (0x622900 con 0x7d36834 y 0x7d366c0), con +0xe8 en 0 y la luz en 1,5. Devuelve su índice o −1.
[[nodiscard]] std::int32_t PickTheFloorItem(std::span<FloorItemState> items, const World::TerrainPickingSegment& ray);

// 0x5c9d20(tipo, 0x78a9bb0, 8, 8): si la huella del tipo (Item.bmd +0x24 y +0x25) entra en algún lugar de la grilla
// con todas sus casillas vacías (tipo 0xffff). El original empieza por abajo a la derecha; como solo devuelve si
// entra, el orden no se ve.
[[nodiscard]] Core::Expected<bool, std::string> HasRoomFor(const Data::Tables::ItemTable* items,
                                                           std::span<const World::ItemState> cells, std::int16_t type);

// 0x5650c0 con +0x379 en 1 (0x566755..0x566eb3), sobre el ítem elegido (0x77f27b0). Sin la barra (0x77f3b40), con
// `actor` (la posición del segundo argumento) a más de 150 del centro de la celda del destino (0x77e1ba8 y 0x77e1bac
// · 100 + 50), nada; si no, 0x6c8dcc en 6. El zen (0x1c0f): con un pedido en 0x6c82e8, nada; si no, el pedido y el
// C1 05 22 con el índice (los dos rand() del C3). Otro ítem: sin lugar en la grilla (0x5c9d20), el aviso 375 del
// tipo 1, el ítem a 3 sobre el terreno (0x5f93c0 con `fixedHeight`) y su caída en 50; con lugar, como el zen.
[[nodiscard]] Core::Expected<void, std::string> PickUpTheFloorItem(const InventoryMoveContext& context,
                                                                   const glm::vec3& actor, float fixedHeight);

// 0x5ac928..0x5ac9c2, la barra (quien llama ya miró el cursor, el flanco, el chat y 0x775857c): el primer ítem vivo y
// a la vista a menos de 300 del héroe: +0x379 del héroe en 1, el elegido, 0x77f3b40 en 1, 0x5650c0(héroe, ítem, 1)
// y los dos en 0 otra vez.
[[nodiscard]] Core::Expected<void, std::string> PickUpTheNearestFloorItem(const InventoryMoveContext& context,
                                                                          float fixedHeight);
} // namespace Client::Scenes
