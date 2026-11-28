#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <string>

namespace Client::Assets
{
class FaunaModels;
}

namespace Client::Models
{
struct Skeleton;
}

namespace Client::Kernel
{
struct ActorState;
class WorldState;
} // namespace Client::Kernel

namespace Client::WorldObjects
{

// 4eb860 con flags (0, 1): conserva los huesos dummy y publica la pose global y la key del modelo en `faunaModels`;
// sin ellos corta ahí.
[[nodiscard]] Core::Expected<void, std::string>
PoseWorldModel(Kernel::WorldState& world, Assets::FaunaModels* faunaModels, const Kernel::ActorState& actor,
               const Models::Skeleton& skeleton, float frame, const glm::vec3& angles);
} // namespace Client::WorldObjects
