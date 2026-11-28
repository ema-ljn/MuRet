#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <string>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::WorldObjects
{
struct WorldObject;
}
namespace Client::Models
{
class IModelLibrary;
}

namespace Client::Actors::Drawing
{
struct DrawnObject;
} // namespace Client::Actors::Drawing

namespace Client::WorldObjects
{
struct GameObjectPass;
} // namespace Client::WorldObjects

namespace Client::MapScripts::Kalima
{
// 0x438fe0/0x43948d: the public tail of the Kalima body, which the caller already emitted.
[[nodiscard]] Core::Expected<void, std::string>
DrawKalimaObjectTail(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                     const WorldObjects::WorldObject& object, const Actors::Drawing::DrawnObject& drawn,
                     Core::Duration frameClock, const WorldObjects::GameObjectPass& pass);
// 0x6044ed→0x438c10 after the common rand(). Failed prefixes stay visible to the caller.
[[nodiscard]] Core::Expected<void, std::string> StepKalimaObjectFrame(WorldObjects::WorldObject& object,
                                                                      const WorldObjects::GameObjectPass& pass);
} // namespace Client::MapScripts::Kalima
