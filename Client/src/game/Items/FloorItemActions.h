#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <span>
#include <string>

namespace Client::Terrain
{
struct TerrainPickingSegment;
struct WorldTerrain;
} // namespace Client::Terrain

// Picking and picking up the floor items (CL9 milestone 5; 06 § Los ítems del piso): the frame's picking search
// (0x59e2e0), the room in the grid (0x5c9d20), 0x5650c0's item branch (its case 1, 0x566755) and the space bar
// (0x5ac928..0x5ac9c2). MoveHero's item branch (0x59dc5e..0x59ddea) is in GameMovement, with its path.
namespace Client::Kernel
{
struct FloorItemState;
struct ItemState;
} // namespace Client::Kernel

namespace Client::Items
{
struct InventoryMoveContext;

// 0x59e2e0, with no rand(): each live item in view (+0x16c) gets the vertex pass light (+0xe8 at 1) and 0.2
// (+0xf4..+0xfc); then the first live one in view whose box (+0x13c, the last draw's) touches the mouse segment
// (0x622900 with 0x7d36834 and 0x7d366c0) gets +0xe8 at 0 and the light at 1.5. Returns its index or −1.
[[nodiscard]] std::int32_t PickTheFloorItem(std::span<Kernel::FloorItemState> items,
                                            const Terrain::TerrainPickingSegment& ray);

// 0x5c9d20(type, 0x78a9bb0, 8, 8): whether the type's footprint (Item.bmd +0x24 and +0x25) fits somewhere in the grid
// with all its cells empty (type 0xffff). The original starts at the bottom right; since it only returns whether it
// fits, the order does not show.
[[nodiscard]] Core::Expected<bool, std::string> HasRoomFor(const Tables::ItemTable* items,
                                                           std::span<const Kernel::ItemState> cells, std::int16_t type);

// 0x5650c0 with +0x379 at 1 (0x566755..0x566eb3), on the picked item (0x77f27b0). Without the space bar (0x77f3b40),
// with `actor` (the second argument's position) more than 150 from the target cell's center (0x77e1ba8 and 0x77e1bac
// · 100 + 50), nothing; otherwise 0x6c8dcc at 6. Zen (0x1c0f): with a request in 0x6c82e8, nothing; otherwise the
// request and the C1 05 22 with the index (the C3's two rand()). Another item: with no room in the grid (0x5c9d20),
// notice 375 of type 1, the item 3 above the terrain (0x5f93c0 on `terrain` with `fixedHeight`) and its drop at 50;
// with room, as with zen.
[[nodiscard]] Core::Expected<void, std::string> PickUpTheFloorItem(const InventoryMoveContext& context,
                                                                   const glm::vec3& actor,
                                                                   const Terrain::WorldTerrain& terrain,
                                                                   float fixedHeight);

// 0x5ac928..0x5ac9c2, the space bar (the caller already checked the cursor, the key edge, the chat and 0x775857c): the
// first live item in view within 300 of the hero: the hero's +0x379 at 1, the picked one, 0x77f3b40 at 1,
// 0x5650c0(hero, item, 1) and both back to 0.
[[nodiscard]] Core::Expected<void, std::string>
PickUpTheNearestFloorItem(const InventoryMoveContext& context, const Terrain::WorldTerrain& terrain, float fixedHeight);
} // namespace Client::Items
