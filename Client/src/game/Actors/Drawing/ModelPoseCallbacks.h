#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <functional>
#include <span>
#include <string>

namespace Client::Models
{
class IModelLibrary;
struct SkinnedMesh;
} // namespace Client::Models
namespace Client::Render
{
class ModelBatch;
}
namespace Client::Actors::Drawing
{
struct DrawnObject;
using TailPoseObserver = std::function<Core::Expected<void, std::string>(const DrawnObject&)>;
// onPose and this getter share the same scratch: the body does not pose again from identities.
using TailMeshProvider = std::function<std::span<const Models::SkinnedMesh>()>;
// 0x4ebc80 consumes the matrices and meshes just published, including the retained dummies.
[[nodiscard]] Core::Expected<void, std::string>
DrawPublishedObject(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                    Core::Duration frameClock, const TailMeshProvider& posedMeshes);
} // namespace Client::Actors::Drawing
