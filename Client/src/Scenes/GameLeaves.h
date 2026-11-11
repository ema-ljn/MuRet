#pragma once

#include "Core/Expected.h"
#include "World/TerrainHeight.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::GameLogic
{
struct MovementSpeedSharedState;
}
namespace Client::World
{
class Leaves;
class Particles;
class ILeafWaveEvaluator;
struct LeafWeather;
} // namespace Client::World

// 637082 → 530490: tablas y globales prestados del mundo durable, antes de aves y peces.
namespace Client::Scenes
{
struct ActorState;
struct GameLeafContext
{
    World::Leaves& leaves;
    World::LeafWeather& weather;
    const Data::Terrain::TerrainHeights& heights;
    const Data::Terrain::TerrainAttributes& attributes;
    const glm::vec3& hero;
    const float& cameraY;
    const std::int32_t& map;
    const std::int32_t& heroCellLayer;
    Render::Models::CrtRandom& random;
    const float& worldClock;
    float fixedHeight = World::FixedTerrainHeight;
    World::Particles* particles = nullptr;
    // Con nullptr usa el perfil retenido de ADR0014, no std::sin.
    World::ILeafWaveEvaluator* waveEvaluator = nullptr;
    // 0x5763358, el clima de Crywolf (WorldRuntime::crywolf.weather), que sortea 0x430510.
    const std::int32_t* crywolfState = nullptr;
    // Sólo los creadores de nubes 443990/435600: Movement/ángulos y el estado real de 508940.
    ActorState* cloudHero = nullptr;
    GameLogic::MovementSpeedSharedState* movementSpeed = nullptr;
};

// Conserva el prefijo ejecutado si falta un binding; nunca continúa con efectos omitidos.
[[nodiscard]] Core::Expected<void, std::string> StepGameLeaves(GameLeafContext& context);
} // namespace Client::Scenes
