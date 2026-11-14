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
class ITerrainAttributeQuerySource;
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
struct ItemDrawContext;
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
    TailPoseObserver onSkin{};
    std::span<const std::int32_t> randomTable{};
    float worldClock = 0;
    std::function<Core::Expected<float, std::string>(float, float)> waterHeight{};
    std::uint8_t glowDetail = 4;
    const ItemDrawContext* itemContext = nullptr;
};

// Qué hay detrás de ownerContext. Casi siempre lo dice el tipo del joint (Scenes/FaunaJointCreation.h): un
// FaunaObject, el ActorState del personaje o un dueño opaco. El 60f/0 que el caballo oscuro suelta a su alrededor
// (0x444740) lleva en cambio el HelperState del caballo, y los de las órdenes del cuervo (0x418390 y 0x418400, el A9 de
// Scenes/PetStats) el DarkSpirit del cuervo. En el original todos son el objeto del dueño y el joint lee los mismos
// offsets: la luz (+0xf4) al nacer y la acción (+0x111) en el paso.
enum class JointOwnerKind : std::uint8_t
{
    ByType,
    Helper,
    Raven,
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
    // Propio de MuRet: cómo leer ownerContext. 0x530860 lo escribe en cada alta.
    JointOwnerKind ownerKind = JointOwnerKind::ByType;
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
    glm::vec3 pathIncrement{}; // +24;531851/5332fa guardan el delta de las cintas Helper07.
};

struct JointDrawResources
{
    Render::Models::CrtRandom& random;
    World::FrameSprites& sprites;
    bool originalFrame = true;
    float worldClock = 0; // 53c8ee: ftol firmado, resto1000 y scroll de UV.
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
    const void* ownerContext = nullptr; // 52e470 compara el dueño antes de buscar un lugar libre.
    std::int32_t key = 0;               // +2f0; sólo discrimina cuando el pedido trae key>0.
    std::int32_t life = 0;              // +8; 30 o15 al crear, no se renueva al reutilizar.
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
struct GameObjectPass;
struct ModelEffectDrawResources
{
    const Data::Terrain::TerrainHeights& heights;
    float worldClock = 0;
    TailPoseObserver onPose{};
    // 0x4eb860 con Translate y padre en 0 (0x52d33f, 0x52d4f1 y 0x52d7d2): los ítems de 0xf4, 0xfa y 0x10f dejan
    // sus huesos en el buffer global sin la escala ni el origen del efecto (Q-C411). Comparte el scratch de
    // posedMeshes, con los vértices ya en el mundo.
    TailPoseObserver onUntranslatedPose{};
    TailMeshProvider posedMeshes{};
    World::FrameSprites* sprites = nullptr;
    World::TerrainLighting* dynamicLighting = nullptr;
    World::ITerrainAttributeQuerySource* attributes = nullptr;
    bool siegeActive = false;                    // 4294b0 lee7239ec; no es un test de mapa.
    Render::Models::CrtRandom* random = nullptr; // 607b60 sólo sortea los casos que lo necesitan.
    const ItemDrawContext* itemContext = nullptr;
    const GameObjectPass* kalimaPass = nullptr;
};
[[nodiscard]] Core::Expected<void, std::string>
DrawGameModelEffects(Render::ModelBatch& batch, const IModelLibrary& models, std::span<World::ModelEffect> effects,
                     const World::SphereFrustum& frustum, const World::TerrainLighting* lighting,
                     Core::Duration frameClock, float alpha, const ModelEffectDrawResources& resources,
                     std::uint8_t mode = 0);
} // namespace Client::Scenes
