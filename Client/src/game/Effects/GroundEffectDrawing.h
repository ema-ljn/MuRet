#pragma once

#include "Core/Expected.h"
#include "Effects/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Data::Terrain
{
struct TerrainHeights;
}
namespace Client::Render
{
class ModelBatch;
}
namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class KalimaWater;
} // namespace Client::Kernel

// The cases of 0x52dc20 after the alive/seen filter, the common rand() and the additive blend. Static contract: 06 §
// Después de los objetos, P-C55 (b).
namespace Client::Effects
{

struct GroundEffectResources
{
    const Data::Terrain::TerrainHeights& heights;
    Crt::CrtRandom& random;
    float worldClock = 0.0f;
    std::uint8_t map = 0;
    const Kernel::KalimaWater* kalima = nullptr;
    // The table the pass walks: the drawing of 0x61e/6 and /8 creates the /7 and /9 there (0x52e04c, 0x52e0a2).
    std::span<Effects::ModelEffect> table{};
    // The position (+0x10) of an effect's owner (+0x108), read live: that of 0x61e/7 (0x52ded0). nullopt if the owner
    // is not a character.
    std::function<std::optional<glm::vec3>(const void*)> ownerPosition{};
};

// 0x620a90 before the case: publishes the additive profile without binding a texture.
void ApplyGroundEffectAdditive(Render::ModelBatch& batch);

// Keeps the order of the decals and the mutations the drawing makes. It does not check alive/seen again or consume
// the rand() of 0x52dc46: those belong to the table walk.
[[nodiscard]] Core::Expected<void, std::string>
DrawGroundEffectCase(Render::ModelBatch& batch, Effects::ModelEffect& effect, const GroundEffectResources& resources);
} // namespace Client::Effects
