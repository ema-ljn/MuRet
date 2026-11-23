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
struct ObjectModelStepState;
}
namespace Client::Render::Models
{
class IModelLibrary;
}

namespace Client::Scenes
{
struct DrawnObject;

// 0x430ae1/0x430b0f: RenderMesh de malla 0 con 0x44 y 0x240, textura−1 y el estado del objeto.
// Conserva los stores RGB por alpha entre pases y publica model+88 en 0, 1 y finalmente−1. Usa la pose previa.
[[nodiscard]] Core::Expected<void, std::string> DrawCrywolfAltarMeshes(Render::ModelBatch& batch,
                                                                       const Render::Models::IModelLibrary& models,
                                                                       DrawnObject& object, Core::Duration frameClock,
                                                                       World::ObjectModelStepState* modelState);
} // namespace Client::Scenes
