#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Math/OriginalRotation.h"
#include "Math/HalfLifeMath.h"
#include "Actors/Drawing/ModelPoseCallbacks.h"
#include "Kernel/DrawTailState.h"
#include "Effects/ModelEffects.h"
#include "Terrain/SphereFrustum.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int2.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace Client::Kernel
{
struct GameObjectPass;
} // namespace Client::Kernel

namespace Client::Render
{
class ModelBatch;
}
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
class IModelLibrary;
} // namespace Client::Models
namespace Client::Terrain
{
class ITerrainAttributeQuerySource;
struct TerrainLighting;
} // namespace Client::Terrain

namespace Client::Weather
{
class Leaves;
} // namespace Client::Weather

namespace Client::Particles
{
class Particles;
class FrameSprites;
} // namespace Client::Particles
namespace Client::Data::Terrain
{
struct TerrainHeights;
}

namespace Client::Actors::Drawing
{
struct ItemDrawContext;
struct DrawnObject;
} // namespace Client::Actors::Drawing

// Static contracts of 0x6374fe..0x637572. The incomplete families keep their prefix and return the first unported
// address; an empty table does not vouch for its live path.
namespace Client::Scenes
{
struct FloorItemDrawContext
{
    Render::ModelBatch& batch;
    const Models::IModelLibrary& models;
    const Terrain::SphereFrustum& frustum;
    const Terrain::TerrainLighting* lighting = nullptr;
    Core::Duration frameClock{};
    std::int32_t map = 0;
    // 0x6205b0; its absence stops after the draw without inventing coordinates.
    std::function<Core::Expected<glm::ivec2, std::string>(const glm::vec3&)> project{};
    Actors::Drawing::TailPoseObserver onPose{};
    Actors::Drawing::TailMeshProvider posedMeshes{};
    Actors::Drawing::TailPoseObserver onSkin{};
    std::span<const std::int32_t> randomTable{};
    float worldClock = 0;
    std::function<Core::Expected<float, std::string>(float, float)> waterHeight{};
    const Actors::Drawing::ItemDrawContext* itemContext = nullptr;
};

// 0x548f40: life/speed decrease; it does not consume RNG or clear the rest of the record on dying.
void StepDamage(std::span<Kernel::DamageNumberState> damage);

[[nodiscard]] Core::Expected<void, std::string> DrawFloorItems(std::span<Kernel::FloorItemState> items,
                                                               const FloorItemDrawContext& context);

[[nodiscard]] Core::Expected<void, std::string> DrawFloorItems(std::span<Kernel::FloorItemState> items,
                                                               const Terrain::SphereFrustum& frustum);
[[nodiscard]] Core::Expected<void, std::string> DrawRibbons(Render::ModelBatch& batch,
                                                            std::span<const Kernel::RibbonState> ribbons);
[[nodiscard]] Core::Expected<void, std::string>
DrawDamage(Render::ModelBatch& batch, std::span<const Kernel::DamageNumberState> damage, std::uint8_t mode);
[[nodiscard]] Core::Expected<void, std::string> DrawDamage(Render::ModelBatch& batch,
                                                           std::span<const Kernel::DamageNumberState> damage,
                                                           std::uint8_t mode, const Math::OriginalMatrix& view);
[[nodiscard]] Core::Expected<void, std::string> DrawGameLeaves(Render::ModelBatch& batch, const Weather::Leaves& leaves,
                                                               std::int32_t map, const Math::OriginalMatrix& view,
                                                               std::int32_t weatherState = 0);
[[nodiscard]] Core::Expected<void, std::string>
DrawGameModelEffects(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                     std::span<Effects::ModelEffect> effects, const Terrain::SphereFrustum& frustum,
                     const Terrain::TerrainLighting* lighting, Core::Duration frameClock, float alpha,
                     std::uint8_t mode = 0);
struct ModelEffectDrawResources
{
    const Data::Terrain::TerrainHeights& heights;
    float worldClock = 0;
    Actors::Drawing::TailPoseObserver onPose{};
    // 0x4eb860 with Translate and parent at 0 (0x52d33f, 0x52d4f1 and 0x52d7d2): the items of 0xf4, 0xfa and 0x10f
    // leave their bones in the global buffer without the effect's scale or origin (Q-C411). It shares posedMeshes'
    // scratch, with the vertices already in the world.
    Actors::Drawing::TailPoseObserver onUntranslatedPose{};
    Actors::Drawing::TailMeshProvider posedMeshes{};
    Particles::FrameSprites* sprites = nullptr;
    Terrain::TerrainLighting* dynamicLighting = nullptr;
    Terrain::ITerrainAttributeQuerySource* attributes = nullptr;
    bool siegeActive = false;         // 0x4294b0 reads 0x7239ec; it is not a map test.
    Crt::CrtRandom* random = nullptr; // 0x607b60 only draws for the cases that need it.
    const Actors::Drawing::ItemDrawContext* itemContext = nullptr;
    const Kernel::GameObjectPass* kalimaPass = nullptr;
    // 0x5ffdff..0x5ffe24: the hero's +0x1a1 (0x743a170), which 0x5ffdf0 reads on maps 18 to 23 (`kalimaPass`'s), and
    // whether the draw is of an original frame: only then does the 0.15f subtraction stay stored in the effect's alpha.
    std::uint8_t heroByte1a1 = 0;
    bool originalFrame = false;
    // 0x42cfcb of fragment 3d..40: the same pool 0x518cf0 that 0x4ee770 emits to.
    Effects::ModelEffects* effects = nullptr;
    Particles::Particles* particles = nullptr;
    std::function<std::span<const Math::BoneTransform>()> posedBones{};
};
[[nodiscard]] Core::Expected<void, std::string>
DrawGameModelEffects(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                     std::span<Effects::ModelEffect> effects, const Terrain::SphereFrustum& frustum,
                     const Terrain::TerrainLighting* lighting, Core::Duration frameClock, float alpha,
                     const ModelEffectDrawResources& resources, std::uint8_t mode = 0);
} // namespace Client::Scenes
