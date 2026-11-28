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
// 438fe0/43948d: cola pública del cuerpo de Kalima, ya emitido por el caller.
[[nodiscard]] Core::Expected<void, std::string>
DrawKalimaObjectTail(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                     const WorldObjects::WorldObject& object, const Actors::Drawing::DrawnObject& drawn,
                     Core::Duration frameClock, const WorldObjects::GameObjectPass& pass);
// 6044ed→438c10 después del rand común. Los prefijos fallidos quedan visibles al caller.
[[nodiscard]] Core::Expected<void, std::string> StepKalimaObjectFrame(WorldObjects::WorldObject& object,
                                                                      const WorldObjects::GameObjectPass& pass);
} // namespace Client::MapScripts::Kalima
