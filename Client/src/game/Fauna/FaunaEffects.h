#pragma once
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Models/Cloth.h"
#include "Assets/FaunaModels.h"
#include "Fauna/FaunaStep.h"
#include <cstddef>
#include <glm/ext/vector_float3.hpp>
#include <optional>
#include <span>
#include <string>
namespace Client::Scenes
{
struct JointState;
}
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
// 518cf0(c2,pos,rot0,light1,3,null,-1,0,0,0,0), sin RNG.
[[nodiscard]] std::optional<std::size_t> CreateFaunaLightning(Effects::ModelEffects& effects,
                                                              const glm::vec3& position);
[[nodiscard]] Core::Expected<void, std::string>
StepFaunaLightning(Effects::ModelEffect& effect, std::size_t physicalIndex, const Models::Skeleton* skeleton,
                   Particles::Particles& particles, Particles::FrameSprites& sprites,
                   Terrain::TerrainLighting& lighting, Models::ClothWorld& cloth);
// Préstamos durables; load contiene referencias válidas hasta terminar el paso.
[[nodiscard]] FaunaStepEffects BindFaunaStepEffects(Assets::FaunaModels& models, Assets::FaunaModelLoadContext load,
                                                    std::span<Scenes::JointState> joints,
                                                    Effects::ModelEffects& effects, Crt::CrtRandom& random);
} // namespace Client::Fauna
