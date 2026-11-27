#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Actors
{
// 0x5069ae: the twenty effects of the brake. The callbacks share the caller's CRT, including the rand() calls the
// effect's creator spends. Empty stops before the creator, after the prefix.
struct ActorCooldownServices
{
    std::function<Core::Expected<void, std::string>(const glm::vec3&, const Kernel::ActorState&)> particle;
    std::function<Core::Expected<void, std::string>(std::int32_t, const Kernel::ActorState&)> effect;
};
} // namespace Client::Actors
