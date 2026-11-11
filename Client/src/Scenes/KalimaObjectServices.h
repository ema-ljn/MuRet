#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <string>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::World
{
struct WorldObject;
}
namespace Client::Scenes
{
class IModelLibrary;
struct DrawnObject;
struct GameObjectPass;
// 438fe0/43948d: cola pública del cuerpo de Kalima, ya emitido por el caller.
[[nodiscard]] Core::Expected<void, std::string>
DrawKalimaObjectTail(Render::ModelBatch& batch, const IModelLibrary& models, const World::WorldObject& object,
                     const DrawnObject& drawn, Core::Duration frameClock, const GameObjectPass& pass);
// 6044ed→438c10 después del rand común. Los prefijos fallidos quedan visibles al caller.
[[nodiscard]] Core::Expected<void, std::string> StepKalimaObjectFrame(World::WorldObject& object,
                                                                      const GameObjectPass& pass);
} // namespace Client::Scenes
