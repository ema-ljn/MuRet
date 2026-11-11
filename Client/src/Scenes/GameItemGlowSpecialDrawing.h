#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <cstdint>
#include <span>
#include <string>

namespace Client::Render
{
class ModelBatch;
namespace Models
{
struct SkinnedMesh;
}
} // namespace Client::Render
namespace Client::Scenes
{
class IModelLibrary;
struct DrawnObject;
struct ItemDrawContext;
// true corresponde al RET temprano nativo; false continúa el cuerpo común de60b3d0.
[[nodiscard]] Core::Expected<bool, std::string>
DrawEarlyItemGlow(Render::ModelBatch& batch, const IModelLibrary& models, DrawnObject& object, Core::Duration clock,
                  std::span<const Render::Models::SkinnedMesh> meshes, int level, std::uint8_t packedLevel,
                  const ItemDrawContext& context);
} // namespace Client::Scenes
