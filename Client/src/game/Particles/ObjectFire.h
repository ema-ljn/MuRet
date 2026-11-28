#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Kernel
{
struct ObjectStepGlobals;
struct WorldObject;
} // namespace Client::Kernel

// 0x52f360: the fire of a world's object, of the dungeon's torches and of the 0xf1 (0x505425).
namespace Client::Particles
{
// (intensity · 0.1, red · green, red · blue), the warm light of 0x52f360 and of the world's lamps.
[[nodiscard]] glm::vec3 WarmLight(std::int32_t intensity, float green, float blue);

// 0x52f360: class 0 flame/light, 1 smoke/0 and 2 smoke/2. The offset rotates without applying the scale.
// Keeps the RNG prefix and the earlier emissions if a borrowed destination is missing.
[[nodiscard]] Core::Expected<void, std::string> EmitObjectFire(std::int32_t fireClass,
                                                               const Kernel::WorldObject& object,
                                                               const glm::vec3& offset, Crt::CrtRandom& random,
                                                               const Kernel::ObjectStepGlobals& globals);
// 0x52f360 with an object other than a world one (the 0xf1 from 0x505425): what it reads from the object, the angles
// (+0x1c), the position (+0x10) and the light (+0xf4, the smoke's in classes 1 and 2). Uses only worldClock,
// dynamicLighting and particles from `globals`.
[[nodiscard]] Core::Expected<void, std::string> EmitObjectFire(std::int32_t fireClass, const glm::vec3& angles,
                                                               const glm::vec3& position, const glm::vec3& light,
                                                               const glm::vec3& offset, Crt::CrtRandom& random,
                                                               const Kernel::ObjectStepGlobals& globals);
} // namespace Client::Particles
