#pragma once

#include "Core/Expected.h"
#include "Scenes/FaunaState.h"
#include "Scenes/FaunaObject.h"
#include "World/IFishStepServices.h"
#include "World/IBirdStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <system_error>

namespace Client::Render::Models
{
struct ClothWorld;
struct Skeleton;
} // namespace Client::Render::Models
namespace Client::World
{
struct WorldTerrain;
struct TerrainLighting;
} // namespace Client::World
namespace Client::Scenes
{
class IModelLibrary;
// Los servicios opcionales fallan al alcanzarse: no simulan efectos con un éxito vacío.
struct FaunaStepEffects
{
    std::function<Core::Expected<std::uint32_t, std::error_code>(const World::FishEffectRequest&)> effect;
    std::function<std::error_code(const glm::vec3&)> lightning;
    std::function<std::error_code(std::int32_t)> prepare;
};
struct FaunaStepContext
{
    FaunaState& fauna;
    const std::int32_t& map;
    const std::int32_t& storm;
    const glm::vec3& heroPosition;
    const std::uint8_t& heroAction;
    const std::uint8_t& heroFlag;
    const World::WorldTerrain& terrain;
    World::TerrainLighting& lighting;
    const IModelLibrary& models;
    Render::Models::ClothWorld& cloth;
    float worldTime;
    FaunaStepEffects effects{};
    // Sólo SetAction con tipo firmado -1: BMD construido previo a la base si su desplazamiento>0.
    // La ausencia mantiene la frontera de memoria; el binding no fabrica esa precondición.
    const Render::Models::Skeleton* prefixModel = nullptr;
    float fixedHeight = 1200;
};
// 6370ea/6370ef. Una sola CRT; las vistas apuntan a los slots físicos del dueño.
[[nodiscard]] Core::Expected<void, std::string> StepGameBirds(FaunaStepContext& context);
[[nodiscard]] Core::Expected<void, std::string> StepGameFish(FaunaStepContext& context);
[[nodiscard]] Core::Expected<void, std::string> StepGameFauna(FaunaStepContext& context);
// Dispatch productivo de IBirdStepServices::Call; expuesto también para comprobar cada handler con ASM.
[[nodiscard]] World::BirdCallResult RunFaunaBirdHandler(FaunaStepContext& context, World::BirdHandler handler,
                                                        FaunaObject& bird, std::int32_t index);
} // namespace Client::Scenes
