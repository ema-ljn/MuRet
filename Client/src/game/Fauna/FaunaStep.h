#pragma once

#include "Core/Expected.h"
#include "Fauna/FaunaState.h"
#include "Fauna/FaunaObject.h"
#include "Kernel/JointRequest.h"
#include "Fauna/IBirdStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <system_error>

namespace Client::Models
{
struct ClothWorld;
class IModelLibrary;
struct Skeleton;
} // namespace Client::Models
namespace Client::Terrain
{
struct WorldTerrain;
struct TerrainLighting;
} // namespace Client::Terrain
namespace Client::Kernel
{
struct WorldRuntime;
} // namespace Client::Kernel

namespace Client::Fauna
{
// Los servicios opcionales fallan al alcanzarse: no simulan efectos con un éxito vacío.
struct FaunaStepEffects
{
    std::function<Core::Expected<std::uint32_t, std::error_code>(const Kernel::JointRequest&)> effect;
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
    const Terrain::WorldTerrain& terrain;
    Terrain::TerrainLighting& lighting;
    const Models::IModelLibrary& models;
    Models::ClothWorld& cloth;
    float worldTime;
    FaunaStepEffects effects{};
    // Sólo SetAction con tipo firmado -1: BMD construido previo a la base si su desplazamiento>0.
    // La ausencia mantiene la frontera de memoria; el binding no fabrica esa precondición.
    const Models::Skeleton* prefixModel = nullptr;
    float fixedHeight = 1200;
    Kernel::WorldRuntime* runtime = nullptr;
};
// 6370ea/6370ef. Una sola CRT; las vistas apuntan a los slots físicos del dueño.
[[nodiscard]] Core::Expected<void, std::string> StepGameBirds(FaunaStepContext& context);
[[nodiscard]] Core::Expected<void, std::string> StepGameFish(FaunaStepContext& context);
[[nodiscard]] Core::Expected<void, std::string> StepGameFauna(FaunaStepContext& context);
// Dispatch productivo de IBirdStepServices::Call; expuesto también para comprobar cada handler con ASM.
[[nodiscard]] Fauna::BirdCallResult RunFaunaBirdHandler(FaunaStepContext& context, Fauna::BirdHandler handler,
                                                        FaunaObject& bird, std::int32_t index);
} // namespace Client::Fauna
