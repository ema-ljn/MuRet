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

// Contratos estáticos de6374fe..637572. Las familias incompletas conservan su prefijo y devuelven
// la primera dirección no portada; una tabla vacía no acredita su camino vivo.
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
    // 6205b0; ausencia corta después del dibujo sin inventar coordenadas.
    std::function<Core::Expected<glm::ivec2, std::string>(const glm::vec3&)> project{};
    Actors::Drawing::TailPoseObserver onPose{};
    Actors::Drawing::TailMeshProvider posedMeshes{};
    Actors::Drawing::TailPoseObserver onSkin{};
    std::span<const std::int32_t> randomTable{};
    float worldClock = 0;
    std::function<Core::Expected<float, std::string>(float, float)> waterHeight{};
    const Actors::Drawing::ItemDrawContext* itemContext = nullptr;
};

// 0x548f40: descenso de vida/velocidad; no consume RNG ni borra el resto del registro al morir.
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
    // 0x4eb860 con Translate y padre en 0 (0x52d33f, 0x52d4f1 y 0x52d7d2): los ítems de 0xf4, 0xfa y 0x10f dejan
    // sus huesos en el buffer global sin la escala ni el origen del efecto (Q-C411). Comparte el scratch de
    // posedMeshes, con los vértices ya en el mundo.
    Actors::Drawing::TailPoseObserver onUntranslatedPose{};
    Actors::Drawing::TailMeshProvider posedMeshes{};
    Particles::FrameSprites* sprites = nullptr;
    Terrain::TerrainLighting* dynamicLighting = nullptr;
    Terrain::ITerrainAttributeQuerySource* attributes = nullptr;
    bool siegeActive = false;         // 4294b0 lee7239ec; no es un test de mapa.
    Crt::CrtRandom* random = nullptr; // 607b60 sólo sortea los casos que lo necesitan.
    const Actors::Drawing::ItemDrawContext* itemContext = nullptr;
    const Kernel::GameObjectPass* kalimaPass = nullptr;
    // 0x5ffdff..0x5ffe24: +0x1a1 del héroe (0x743a170), que 0x5ffdf0 lee en los mapas 18 a 23 (el de `kalimaPass`), y
    // si el dibujo es de un frame del original: sólo entonces la resta de 0,15f queda guardada en el alpha del efecto.
    std::uint8_t heroByte1a1 = 0;
    bool originalFrame = false;
    // 42cfcb del fragmento3d..40: el mismo pool518cf0 al que emite4ee770.
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
