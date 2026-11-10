#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Scenes
{
struct ActorState;
// 0x5069ae: los veinte efectos del freno. Los callbacks comparten el CRT del caller, incluyendo
// los rand que el creador del efecto gasta. Vacío corta antes del creador, después del prefijo.
struct ActorCooldownServices
{
    std::function<Core::Expected<void, std::string>(const glm::vec3&, const ActorState&)> particle;
    std::function<Core::Expected<void, std::string>(std::int32_t, const ActorState&)> effect;
};
} // namespace Client::Scenes
