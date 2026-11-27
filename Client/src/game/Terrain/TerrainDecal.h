#pragma once

#include "Core/Expected.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <string>

// A decal on the ground (0x5fb2f0): a turned texture laid over the terrain cells around a point, each cell a
// four-vertex fan with the .OZB heights (0x5faec0). The ground marks call it (0x549130), and so do fourteen other
// unported places, among them the ground effects (0x52dc20). Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "Las marcas del suelo".
namespace Client::Terrain
{
// The arguments of 0x5fb2f0, in their order.
struct TerrainDecal
{
    // The texture slot, which 0x620780 binds.
    std::int32_t slot = 0;
    // The center, in the world.
    float x = 0.0f;
    float y = 0.0f;
    // The sides, in cells: the larger one sets how many cells it covers and the ratio stretches u.
    float width = 0.0f;
    float height = 0.0f;
    // glColor3fv with the alpha at 1 and glColor4f with another give the same.
    glm::vec3 color{};
    // The turn around z, in degrees (0x5fc8f0 with (0, 0, angle)).
    float angle = 0.0f;
    float alpha = 0.0f;
    // How much each vertex rises above its corner's height.
    float lift = 0.0f;
};

// 0x5fb2f0, with the blend and depth the caller left: binds the slot and, with n = ftol(larger side) + 1, draws the (2n
// + 1)² cells from (cx − n, cy − n) to (cx + n, cy + n) around the center's cell, each with the UVs of its place in the
// decal turned around (0.5, 0.5). Cells with x or y outside 0..254 are not drawn (0x5faec0). The arithmetic goes as in
// the x87: in 53 bits, stored as float where the binary stores it. Fails with a slot outside the table or with an ftol
// that does not give a 64-bit integer.
[[nodiscard]] Core::Expected<void, std::string>
DrawTerrainDecal(Render::ModelBatch& batch, const Data::Terrain::TerrainHeights& heights, const TerrainDecal& decal);
} // namespace Client::Terrain
