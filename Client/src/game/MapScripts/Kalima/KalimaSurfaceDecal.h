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

// 437940→41dd00→41df90: calco sobre el plano0 de ondas, independiente del pase de agua.
[[nodiscard]] Core::Expected<void, std::string>
DrawKalimaSurfaceDecal(Render::ModelBatch& batch, const KalimaWorld& world, const Terrain::TerrainDecal& decal);
} // namespace Client::MapScripts::Kalima
