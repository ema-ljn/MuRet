#pragma once
#include "Core/Expected.h"
#include <string>
namespace Client::Kernel
{
struct ObjectStepGlobals;
struct WorldObject;
} // namespace Client::Kernel

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::MapScripts::Noria
{
[[nodiscard]] Core::Expected<void, std::string> StepNoriaObject(Kernel::WorldObject& object, Crt::CrtRandom& random,
                                                                const Kernel::ObjectStepGlobals& globals);
} // namespace Client::MapScripts::Noria
