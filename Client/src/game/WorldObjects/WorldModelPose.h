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

// 0x4eb860 with flags (0, 1): keeps the dummy bones and publishes the global pose and the model's key in
// `faunaModels`; without them it stops there.
[[nodiscard]] Core::Expected<void, std::string>
PoseWorldModel(Kernel::WorldState& world, Assets::FaunaModels* faunaModels, const Kernel::ActorState& actor,
               const Models::Skeleton& skeleton, float frame, const glm::vec3& angles);
} // namespace Client::WorldObjects
