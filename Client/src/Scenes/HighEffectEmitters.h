#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// Lo que piden las altas y los pasos de los tipos altos (HighModelEffects y sus familias): el joint (0x530860), la
// partícula (0x53fe70) y el sprite (0x5491e0), con la dirección del que los pide en el error.
namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

namespace Client::World
{
struct ModelEffect;
} // namespace Client::World

namespace Client::Scenes
{
class WorldState;
struct HighEffectStepContext;

// 0x530860(tipo, punto, punto2, ángulos, subtipo, dueño, escala, clave, 0, 0, −1): el joint, con la dirección del que
// lo pide delante del error de un joint que MuRet no tiene (el lote de los joints de la CL11) y "(CL11)" detrás.
[[nodiscard]] Core::Expected<void, std::string> CreateHighJoint(WorldState& world, Render::Models::CrtRandom& random,
                                                                std::int32_t type, glm::vec3 first, glm::vec3 second,
                                                                glm::vec3 angles, std::int32_t subtype,
                                                                World::ModelEffect* owner, float scale,
                                                                std::uint32_t address);

// 0x53fe70 de una partícula, con el error del pool como texto y la dirección del que la pide. Con el pool lleno,
// nullopt y sin rand().
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateHighParticle(WorldState& world, Render::Models::CrtRandom& random, float worldClock, std::int32_t type,
                   const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light, std::int32_t subtype,
                   float scale, std::uint32_t address);

// 0x5491e0 desde un paso: el sprite con la luz y la rotación, en el lugar libre (el dueño, que MuRet no guarda). Sin
// la tabla de sprites en los servicios, corta.
[[nodiscard]] Core::Expected<void, std::string> CreateHighSprite(const HighEffectStepContext& context,
                                                                 std::uint16_t texture, const glm::vec3& position,
                                                                 float scale, const glm::vec3& light, float rotation,
                                                                 std::uint32_t address);
} // namespace Client::Scenes
