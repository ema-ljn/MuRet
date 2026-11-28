#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <string>

namespace Client::Kernel
{
struct GameObjectPass;
struct WorldObject;
} // namespace Client::Kernel

namespace Client::Render
{
class ModelBatch;
}
namespace Client::Models
{
class IModelLibrary;
}

namespace Client::Actors::Drawing
{
struct DrawnObject;
} // namespace Client::Actors::Drawing

namespace Client::MapScripts::Kalima
{
// 0x438fe0/0x43948d: the public tail of the Kalima body, which the caller already emitted.
[[nodiscard]] Core::Expected<void, std::string>
DrawKalimaObjectTail(Render::ModelBatch& batch, const Models::IModelLibrary& models, const Kernel::WorldObject& object,
                     const Actors::Drawing::DrawnObject& drawn, Core::Duration frameClock,
                     const Kernel::GameObjectPass& pass);
// 0x6044ed→0x438c10 after the common rand(). Failed prefixes stay visible to the caller.
[[nodiscard]] Core::Expected<void, std::string> StepKalimaObjectFrame(Kernel::WorldObject& object,
                                                                      const Kernel::GameObjectPass& pass);
} // namespace Client::MapScripts::Kalima
