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
// 5f9580: suma radial firmada, sin clamp niRNG, wrapdeambosíndices.
[[nodiscard]] Core::Expected<void, std::string> DeformTerrainHeights(Data::Terrain::TerrainHeights& heights, float x,
                                                                     float y, float change, std::int32_t radius);
// 5283df→5f9580(−30,2),5f8c10 y5f8e40 eneseorden.
[[nodiscard]] Core::Expected<void, std::string> DeformTheSiegeTerrain(WorldTerrain& terrain, float x, float y,
                                                                      std::int32_t tileX, std::int32_t tileY);
} // namespace Client::Terrain
