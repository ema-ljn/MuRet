#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::Terrain
{
struct TerrainDecal;
} // namespace Client::Terrain

namespace Client::MapScripts::Kalima
{
class KalimaWorld;

// 0x437940→0x41dd00→0x41df90: a decal on wave plane 0, independent of the water pass.
[[nodiscard]] Core::Expected<void, std::string>
DrawKalimaSurfaceDecal(Render::ModelBatch& batch, const KalimaWorld& world, const Terrain::TerrainDecal& decal);
} // namespace Client::MapScripts::Kalima
