#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Pose.h"
#include "Scenes/ModelPoseCallbacks.h"
#include "World/ModelEffects.h"
#include "World/SphereFrustum.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
class Leaves;
class FrameSprites;
struct TerrainLighting;
} // namespace Client::World
namespace Client::Data::Terrain
{
struct TerrainHeights;
}

// Contratos estáticos de6374fe..637572. Las familias incompletas conservan su prefijo y devuelven
// la primera dirección no portada; una tabla vacía no acredita su camino vivo.
namespace Client::Scenes
{
class IModelLibrary;
struct DrawnObject;
struct FloorItemState
{
    bool alive = false;
    bool seen = false;
    std::int16_t type = 0;
    glm::vec3 position{};
    glm::vec3 angles{};
    float scale = 0;
    float alpha = 0;
    bool vertexLight = false;
    glm::vec3 light{};
    Render::Models::AnimationState animation{};
    Render::Models::ObjectState objectState{};
    std::int32_t value = 0;
    std::uint8_t excellent = 0;
    std::uint8_t ancient = 0;
    std::int16_t screenX = 0;
    std::int16_t screenY = 0;
    std::int32_t effectCounter = 0; // +4;608780 usa el valor anterior, luego suma uno.
    float dropSpeed = 0;            // +e4;608880 resta6 incluso después de tocar el suelo.
};

struct FloorItemDrawContext
{
    Render::ModelBatch& batch;
    const IModelLibrary& models;
    const World::SphereFrustum& frustum;
    const World::TerrainLighting* lighting = nullptr;
    Core::Duration frameClock{};
    std::int32_t map = 0;
    // 6205b0; ausencia corta después del dibujo sin inventar coordenadas.
    std::function<Core::Expected<glm::ivec2, std::string>(const glm::vec3&)> project{};
    TailPoseObserver onPose{};
    TailMeshProvider posedMeshes{};
};

struct JointState
{
    bool alive = false;
    std::int32_t count = 0;
    std::uint8_t drawEnabled = 0; // +11: bits1/2 eligen las dos caras del segmento
    std::uint8_t category = 0;
    std::uint8_t blend = 0;
    std::int32_t type = 0;
    std::int32_t subtype = 0;
    std::uint16_t texture = 0;
    glm::vec3 light{};
    std::int32_t maximumCount = 0;                     // +6c, divisor de las UV
    bool repeatedTexture = false;                      // +9f0
    std::uint8_t orientation = 0;                      // +9f1
    std::array<std::array<glm::vec3, 4>, 50> points{}; // +70, cuatro puntos por tramo
    float width = 0;
    glm::vec3 position{};
    glm::vec3 angles{};
    glm::vec3 targetPosition{};
    void* ownerContext = nullptr;
    std::int32_t life = 0;
    std::uint8_t field9d4 = 0;
    float speed = 0;
    glm::vec3 velocity{};
    std::int16_t key = 0;
    std::uint8_t skill = 0;
    std::uint8_t skillSerial = 0;
    bool tailEnabled = false;
    std::int32_t field9ec = 0;
    // D-C4: dos giros del último dibujo original; no forman parte del registro nativo.
    std::optional<std::array<float, 2>> icarusRotations{};
};

struct JointDrawResources
{
    Render::Models::CrtRandom& random;
    World::FrameSprites& sprites;
    bool originalFrame = true;
};

inline constexpr std::size_t RibbonPointCount = 30;
struct RibbonState
{
    bool alive = false;
    std::int32_t type = 0;
    // Lectura viva del +1d6 del dueño durable; nunca una vista temporal.
    const std::uint16_t* ownerEquipment = nullptr;
    std::int32_t count = 0;
    glm::vec3 light{};
    std::array<glm::vec3, RibbonPointCount> first{};
    std::array<glm::vec3, RibbonPointCount> second{};
};

struct DamageNumberState
{
    bool alive = false;
    std::int32_t number = 0;
    float scale = 0.0f; // +10, tamaño del atlas; speed*.4 es su alpha
    glm::vec3 position{};
    glm::vec3 light{};
    std::int32_t timer = 0;
    bool rise = false;
    float speed = 0.0f;
};

struct GameDrawTailState
{
    std::array<FloorItemState, 1000> floorItems{};
    std::array<JointState, 500> joints{};
    std::array<RibbonState, 100> ribbons{};
    std::array<DamageNumberState, 100> damage{};
};

// 0x548f40: descenso de vida/velocidad; no consume RNG ni borra el resto del registro al morir.
void StepDamage(std::span<DamageNumberState> damage);

[[nodiscard]] Core::Expected<void, std::string> DrawFloorItems(std::span<FloorItemState> items,
                                                               const FloorItemDrawContext& context);

[[nodiscard]] Core::Expected<void, std::string> DrawFloorItems(std::span<FloorItemState> items,
                                                               const World::SphereFrustum& frustum);
[[nodiscard]] Core::Expected<void, std::string> DrawJoints(Render::ModelBatch& batch, std::span<JointState> joints,
                                                           std::uint8_t mode);
[[nodiscard]] Core::Expected<void, std::string> DrawJoints(Render::ModelBatch& batch, std::span<JointState> joints,
                                                           std::uint8_t mode, const JointDrawResources& resources);
[[nodiscard]] Core::Expected<void, std::string> DrawRibbons(Render::ModelBatch& batch,
                                                            std::span<const RibbonState> ribbons);
[[nodiscard]] Core::Expected<void, std::string>
DrawDamage(Render::ModelBatch& batch, std::span<const DamageNumberState> damage, std::uint8_t mode);
[[nodiscard]] Core::Expected<void, std::string> DrawDamage(Render::ModelBatch& batch,
                                                           std::span<const DamageNumberState> damage, std::uint8_t mode,
                                                           const Render::Models::OriginalMatrix& view);
[[nodiscard]] Core::Expected<void, std::string> DrawGameLeaves(Render::ModelBatch& batch, const World::Leaves& leaves,
                                                               std::int32_t map,
                                                               const Render::Models::OriginalMatrix& view,
                                                               std::int32_t weatherState = 0);
[[nodiscard]] Core::Expected<void, std::string>
DrawGameModelEffects(Render::ModelBatch& batch, const IModelLibrary& models, std::span<World::ModelEffect> effects,
                     const World::SphereFrustum& frustum, const World::TerrainLighting* lighting,
                     Core::Duration frameClock, float alpha, std::uint8_t mode = 0);
struct ModelEffectDrawResources
{
    const Data::Terrain::TerrainHeights& heights;
    float worldClock = 0;
    TailPoseObserver onPose{};
    TailMeshProvider posedMeshes{};
};
[[nodiscard]] Core::Expected<void, std::string>
DrawGameModelEffects(Render::ModelBatch& batch, const IModelLibrary& models, std::span<World::ModelEffect> effects,
                     const World::SphereFrustum& frustum, const World::TerrainLighting* lighting,
                     Core::Duration frameClock, float alpha, const ModelEffectDrawResources& resources,
                     std::uint8_t mode = 0);
} // namespace Client::Scenes
