#pragma once
#include "Core/Expected.h"
#include <cstdint>
#include <string>
namespace Client::Data::Terrain
{
struct TerrainHeights;
}
namespace Client::Terrain
{
struct WorldTerrain;
// 0x5f9580: a signed radial sum, without clamp or RNG, wrapping both indices.
[[nodiscard]] Core::Expected<void, std::string> DeformTerrainHeights(Data::Terrain::TerrainHeights& heights, float x,
                                                                     float y, float change, std::int32_t radius);
// 0x5283df → 0x5f9580(−30, 2), 0x5f8c10 and 0x5f8e40, in that order.
[[nodiscard]] Core::Expected<void, std::string> DeformTheSiegeTerrain(WorldTerrain& terrain, float x, float y,
                                                                      std::int32_t tileX, std::int32_t tileY);
} // namespace Client::Terrain
