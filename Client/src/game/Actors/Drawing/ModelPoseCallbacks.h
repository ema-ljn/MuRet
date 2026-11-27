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
// onPose y este getter comparten el mismo scratch: el cuerpo no vuelve a posar desde identidades.
using TailMeshProvider = std::function<std::span<const Models::SkinnedMesh>()>;
// 4ebc80 consume las matrices/mallas recién publicadas, incluidos los dummy retenidos.
[[nodiscard]] Core::Expected<void, std::string>
DrawPublishedObject(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                    Core::Duration frameClock, const TailMeshProvider& posedMeshes);
} // namespace Client::Actors::Drawing
