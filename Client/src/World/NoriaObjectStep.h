#pragma once
#include "Core/Expected.h"
#include <string>
namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
struct WorldObject;
struct ObjectStepGlobals;
[[nodiscard]] Core::Expected<void, std::string> StepNoriaObject(WorldObject& object, Render::Models::CrtRandom& random,
                                                                const ObjectStepGlobals& globals);
} // namespace Client::World
