#pragma once

#include "Core/Expected.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Kernel
{
struct ModelEffectArguments;
struct WorldObject;
} // namespace Client::Kernel

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
struct Skeleton;
} // namespace Client::Models

namespace Client::Terrain
{
struct TerrainLighting;
} // namespace Client::Terrain

namespace Client::Effects
{
class ModelEffects;
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

namespace Client::CastleSiege
{
// 0x429ec0: original sector and first valid object in insertion order; the fifth native argument is not read.
[[nodiscard]] Core::Expected<bool, std::string> CollideTheSiegeObject(Effects::ModelEffect& projectile,
                                                                      std::span<Kernel::WorldObject> objects,
                                                                      float radius, float heightLimit, bool markOnly);

struct SiegeStoneStepServices
{
    std::function<double(float, float)> terrainHeight{};
    Effects::ModelEffects* effects = nullptr;
    Terrain::TerrainLighting* lighting = nullptr;
    const void* hero = nullptr;
    glm::vec3* cameraTarget = nullptr;
    float* cameraShake = nullptr;
    std::function<Core::Expected<bool, std::string>(Effects::ModelEffect&, float, float, bool)> collide{};
    std::function<Core::Expected<void, std::string>(const glm::vec3&, float)> heroAction{};
    std::function<Core::Expected<void, std::string>(const glm::vec3&, float)> actorsAction{};
    std::function<void(std::uint8_t, std::uint8_t)> notify{};
    std::function<Core::Expected<void, std::string>(float, float, std::int32_t, std::int32_t)> deform{};
    std::function<void(Audio::SoundId, const glm::vec3&)> sound{};
};
// 0x5208a0, 0x527c62..5290: native flight/contact and epilogue, without the generic 0x51fc80.
[[nodiscard]] Core::Expected<void, std::string> StepSiegeStone(Effects::ModelEffect& effect,
                                                               const Models::Skeleton* skeleton,
                                                               Particles::Particles& particles, Crt::CrtRandom& random,
                                                               const SiegeStoneStepServices& services);

inline constexpr std::int16_t SiegeStoneEffect = 0x12b;
inline constexpr std::int16_t SiegeIceStoneEffect = 0x12c;

[[nodiscard]] bool IsSiegeStoneEffect(std::int16_t type);
// 0x51cfa4 and 0x51d1f0, after the prefix 0x518cf0. Full is a success without an index and without CRT.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateSiegeStone(const Kernel::ModelEffectArguments& arguments, Effects::ModelEffects& effects,
                 const glm::vec3& heroPosition, Crt::CrtRandom& random);
} // namespace Client::CastleSiege
