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
// The optional services fail when reached: they do not simulate effects with an empty success.
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
    // Only SetAction with signed type -1: a BMD built before the base if its offset>0. The absence keeps the memory
    // boundary; the binding does not fabricate that precondition.
    const Models::Skeleton* prefixModel = nullptr;
    float fixedHeight = 1200;
    Kernel::WorldRuntime* runtime = nullptr;
};
// 0x6370ea/0x6370ef. A single CRT; the views point to the owner's physical slots.
[[nodiscard]] Core::Expected<void, std::string> StepGameBirds(FaunaStepContext& context);
[[nodiscard]] Core::Expected<void, std::string> StepGameFish(FaunaStepContext& context);
[[nodiscard]] Core::Expected<void, std::string> StepGameFauna(FaunaStepContext& context);
// Production dispatch of IBirdStepServices::Call; also exposed to check each handler against the ASM.
[[nodiscard]] Fauna::BirdCallResult RunFaunaBirdHandler(FaunaStepContext& context, Fauna::BirdHandler handler,
                                                        FaunaObject& bird, std::int32_t index);
} // namespace Client::Fauna
