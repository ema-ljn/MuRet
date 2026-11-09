#pragma once
#include "World/WorldObjectRegistrations.h"
#include <glm/ext/vector_float3.hpp>
#include <cstdint>
#include <optional>
namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
class IWorldObjectDrawingServices;
// Estado vivo prestado, sin realocación de objetos/modelos durante servicios.
struct WorldObjectDrawingContext
{
    const std::int32_t& map;
    const std::int32_t& scene; // valor nativo2, no ordinal ObjectScene
    const float& worldTime;
    const std::int32_t& editorState;
    const WorldObjectIdentity& hero;
    Render::Models::CrtRandom& random;
    IWorldObjectDrawingServices& services;
    // Residuos602ed0 sólo con evidencia concreta del caller; nullopt NO significa0.
    std::optional<glm::vec3> effectsInput;
    std::optional<glm::vec3> effectsLight;
};
} // namespace Client::World
