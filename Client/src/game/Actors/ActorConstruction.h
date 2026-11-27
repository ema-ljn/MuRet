#pragma once

#include "Core/Expected.h"
#include "Terrain/TerrainCulling.h"

#include <cstdint>
#include <string>

namespace Client::Models
{
struct ClothWorld;
}

namespace Client::Terrain
{
struct WorldTerrain;
}

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The character constructor, 0x511290. Contract: Client/docs/research/06-render-escenas-y-ui.md,
// "El héroe" and "Los personajes de la lista".
namespace Client::Actors
{

// What 0x511290 reads outside the character for the height. The caller lends it for the call.
struct ActorConstructionContext
{
    // The tables of the loaded world: the attributes and the heights.
    const Terrain::WorldTerrain& terrain;
    // DAT_006c9dd0, the current world: with −1 it does not add the pet's offset.
    std::int32_t map;
    // DAT_006d5dc8: 0x5f93c0 does not look at the tables in scenes 0, 1 and 3.
    Terrain::CullingScene scene;
    // 0x6c9d5c, the camera's fixed height (Camera::GameCamera::fixedHeight).
    float fixedHeight;
};

// Where the character goes: the server's cell and the +0x24 angle.
struct ActorPlacement
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    float angle = 0.0f;
};

// 0x5f93c0 returns 0 before reading the tables with these native scene values.
[[nodiscard]] bool HeightSkippedIn(Terrain::CullingScene scene);

// 0xa8..0xac: the five NewFace of 0x61ca30, with the generic path read from 0x511290.
[[nodiscard]] bool IsNewFace(std::uint16_t type);

// The Z of 0x511290 at (x, y): the terrain height (0x5f93c0) and, with the previous word318 at 0x1c15
// (`previousHad1c15`), a world and outside the safe zone, the pet's offset: 90 on maps 8 and 10 and 30 on the
// others. The sum stays on the stack: whoever stores it turns it into a float.
[[nodiscard]] double ConstructionHeight(const ActorConstructionContext& context, float x, float y, bool previousHad1c15,
                                        bool safe);

// 0x511290(character, 0x20a, X, Y, angle): alive, the cell in both its copies, the safe zone of the cell's .att, the
// position at the center of the cell with the Z of ConstructionHeight, the angle, the starting state (with +0x358 at
// 0) and the equipment parts without an item. Each access of the protection table (the two writes of the cell, its
// four reads and that of the step counter) opens and closes, and spends 4 rand() of `cloth` if it runs out of
// accesses: 28 from zero. It does not touch the key (+0x1f4). Any type: only 0x20a starts on action 1 and only the
// NewFace keep the class; the box (+0x124..+0x138) and the hand bones (+0x2bd and +0x2dd) go by the type's tables,
// with 0x43a250 and 0x42d7f0 in case 0x5126b2.
[[nodiscard]] Core::Expected<void, std::string> ConstructActor(Kernel::ActorState& actor, std::uint16_t type,
                                                               const ActorPlacement& placement,
                                                               const ActorConstructionContext& context,
                                                               Models::ClothWorld& cloth);
} // namespace Client::Actors
