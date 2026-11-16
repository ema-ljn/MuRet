#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <string>

namespace Client::Render::Models
{
struct Skeleton;
}

namespace Client::Scenes
{
struct ActorState;
class WorldState;

// 4eb860 con flags (0, 1): conserva los huesos dummy y publica la pose global y la key del modelo.
[[nodiscard]] Core::Expected<void, std::string> PoseWorldModel(WorldState& world, const ActorState& actor,
                                                               const Render::Models::Skeleton& skeleton, float frame,
                                                               const glm::vec3& angles);
} // namespace Client::Scenes
