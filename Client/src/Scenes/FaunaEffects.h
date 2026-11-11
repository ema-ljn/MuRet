#pragma once
#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/FaunaStep.h"
#include <cstddef>
#include <glm/ext/vector_float3.hpp>
#include <optional>
#include <span>
#include <string>
namespace Client::Scenes
{
struct JointState;
}
namespace Client::World
{
class ModelEffects;
struct ModelEffect;
class Particles;
class FrameSprites;
struct TerrainLighting;
} // namespace Client::World
namespace Client::Render::Models
{
struct Skeleton;
}
namespace Client::Scenes
{
// 518cf0(c2,pos,rot0,light1,3,null,-1,0,0,0,0), sin RNG.
[[nodiscard]] std::optional<std::size_t> CreateFaunaLightning(World::ModelEffects& effects, const glm::vec3& position);
[[nodiscard]] Core::Expected<void, std::string>
StepFaunaLightning(World::ModelEffect& effect, std::size_t physicalIndex, const Render::Models::Skeleton* skeleton,
                   World::Particles& particles, World::FrameSprites& sprites, World::TerrainLighting& lighting,
                   Render::Models::ClothWorld& cloth);
// Préstamos durables; load contiene referencias válidas hasta terminar el paso.
[[nodiscard]] FaunaStepEffects BindFaunaStepEffects(FaunaModels& models, FaunaModelLoadContext load,
                                                    std::span<JointState> joints, World::ModelEffects& effects,
                                                    Render::Models::CrtRandom& random);
} // namespace Client::Scenes
