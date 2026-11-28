#pragma once

#include "Core/Expected.h"
#include "Particles/ParticleOwner.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// What the high types' creations and steps ask for (HighModelEffects and its families): the joint (0x530860), the
// particle (0x53fe70) and the sprite (0x5491e0), with the requester's address in the error.
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

// 0x530860(type, point, point2, angles, subtype, owner, scale, key, 0, 0, −1): the joint, with the requester's address
// before the error of a joint MuRet lacks (CL11's joint batch) and "(CL11)" after it. The key is −1 except in those
// that pass another (the 0x125, 0x51ce91 and 0x52777f, with 0).
[[nodiscard]] Core::Expected<void, std::string> CreateHighJoint(Kernel::WorldState& world, Crt::CrtRandom& random,
                                                                std::int32_t type, glm::vec3 first, glm::vec3 second,
                                                                glm::vec3 angles, std::int32_t subtype,
                                                                Effects::ModelEffect* owner, float scale,
                                                                std::uint32_t address, std::int32_t key = -1);

// 0x53fe70 for a particle, with the pool's error as text and the requester's address. With the pool full, nullopt and
// without rand(). With `owner`, the owner (+0x40) that the requester pushes.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateHighParticle(Kernel::WorldState& world, Crt::CrtRandom& random, float worldClock, std::int32_t type,
                   const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light, std::int32_t subtype,
                   float scale, std::uint32_t address, Particles::ParticleOwner owner = {});

// 0x5491e0 from a step: the sprite with the light and the rotation, in the free slot (the owner, which MuRet does not
// keep). Without the sprite table in the services, it stops.
[[nodiscard]] Core::Expected<void, std::string> CreateHighSprite(const HighEffectStepContext& context,
                                                                 std::uint16_t texture, const glm::vec3& position,
                                                                 float scale, const glm::vec3& light, float rotation,
                                                                 std::uint32_t address);
} // namespace Client::Effects
