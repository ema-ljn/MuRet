#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <cstdint>
#include <span>
#include <string>

namespace Client::Render
{
class ModelBatch;
} // namespace Client::Render
namespace Client::Models
{
class IModelLibrary;
struct SkinnedMesh;
} // namespace Client::Models

namespace Client::Actors::Drawing
{
struct DrawnObject;
struct ItemDrawContext;
// true is the native early RET; false goes on with the common body of 0x60b3d0.
[[nodiscard]] Core::Expected<bool, std::string>
DrawEarlyItemGlow(Render::ModelBatch& batch, const Models::IModelLibrary& models, DrawnObject& object,
                  Core::Duration clock, std::span<const Models::SkinnedMesh> meshes, int level,
                  std::uint8_t packedLevel, const ItemDrawContext& context);
} // namespace Client::Actors::Drawing
