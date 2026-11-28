#pragma once
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Models/Cloth.h"
#include "Assets/FaunaModels.h"
#include "Fauna/FaunaStep.h"
#include <cstddef>
#include <glm/ext/vector_float3.hpp>
#include <optional>
#include <string>
namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Terrain
{
struct TerrainLighting;
} // namespace Client::Terrain

namespace Client::Particles
{
class Particles;
class FrameSprites;
} // namespace Client::Particles

namespace Client::Effects
{
class ModelEffects;
struct ModelEffect;
} // namespace Client::Effects
namespace Client::Models
{
struct Skeleton;
}
namespace Client::Fauna
{
// 0x518cf0(0xc2, position, rotation 0, light 1, 3, null, -1, 0, 0, 0, 0), without consuming RNG.
[[nodiscard]] std::optional<std::size_t> CreateFaunaLightning(Effects::ModelEffects& effects,
                                                              const glm::vec3& position);
[[nodiscard]] Core::Expected<void, std::string>
StepFaunaLightning(Effects::ModelEffect& effect, std::size_t physicalIndex, const Models::Skeleton* skeleton,
                   Particles::Particles& particles, Particles::FrameSprites& sprites,
                   Terrain::TerrainLighting& lighting, Models::ClothWorld& cloth);
// Durable borrows; load holds valid references until the step ends.
[[nodiscard]] FaunaStepEffects BindFaunaStepEffects(Assets::FaunaModels& models, Assets::FaunaModelLoadContext load,
                                                    const Kernel::WorldState& world, Effects::ModelEffects& effects,
                                                    Crt::CrtRandom& random);
} // namespace Client::Fauna
