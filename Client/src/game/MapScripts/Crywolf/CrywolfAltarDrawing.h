#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <string>

namespace Client::Kernel
{
struct ObjectModelStepState;
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

namespace Client::MapScripts::Crywolf
{

// 0x430ae1/0x430b0f: RenderMesh of mesh 0 with 0x44 and 0x240, texture −1 and the object's state. Keeps the RGB
// stores by alpha between passes and publishes model+88 at 0, 1 and finally −1. Uses the previous pose.
[[nodiscard]] Core::Expected<void, std::string> DrawCrywolfAltarMeshes(Render::ModelBatch& batch,
                                                                       const Models::IModelLibrary& models,
                                                                       Actors::Drawing::DrawnObject& object,
                                                                       Core::Duration frameClock,
                                                                       Kernel::ObjectModelStepState* modelState);
} // namespace Client::MapScripts::Crywolf
