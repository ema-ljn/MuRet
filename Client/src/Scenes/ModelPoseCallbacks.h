#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <functional>
#include <span>
#include <string>

namespace Client::Render::Models
{
struct SkinnedMesh;
}
namespace Client::Render
{
class ModelBatch;
}
namespace Client::Scenes
{
struct DrawnObject;
class IModelLibrary;
using TailPoseObserver = std::function<Core::Expected<void, std::string>(const DrawnObject&)>;
// onPose y este getter comparten el mismo scratch: el cuerpo no vuelve a posar desde identidades.
using TailMeshProvider = std::function<std::span<const Render::Models::SkinnedMesh>()>;
// 4ebc80 consume las matrices/mallas recién publicadas, incluidos los dummy retenidos.
[[nodiscard]] Core::Expected<void, std::string>
DrawPublishedObject(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                    Core::Duration frameClock, const TailMeshProvider& posedMeshes);
} // namespace Client::Scenes
