#pragma once
#include "Core/Expected.h"
#include <string>
namespace Client::Crt
{
class CrtRandom;
}
namespace Client::World
{
struct WorldObject;
struct ObjectStepGlobals;
[[nodiscard]] Core::Expected<void, std::string> StepNoriaObject(WorldObject& object, Crt::CrtRandom& random,
                                                                const ObjectStepGlobals& globals);
} // namespace Client::World
