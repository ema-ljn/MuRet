#pragma once
#include "Core/Expected.h"
#include <string>
namespace Client::Crt
{
class CrtRandom;
}
namespace Client::WorldObjects
{
struct WorldObject;
struct ObjectStepGlobals;
} // namespace Client::WorldObjects

namespace Client::MapScripts::Noria
{
[[nodiscard]] Core::Expected<void, std::string> StepNoriaObject(WorldObjects::WorldObject& object,
                                                                Crt::CrtRandom& random,
                                                                const WorldObjects::ObjectStepGlobals& globals);
} // namespace Client::MapScripts::Noria
