#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::World
{
class KalimaWorld;
struct TerrainDecal;

// 437940→41dd00→41df90: calco sobre el plano0 de ondas, independiente del pase de agua.
[[nodiscard]] Core::Expected<void, std::string>
DrawKalimaSurfaceDecal(Render::ModelBatch& batch, const KalimaWorld& world, const TerrainDecal& decal);
} // namespace Client::World
