#pragma once

#include "Core/Expected.h"
#include "Terrain/TerrainHeight.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Actors
{
struct MovementSpeedSharedState;
}
namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

namespace Client::Weather
{
class Leaves;
class ILeafWaveEvaluator;
struct LeafWeather;
} // namespace Client::Weather

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// 0x637082 → 0x530490: tables and globals borrowed from the durable world, before birds and fish.
namespace Client::Weather
{
struct GameLeafContext
{
    Weather::Leaves& leaves;
    Weather::LeafWeather& weather;
    const Data::Terrain::TerrainHeights& heights;
    const Data::Terrain::TerrainAttributes& attributes;
    const glm::vec3& hero;
    const float& cameraY;
    const std::int32_t& map;
    const std::int32_t& heroCellLayer;
    Crt::CrtRandom& random;
    const float& worldClock;
    float fixedHeight = Terrain::FixedTerrainHeight;
    Particles::Particles* particles = nullptr;
    // With nullptr it uses ADR0014's retained profile, not std::sin.
    Weather::ILeafWaveEvaluator* waveEvaluator = nullptr;
    // 0x5763358, Crywolf's weather (WorldRuntime::crywolf.weather), which 0x430510 rolls.
    const std::int32_t* crywolfState = nullptr;
    // Only the cloud creators 0x443990/0x435600: Movement/angles and the real state of 0x508940.
    Kernel::ActorState* cloudHero = nullptr;
    Actors::MovementSpeedSharedState* movementSpeed = nullptr;
};

// Keeps the executed prefix if a binding is missing; never goes on with skipped effects.
[[nodiscard]] Core::Expected<void, std::string> StepGameLeaves(GameLeafContext& context);
} // namespace Client::Weather
