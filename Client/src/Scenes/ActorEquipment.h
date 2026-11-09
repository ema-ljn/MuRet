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
class WorldState;
// Proyecta el equipo propio sobre el actor canónico y actualiza sus stats con el mismo CRT.
[[nodiscard]] Core::Expected<void, std::string> ApplyActorEquipment(ActorState& actor, WorldState& world,
                                                                    Render::Models::ClothWorld& cloth);
} // namespace Client::Scenes
