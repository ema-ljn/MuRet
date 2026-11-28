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

// 637082 → 530490: tablas y globales prestados del mundo durable, antes de aves y peces.
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
    // Con nullptr usa el perfil retenido de ADR0014, no std::sin.
    Weather::ILeafWaveEvaluator* waveEvaluator = nullptr;
    // 0x5763358, el clima de Crywolf (WorldRuntime::crywolf.weather), que sortea 0x430510.
    const std::int32_t* crywolfState = nullptr;
    // Sólo los creadores de nubes 443990/435600: Movement/ángulos y el estado real de 508940.
    Kernel::ActorState* cloudHero = nullptr;
    Actors::MovementSpeedSharedState* movementSpeed = nullptr;
};

// Conserva el prefijo ejecutado si falta un binding; nunca continúa con efectos omitidos.
[[nodiscard]] Core::Expected<void, std::string> StepGameLeaves(GameLeafContext& context);
} // namespace Client::Weather
