#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Render::Models
{
struct ClothWorld;
}
namespace Client::Scenes
{
struct ActorState;
struct GameData;
class IModelLibrary;
class WorldState;
// Selección y RNG propios de4f2250; las fronteras aún no ligadas retornan error tras los stores previos.
[[nodiscard]] Core::Expected<void, std::string> SetActorStop(ActorState& actor, WorldState& world, const GameData& data,
                                                             const IModelLibrary& models,
                                                             Render::Models::ClothWorld& cloth);
[[nodiscard]] Core::Expected<void, std::string> SetActorWalk(ActorState& actor, WorldState& world, const GameData& data,
                                                             const IModelLibrary& models,
                                                             Render::Models::ClothWorld& cloth);
} // namespace Client::Scenes
