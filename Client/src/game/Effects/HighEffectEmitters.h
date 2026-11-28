#pragma once

#include "Core/Expected.h"
#include "Particles/ParticleOwner.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// Lo que piden las altas y los pasos de los tipos altos (HighModelEffects y sus familias): el joint (0x530860), la
// partícula (0x53fe70) y el sprite (0x5491e0), con la dirección del que los pide en el error.
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Effects
{
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Effects
{
struct HighEffectStepContext;

// 0x530860(tipo, punto, punto2, ángulos, subtipo, dueño, escala, clave, 0, 0, −1): el joint, con la dirección del que
// lo pide delante del error de un joint que MuRet no tiene (el lote de los joints de la CL11) y "(CL11)" detrás. La
// clave es −1 salvo en los que pasan otra (el 0x125, 0x51ce91 y 0x52777f, con 0).
[[nodiscard]] Core::Expected<void, std::string> CreateHighJoint(Kernel::WorldState& world, Crt::CrtRandom& random,
                                                                std::int32_t type, glm::vec3 first, glm::vec3 second,
                                                                glm::vec3 angles, std::int32_t subtype,
                                                                Effects::ModelEffect* owner, float scale,
                                                                std::uint32_t address, std::int32_t key = -1);

// 0x53fe70 de una partícula, con el error del pool como texto y la dirección del que la pide. Con el pool lleno,
// nullopt y sin rand(). Con `owner`, el dueño (+0x40) que el que la pide empuja.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateHighParticle(Kernel::WorldState& world, Crt::CrtRandom& random, float worldClock, std::int32_t type,
                   const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light, std::int32_t subtype,
                   float scale, std::uint32_t address, Particles::ParticleOwner owner = {});

// 0x5491e0 desde un paso: el sprite con la luz y la rotación, en el lugar libre (el dueño, que MuRet no guarda). Sin
// la tabla de sprites en los servicios, corta.
[[nodiscard]] Core::Expected<void, std::string> CreateHighSprite(const HighEffectStepContext& context,
                                                                 std::uint16_t texture, const glm::vec3& position,
                                                                 float scale, const glm::vec3& light, float rotation,
                                                                 std::uint32_t address);
} // namespace Client::Effects
