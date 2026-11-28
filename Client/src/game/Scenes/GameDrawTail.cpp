#include "Kernel/DrawTailState.h"
#include "Kernel/ObjectPass.h"
#include "Scenes/GameDrawTail.h"
#include "Maps/MapId.h"
#include "Math/X87.h"
#include "Models/IModelLibrary.h"
#include "Actors/Drawing/VertexEffects.h"
#include "Effects/CrywolfMissileDrawing.h"
#include "MapScripts/Kalima/KalimaObjectServices.h"
#include "Kernel/KalimaWater.h"
#include "Kernel/WorldObject.h"
#include "Events/ChaosCastle/ChaosCastle.h"

#include "Core/IClock.h"
#include "Models/ObjectState.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Effects/CastleWeaponBodies.h"
#include "Tables/PlayerModels.h"
#include "Render/Billboard.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Math/OriginalRotation.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Items/GameFloorItemDrawing.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"
#include "Scenes/GameTailDrawingPrimitives.h"
#include "Effects/HighEffectDrawing.h"
#include "Actors/Drawing/ModelPoseCallbacks.h"
#include "Effects/MeteorEffects.h"
#include "Effects/SkinShells.h"
#include "Effects/WizardEffectDrawing.h"
#include "Effects/GameModelEffects.h"
#include "Weather/Leaves.h"
#include "Effects/ModelEffects.h"
#include "Effects/SkinShellEffects.h"
#include "Particles/Sprites.h"
#include "Terrain/SphereFrustum.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/TerrainDecal.h"
#include "Terrain/TerrainAttributeQuery.h"
#include "Data/Terrain/TerrainLight.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
using Math::F32;
using TailDrawing::Blend;
using TailDrawing::Quad;
bool AdditiveLeaves(std::int32_t map)
{
    return map == 2 || map == 7 || map == 10 || map == 30 || map == 55;
}
void LeafInWorld(Render::ModelBatch& batch, const Weather::Leaf& leaf, float a, float b)
{
    const auto turn = Math::OriginalAngleMatrix(leaf.angles);
    const std::array<glm::vec3, 4> local{{{-a, -a, -b}, {a, a, -b}, {a, a, b}, {-a, -a, b}}};
    const std::array<glm::vec2, 4> uv{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};
    std::array<Render::ModelVertex, 4> vertices{};
    for (std::size_t i = 0; i < vertices.size(); ++i)
        vertices[i] = {leaf.position + Math::OriginalVectorTransform(local[i], turn), uv[i], {1, 1, 1, 1}};
    Quad(batch, vertices);
}
bool GenericEffect(std::int16_t type)
{
    if (type >= 0xbe && type < 0x151)
        return true;
    return type == 9 || (type >= 0x3d && type <= 0x40) || type == 0xae || type == 0xb3 || type == 0xb4 ||
           type == 0xbb || type == 0x192 || type == 0xc1a;
}
bool SpecialEffect(const Effects::ModelEffect& effect)
{
    const auto type = effect.type;
    return (type == 0xcf && effect.subtype >= 3 && effect.subtype <= 7) || type == 0xf4 || type == 0xfa ||
           type == 0x10f || type == 0x135 || type == 0x14e || (type >= 0x5e8 && type <= 0x5ea);
}
bool OmittedEffect(const Effects::ModelEffect& effect)
{
    return effect.type == 0xf3 || effect.type == 0xf8 || (effect.type == 0x105 && effect.subtype == 3) ||
           (effect.type == 0x115 && effect.subtype == 1);
}
} // namespace
void StepDamage(std::span<Kernel::DamageNumberState> damage)
{
    for (auto& number : damage)
    {
        if (!number.alive)
            continue;
        number.timer = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(number.timer) - 1u);
        if (number.timer >= 0)
            continue;
        if (number.rise)
            number.position.z = F32(static_cast<double>(number.position.z) + number.speed);
        const double speed = static_cast<double>(number.speed) - static_cast<double>(0.3f);
        number.speed = F32(speed);
        if (!(speed > 0))
            number.alive = false;
        const double scale = static_cast<double>(number.scale) - 5;
        number.scale = F32(scale);
        if (!(scale >= 15))
            number.scale = 15;
    }
}

Core::Expected<void, std::string> DrawFloorItems(std::span<Kernel::FloorItemState> items,
                                                 const Terrain::SphereFrustum& frustum)
{
    constexpr float Radius = 400;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        auto& item = items[i];
        if (!item.alive)
            continue;
        item.seen = Terrain::SphereVisible(frustum, item.position, Radius);
        if (!item.seen)
            continue;
        return Core::Unexpected(
            std::format("0x608a53: el modelo y RenderItem del ítem del piso {} no están portados", i));
    }
    return {};
}

Core::Expected<void, std::string> DrawFloorItems(std::span<Kernel::FloorItemState> items,
                                                 const FloorItemDrawContext& context)
{
    return Items::DrawFloorItemDrawing(items, context);
}
Core::Expected<void, std::string> DrawRibbons(Render::ModelBatch& batch, std::span<const Kernel::RibbonState> ribbons)
{
    for (std::size_t index = 0; index < ribbons.size(); ++index)
    {
        const auto& ribbon = ribbons[index];
        if (!ribbon.alive)
            continue;
        if (ribbon.ownerEquipment == nullptr)
            return Core::Unexpected(std::format("0x52e5bc: falta el dueño durable de la cinta {}", index));
        const bool fades = *ribbon.ownerEquipment == 0;
        Blend(batch, fades && ribbon.type <= 3 ? Render::ModelBlend::Additive : Render::ModelBlend::Subtractive);
        if (ribbon.count < 2)
            continue;
        auto state = batch.State();
        constexpr std::int32_t RibbonTexture = 0x5bb;
        constexpr std::int32_t SpecialRibbonTexture = 0x639;
        const auto selector = ribbon.type == 3 ? SpecialRibbonTexture : RibbonTexture + ribbon.type;
        if (selector < 0 || selector >= static_cast<std::int32_t>(Render::OriginalTextureSlotCount))
            return Core::Unexpected(std::format("0x52e5e4: la textura de la cinta {} queda fuera de la tabla", index));
        state.texture = Render::TextureSlot{static_cast<std::uint16_t>(selector)};
        state.rawTextureName.reset();
        batch.ApplyOperation(state, Render::ModelStateOperation::Binding);
        if (ribbon.count > static_cast<std::int32_t>(Kernel::RibbonPointCount))
            return Core::Unexpected(std::format("0x52e603: la cinta {} excede sus 30 puntos", index));
        for (std::int32_t j = 0; j < ribbon.count - 1; ++j)
        {
            const auto at = static_cast<std::size_t>(j);
            const float u = F32(static_cast<double>(j) / ribbon.count);
            const float nextU = F32(static_cast<double>(j + 1) / ribbon.count);
            const double first = fades ? static_cast<double>(ribbon.count - j) / ribbon.count : 1.0;
            const double next = fades ? static_cast<double>(ribbon.count - j - 1) / ribbon.count : 1.0;
            const auto color = [&ribbon](double factor) {
                return Render::Color{F32(factor * ribbon.light.x), F32(factor * ribbon.light.y),
                                     F32(factor * ribbon.light.z), 1};
            };
            Quad(batch, {{{ribbon.first[at], {u, 1}, color(first)},
                          {ribbon.second[at], {u, 0}, color(first)},
                          {ribbon.second[at + 1], {nextU, 0}, color(next)},
                          {ribbon.first[at + 1], {nextU, 1}, color(next)}}});
        }
    }
    return {};
}
Core::Expected<void, std::string> DrawDamage(Render::ModelBatch& batch,
                                             std::span<const Kernel::DamageNumberState> damage, std::uint8_t mode)
{
    Blend(batch, Render::ModelBlend::Alpha);
    auto state = batch.State();
    state.depthTest = false;
    batch.ApplyState(state);
    for (std::size_t i = 0; i < damage.size(); ++i)
    {
        const auto& number = damage[i];
        if (!number.alive)
            continue;
        if (mode == 1 && number.position.z > 350.0f)
            continue;
        if (mode == 2 && !(number.position.z > 300.0f))
            continue;
        return Core::Unexpected(std::format("0x548efd: el atlas 0x621710 del número de daño {} no está portado", i));
    }
    return {};
}
namespace
{
void DamageGlyph(Render::ModelBatch& batch, const Math::OriginalMatrix& view, const glm::vec3& position, float width,
                 float height, const glm::vec3& light, float alpha, glm::vec2 uv, glm::vec2 extent)
{
    auto state = batch.State();
    state.texture = Render::TextureSlot{1}; // 0x621710: the common digits/miss atlas.
    state.rawTextureName.reset();
    state.space = Render::ModelCoordinateSpace::Eye;
    batch.ApplyOperation(state, Render::ModelStateOperation::Binding);
    const auto corners = Render::MakeBillboardGeometry(view, position, width, height, 0, uv, extent);
    std::array<Render::ModelVertex, 4> vertices{};
    for (std::size_t i = 0; i < vertices.size(); ++i)
        vertices[i] = {corners[i].position, corners[i].uv, {light.x, light.y, light.z, alpha}};
    Quad(batch, vertices);
}

void DamageDigits(Render::ModelBatch& batch, const Kernel::DamageNumberState& number, const Math::OriginalMatrix& view)
{
    const float alpha = F32(static_cast<double>(number.speed) * static_cast<double>(0.4f));
    if (number.number == -1)
    {
        DamageGlyph(batch, view, number.position, 45, 20, number.light, alpha, {0, 0.53125f}, {0.125f, 0.46875f});
        return;
    }
    std::array<char, 16> text{};
    const auto converted = std::to_chars(text.data(), text.data() + text.size(), number.number);
    const auto length = static_cast<std::size_t>(converted.ptr - text.data());
    const double shift = static_cast<double>(length) * number.scale * 0.125;
    glm::vec3 position = number.position;
    position.x = F32((static_cast<double>(position.x) - static_cast<double>(length) * 5) - shift);
    position.y = F32(static_cast<double>(position.y) - shift);
    const float advance = F32(static_cast<double>(number.scale) * 0.5);
    for (std::size_t i = 0; i < length; ++i)
    {
        const auto digit = static_cast<int>(text[i]) - static_cast<int>('0');
        const float u = F32(static_cast<double>(digit) * 0.0625);
        DamageGlyph(batch, view, position, number.scale, number.scale, number.light, alpha, {u, 0}, {0.0625f, 0.5f});
        position.x = F32(static_cast<double>(position.x) + advance);
        position.y = F32(static_cast<double>(position.y) + advance);
    }
}
} // namespace

Core::Expected<void, std::string> DrawDamage(Render::ModelBatch& batch,
                                             std::span<const Kernel::DamageNumberState> damage, std::uint8_t mode,
                                             const Math::OriginalMatrix& view)
{
    Blend(batch, Render::ModelBlend::Alpha);
    auto state = batch.State();
    state.depthTest = false;
    batch.ApplyState(state);
    for (const auto& number : damage)
    {
        if (!number.alive)
            continue;
        if (mode == 1 && number.position.z > 350.0f)
            continue;
        if (mode == 2 && !(number.position.z > 300.0f))
            continue;
        DamageDigits(batch, number, view);
    }
    return {};
}

Core::Expected<void, std::string> DrawGameLeaves(Render::ModelBatch& batch, const Weather::Leaves& leaves,
                                                 std::int32_t map, const Math::OriginalMatrix& view,
                                                 std::int32_t weatherState)
{
    Blend(batch, AdditiveLeaves(map) ? Render::ModelBlend::Additive : Render::ModelBlend::Alpha);
    for (const auto& leaf : leaves.All())
    {
        if (!leaf.alive)
            continue;
        auto state = batch.State();
        state.texture = Render::TextureSlot{static_cast<std::uint16_t>(leaf.type)};
        state.rawTextureName.reset();
        // 0x5306a0 calls 0x621400 without its own identity. In the first pass, map 2 keeps the active view and
        // transforms the already computed coordinates again; the second pass arrives under identity.
        state.space = Render::ModelCoordinateSpace::World;
        batch.ApplyOperation(state, Render::ModelStateOperation::Binding);
        if (map == 2)
        {
            const auto quad = Render::MakeBillboard(view, leaf.position, leaf.scale, leaf.scale, leaf.light, 0, {0, 0},
                                                    {1, 1}, static_cast<std::uint16_t>(leaf.type), 3);
            std::array<Render::ModelVertex, 4> vertices{};
            for (std::size_t i = 0; i < vertices.size(); ++i)
                vertices[i] = {quad.corners[i].position, quad.corners[i].uv, quad.color};
            Quad(batch, vertices);
        }
        else if (map >= 18 && map <= 23)
            LeafInWorld(batch, leaf, leaf.spin.x, leaf.spin.y);
        else if (leaf.type == 0x66)
        {
            if (map != 34 || weatherState == 1)
                LeafInWorld(batch, leaf, 1, 20);
        }
        else if (leaf.type == Weather::EmberTexture)
            LeafInWorld(batch, leaf, F32(static_cast<double>(leaf.scale) * 2),
                        F32(static_cast<double>(leaf.scale) * 4));
        else
            LeafInWorld(batch, leaf, 3, 3);
    }
    return {};
}
namespace
{
void Opaque(Render::ModelBatch& batch)
{
    auto state = batch.State();
    state.blend = Render::ModelBlend::Opaque;
    state.cullEnabled = true;
    state.depthWrite = true;
    state.alphaTest = false;
    state.texture2d = true;
    constexpr std::array operations{Render::ModelStateOperation::Blend, Render::ModelStateOperation::Cull,
                                    Render::ModelStateOperation::DepthWrite, Render::ModelStateOperation::AlphaTest,
                                    Render::ModelStateOperation::Texture2d};
    for (const auto operation : operations)
        batch.ApplyOperation(state, operation);
}

Core::Expected<void, std::string> DrawModelDecal(Render::ModelBatch& batch, const Effects::ModelEffect& effect,
                                                 const ModelEffectDrawResources* resources)
{
    Blend(batch, Render::ModelBlend::Additive);
    const bool rotating = effect.type == 0xcf;
    if (resources == nullptr)
        return Core::Unexpected(std::format("{}: faltan las alturas del calco del efecto {:#x}",
                                            rotating ? "0x52d749" : "0x52d90d", effect.type));
    const Terrain::TerrainDecal decal{.slot = rotating ? 0x6ad : 0x690 + effect.textureFrame % 5,
                                      .x = effect.transform.position.x,
                                      .y = effect.transform.position.y,
                                      .width = rotating ? 2.0f : 0.6f,
                                      .height = rotating ? 2.0f : 0.6f,
                                      .color = rotating ? glm::vec3(0.3f) : effect.light,
                                      .angle = rotating ? resources->worldClock : 0.0f,
                                      .alpha = 1,
                                      .lift = 5};
    if (auto drawn = Terrain::DrawTerrainDecal(batch, resources->heights, decal); !drawn.HasValue())
        return drawn;
    if (!rotating)
        Opaque(batch); // 0x52d915 calls 0x620980 only after the whole decal.
    return {};
}

// 0x52d941: the siege guard returns the whole pass, after the sprite and before adding light.
Core::Expected<bool, std::string> DrawMovingLightEffect(Render::ModelBatch& batch, Effects::ModelEffect& effect,
                                                        const ModelEffectDrawResources* resources)
{
    glm::vec3 point = effect.transform.position;
    constexpr int SampleCount = 20;
    for (int sample = 0; sample < SampleCount; ++sample)
    {
        if (effect.subtype == 1)
        {
            effect.transform.angles.z = F32(static_cast<double>(effect.transform.angles.z) - 0.1f);
            effect.light = glm::vec3(1);
            if (resources == nullptr || resources->sprites == nullptr)
                return Core::Unexpected(std::string("0x52d99e: faltan los sprites del efecto5e8..5ea/1"));
            static_cast<void>(resources->sprites->Create(0x5da, point, effect.transform.scale, effect.light,
                                                         effect.transform.angles.z, 0));
            Blend(batch, Render::ModelBlend::Additive);
            effect.light = {0.2f, 0, 0};
            const Terrain::TerrainDecal decal{.slot = 0x5f2,
                                              .x = point.x,
                                              .y = point.y,
                                              .width = 4,
                                              .height = 4,
                                              .color = effect.light,
                                              .angle = -effect.transform.angles.z,
                                              .alpha = 1,
                                              .lift = 5};
            if (auto drawn = Terrain::DrawTerrainDecal(batch, resources->heights, decal); !drawn.HasValue())
                return Core::Unexpected(drawn.Error());
            Opaque(batch);
        }
        else
        {
            if (resources == nullptr || resources->sprites == nullptr)
                return Core::Unexpected(std::string("0x52da12: faltan los sprites del efecto5e8..5ea"));
            static_cast<void>(resources->sprites->Create(0x5c6, point, effect.transform.scale, effect.light, 0, 0));
        }
        point = {F32(static_cast<double>(point.x) + effect.velocity.x),
                 F32(static_cast<double>(point.y) + effect.velocity.y),
                 F32(static_cast<double>(point.z) + effect.velocity.z)};
        if (resources->siegeActive)
        {
            if (resources->attributes == nullptr)
                return Core::Unexpected(std::string("0x52da46: falta la consulta de atributos de asedio"));
            const auto attribute = Terrain::TerrainAttributeQueryAt(*resources->attributes, point.x, point.y);
            if (!attribute.HasValue())
                return Core::Unexpected(std::string("0x52da46: el atributo de asedio queda fuera de la tabla"));
            constexpr std::uint32_t SiegeBarrier = 0x100;
            if ((attribute.Value() & SiegeBarrier) != 0)
                return true;
        }
        if (resources->dynamicLighting == nullptr)
            return Core::Unexpected(std::string("0x52da6a: falta la luz dinámica del efecto5e8..5ea"));
        Terrain::AddTerrainLight(*resources->dynamicLighting, point.x, point.y,
                                 Data::Terrain::TerrainLightColor{effect.light.x, effect.light.y, effect.light.z}, 2);
    }
    return false;
}

// 0x52d35b, 0x52d52b and 0x52d7f8: 0x4eb860 on the global buffer (0x68b181c) with Translate and parent at 0 (Q-C411):
// the root turns with the effect's angles, without its scale or its origin. Then 0x60cf70 passes the scale (+0xc) and
// the position (+0x10) to the model (+0x68 and +0x6c) and calls 0x4ebc80 with Translate at 1 (the eleventh argument,
// `push 1` at 0x52d3a1, 0x52d53a and 0x52d82f): each vertex by the bone, stored in float, times the scale, in float,
// and plus the origin, in float (0x4ebdf8..0x4ebf02), like SkinMesh. 0x60b3d0's sprites read those bones with
// 0x4ec110: with Translate at 0, plus the object's position (0x60be6e, 0x60c0e9); with 1, times the scale and plus
// the origin (0x60c44b).
Core::Expected<void, std::string> PublishEffectItem(const Models::IModelLibrary& models,
                                                    const Actors::Drawing::DrawnObject& object,
                                                    const ModelEffectDrawResources* resources)
{
    if (models.Find(Models::ModelId{object.model}) == nullptr ||
        models.SkeletonOf(Models::ModelId{object.model}) == nullptr)
        return Core::Unexpected(std::string("0x4eb860: faltan modelo o esqueleto del efecto con ítem"));
    if (resources == nullptr || !resources->onUntranslatedPose)
        return Core::Unexpected(std::string("0x4eb860: falta la pose compartida sin trasladar del efecto con ítem"));
    auto posed = object;
    posed.alpha = 1; // 0x52d35b/0x52d52b/0x52d7f8 pose before any alpha guard.
    return resources->onUntranslatedPose(posed);
}
Core::Expected<std::span<const Models::SkinnedMesh>, std::string>
EffectItemMeshes(const Models::IModelLibrary& models, const Actors::Drawing::DrawnObject& object,
                 const ModelEffectDrawResources* resources)
{
    if (resources == nullptr || !resources->posedMeshes)
        return Core::Unexpected(std::string("0x4ebc80: falta el scratch retenido del efecto con ítem"));
    const auto meshes = resources->posedMeshes();
    const auto* model = models.Find(Models::ModelId{object.model});
    if (model == nullptr || meshes.size() < model->meshes.size())
        return Core::Unexpected(std::string("0x4ebc80: el scratch no cubre las mallas del efecto con ítem"));
    return meshes;
}
Core::Expected<void, std::string> DrawBloodCastleModelEffect(Render::ModelBatch& batch,
                                                             const Models::IModelLibrary& models,
                                                             Effects::ModelEffect& effect, Core::Duration frameClock,
                                                             const ModelEffectDrawResources* resources)
{
    Actors::Drawing::DrawnObject object;
    object.model = 0x10f;
    object.animation = effect.animation;
    object.transform = effect.transform;
    object.state = effect.objectState;
    object.vertexLight = effect.vertexLight;
    if (auto posed = PublishEffectItem(models, object, resources); !posed.HasValue())
        return posed;
    effect.objectState.light = 1;
    object.state.light = 1;
    for (int axis = 0; axis < 3; ++axis)
        object.color[axis] = F32(static_cast<double>(effect.life) * effect.light[axis] * 0.05f);
    const auto meshes = EffectItemMeshes(models, object, resources);
    if (!meshes.HasValue())
        return Core::Unexpected(meshes.Error());
    return Actors::Drawing::DrawItemMeshPass(batch, models, object, frameClock, meshes.Value(), -1, 0x42);
}
float EffectItemScale(std::int16_t type)
{
    // 0x607beb..0x607bfd and the exceptions of 0x607f14..0x607f52.
    if (type == 0x1a15 || type == 0x1e27)
        return 0.5f;
    if (type == 0x1e13)
        return 1;
    if ((type >= 0x812 && type < 0xe12) || type == 0x2012)
        return 0.7f;
    return 0.8f;
}
void InitializeEffectItemPrefix(Effects::ModelEffect& effect)
{
    // 0x607b74..0x607bfd: durable stores before any type-specific CRT.
    effect.light = glm::vec3(0.3f);
    effect.objectState.blendMesh = Models::NoBlendMesh;
    effect.objectState.hiddenMesh = Models::NoHiddenMesh;
    effect.vertexLight = true;
    effect.speed = 0;
    effect.turnPending = 50;
    effect.animation = {};
    effect.turnDirection = 0;
    effect.alpha = 1;
    effect.objectState.light = 1;
    effect.objectState.uv = {};
    effect.transform.scale = EffectItemScale(effect.type);
}
Core::Expected<void, std::string> InitializeEffectItem(Effects::ModelEffect& effect, Core::Duration clock,
                                                       const ModelEffectDrawResources* resources)
{
    InitializeEffectItemPrefix(effect);
    Models::ObjectRandoms randoms{};
    const int count = effect.type == 0xe20 || effect.type == 0xc17   ? 2
                      : effect.type == 0x220 || effect.type == 0xc19 ? 1
                                                                     : 0;
    if (count != 0)
        effect.objectState.blendMesh = effect.type == 0xc17 ? 2 : 1;
    if (count != 0 && (resources == nullptr || resources->random == nullptr))
        return Core::Unexpected(std::string("0x607b60: falta el CRT del estado del efecto con ítem"));
    for (int index = 0; index < count; ++index)
        randoms[static_cast<std::size_t>(index)] = resources->random->Next();
    effect.objectState = Models::NativeItemObjectState(static_cast<std::uint16_t>(effect.type), clock, randoms);
    return {};
}
Core::Expected<void, std::string> DrawItemModelEffect(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                      Effects::ModelEffect& effect,
                                                      const Terrain::TerrainLighting* lighting, Core::Duration clock,
                                                      const ModelEffectDrawResources* resources)
{
    const bool spinning = effect.type == 0xf4;
    if (!spinning && (effect.life <= 10 || effect.state != 0))
        return {};
    if (effect.itemType == nullptr)
        return Core::Unexpected(std::string(spinning ? "0x52d294: falta el ítem del dueño del efecto f4"
                                                     : "0x52d43a: falta el ítem del dueño del efecto fa"));
    const auto originalType = effect.type;
    const auto originalPosition = effect.transform.position;
    const auto originalAngles = effect.transform.angles;
    const float originalAlpha = effect.alpha;
    const float originalTurnDirection = effect.turnDirection;
    const auto model = static_cast<std::uint16_t>(static_cast<std::int32_t>(*effect.itemType) + 0x212);
    if (spinning)
    {
        const double angularVelocity = static_cast<double>(effect.velocity.z) - 30;
        effect.velocity.z = F32(angularVelocity);
        effect.transform.angles.z = F32(angularVelocity + effect.transform.angles.z);
        effect.transform.angles.y = 90;
        effect.transform.position.z = F32(static_cast<double>(effect.transform.position.z) + 100);
    }
    effect.type = std::bit_cast<std::int16_t>(model);
    if (auto initialized = InitializeEffectItem(effect, clock, resources); !initialized.HasValue())
        return initialized;
    if (!spinning)
        effect.turnDirection = originalTurnDirection; // 0x52d4b9 after returning from 0x607b60.
    Actors::Drawing::DrawnObject object;
    object.model = model;
    object.animation = effect.animation;
    object.transform = effect.transform;
    object.state = effect.objectState;
    object.alpha = originalAlpha;
    object.vertexLight = effect.vertexLight;
    const auto setColor = [&]
    {
        const auto terrain =
            lighting != nullptr ? Terrain::LightAt(*lighting, originalPosition.x, originalPosition.y) : glm::vec3(0);
        for (int axis = 0; axis < 3; ++axis)
            object.color[axis] = F32(static_cast<double>(terrain[axis]) + effect.light[axis]);
    };
    if (!spinning)
        setColor(); // 0xfa: 0x52d4c5 comes before the 0x52d52b pose.
    if (auto posed = PublishEffectItem(models, object, resources); !posed.HasValue())
        return posed;
    if (spinning)
        setColor(); // 0xf4: 0x52d36c follows the 0x52d35b pose.
    if (effect.itemLevel == nullptr)
        return Core::Unexpected(std::string(spinning ? "0x52d3a6: falta el nivel del ítem del dueño de f4"
                                                     : "0x52d549: falta el nivel del ítem del dueño de fa"));
    const auto meshes = EffectItemMeshes(models, object, resources);
    if (!meshes.HasValue())
        return Core::Unexpected(meshes.Error());
    if (originalAlpha > 0.01f)
    {
        auto itemContext = resources != nullptr && resources->itemContext != nullptr
                               ? *resources->itemContext
                               : Actors::Drawing::ItemDrawContext{};
        auto special = itemContext.special ? *itemContext.special : Actors::Drawing::ItemSpecialDrawContext{};
        special.objectLight = &effect.light;
        special.publishedObject = &object;
        itemContext.special = &special;
        itemContext.ownerContext = &effect; // The real opaque owner; not to be reinterpreted as FaunaObject.
        itemContext.storedAlpha = &effect.alpha;
        const auto drawn = Actors::Drawing::DrawItemFromMeshes(batch, models, object, clock, meshes.Value(),
                                                               *effect.itemLevel, 0, 0, itemContext);
        effect.objectState = object.state;
        if (!drawn.HasValue())
            return drawn;
    }
    effect.type = originalType;
    if (spinning)
    {
        effect.transform.position = originalPosition;
        effect.transform.angles = originalAngles;
    }
    return {};
}

Core::Expected<void, std::string>
DrawPlainModelEffect(Render::ModelBatch& batch, Effects::ModelEffect& effect, const Models::IModelLibrary& models,
                     const Terrain::TerrainLighting* lighting, Core::Duration frameClock, float alpha,
                     const ModelEffectDrawResources* resources, std::uint64_t physicalSlot)
{
    // 0x602e90 calls 0x5ffdf0 first (0x602ea6). 0x5ffdff..0x5ffe24: on maps 18 to 23, with the hero's +0x1a1, the
    // effect's alpha (+0x174) drops 0.15f and stays stored, once per call on an original frame (like the objects in
    // WorldObjectsDrawing). 0x5ffe2a..0x5ffe3d: below 0.01f (0x6b88d4; fcomp and test ah,1, also unordered, with NaN)
    // it returns al = 0 without posing, and 0x602eb0 skips 0x6002e0: neither the pose, nor the body, nor the own case
    // (0x438fe0 of 0x125, its sprites, nor 0x11b's rand() calls).
    if (resources != nullptr && resources->originalFrame && resources->kalimaPass != nullptr)
        effect.alpha =
            Events::ChaosCastle::ChaosCastlePoseAlpha(resources->kalimaPass->map, resources->heroByte1a1, effect.alpha);
    constexpr float VisibleAlpha = 0.01f; // 0x6b88d4
    if (!(effect.alpha >= VisibleAlpha))
        return {};
    const auto model = static_cast<std::uint16_t>(effect.type);
    Actors::Drawing::DrawnObject object;
    object.model = model;
    object.animation = effect.animation;
    object.transform = effect.transform;
    object.transform.position += (effect.transform.position - effect.previousPosition) * alpha;
    object.state = effect.objectState;
    const auto terrain = lighting != nullptr
                             ? Terrain::LightAt(*lighting, effect.transform.position.x, effect.transform.position.y)
                             : glm::vec3(0);
    object.color = terrain + effect.light;
    object.alpha = effect.alpha;
    object.vertexLight = effect.vertexLight;
    if ((effect.type == 0xe1 && effect.subtype == 1) || effect.type == Effects::SpiritTrailEffect ||
        effect.type == 0x145 || effect.type == 0x147 || effect.type == 0x10a || effect.type == 0x10b ||
        (effect.type >= 0x12b && effect.type <= 0x133) ||
        (effect.type == 0x114 && (effect.subtype == 2 || effect.subtype == 6)) ||
        (effect.type == 0x115 && effect.subtype == 1) || effect.type == 0xed || effect.type == 0xee)
    {
        if (resources != nullptr && resources->onPose)
        {
            if (auto posed = resources->onPose(object); !posed.HasValue())
                return posed;
            if (resources->posedMeshes)
                object.retainedMeshes = resources->posedMeshes();
        }
        const auto body = Effects::DrawCastleWeaponBody(
            batch, models, effect, object, frameClock,
            [&](std::int32_t type, const Actors::Drawing::DrawnObject& owner,
                std::span<const Models::SkinnedMesh> meshes) -> Core::Expected<void, std::string>
            {
                if (resources == nullptr || resources->effects == nullptr || resources->random == nullptr)
                    return Core::Unexpected(std::string("0x601c90: falta pool o CRT del impacto por vértices"));
                return Actors::Drawing::EmitVertexEffects(models, owner, meshes, 0, type, 0, *resources->effects,
                                                          nullptr, *resources->random);
            });
        if (!body.HasValue())
            return body;
        if ((effect.type == 0xed || effect.type == 0xee) && effect.objectState.hiddenMesh != Kernel::HiddenObject)
        {
            if (resources == nullptr || resources->particles == nullptr || resources->sprites == nullptr ||
                resources->random == nullptr || !resources->posedBones)
                return Core::Unexpected(std::string("0x602596: faltan los servicios de la cola del misil de Crywolf"));
            return Effects::DrawCrywolfMissileTail(effect, physicalSlot, *resources->particles, *resources->sprites,
                                                   *resources->random, resources->posedBones(), resources->worldClock);
        }
        return {};
    }
    const bool castleGate = effect.type >= 0x3d && effect.type <= 0x40 && resources != nullptr &&
                            resources->kalimaPass != nullptr && resources->kalimaPass->map == 30;
    if (castleGate && effect.objectState.hiddenMesh != Kernel::HiddenObject)
    {
        if (resources->onPose)
        {
            if (auto posed = resources->onPose(object); !posed.HasValue())
                return posed;
            if (resources->posedMeshes)
                object.retainedMeshes = resources->posedMeshes();
        }
        const auto drawn = Actors::Drawing::DrawCastleSiegeBody(
            batch, models, object,
            Actors::Drawing::CastleSiegeBodyPass{
                .type = effect.type,
                .field04 = effect.subtype,
                .siegeActive = resources->siegeActive,
                .worldClock = resources->worldClock,
                .emitGateVertices =
                    [&](const Actors::Drawing::DrawnObject& gate,
                        std::span<const Models::SkinnedMesh> meshes) -> Core::Expected<void, std::string>
                {
                    if (resources->effects == nullptr || resources->random == nullptr)
                        return Core::Unexpected(std::string("0x42cfcb: falta el pool o CRT del fragmento de puerta"));
                    return Actors::Drawing::EmitVertexEffects(models, gate, meshes, 0, 0x12f, 0, *resources->effects,
                                                              nullptr, *resources->random);
                }},
            frameClock);
        if (!drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        return {};
    }
    // 0x6004aa (+0x74 at 0x80) and the colors of 0xcc/3, /4 and 0xcb/2, /3 (Effects/WizardEffectDrawing, CL11); 0x137
    // (Effects/SkinShells, 0x600557); 0x13d and 0x13e through 0x442e10 (Effects/MeteorEffects); 0x11b, 0x114/3..5,
    // 0x140 and, through 0x438fe0, 0x125 (Effects/HighEffectDrawing), after the +0x74 at 0x80, which 0x6002e0 looks at
    // before the type.
    const auto drawTheSpecialBody = [&]() -> Core::Expected<bool, std::string>
    {
        if (effect.type == Effects::SkinShellEffect && effect.renderFlags != Effects::CrackRenderFlags)
            return Effects::DrawTheSkinShellBody(batch, models, effect, object, frameClock);
        if (effect.type == Effects::MeteorEffect || effect.type == Effects::MeteorShellEffect)
            return Effects::DrawTheMeteorBody(batch, models, effect, object, frameClock,
                                              resources != nullptr ? std::optional<float>(resources->worldClock)
                                                                   : std::nullopt);
        if (effect.renderFlags != Effects::CrackRenderFlags && Effects::HasHighEffectBody(effect))
        {
            Effects::HighEffectDrawServices services;
            if (resources != nullptr)
                services = {.sprites = resources->sprites,
                            .random = resources->random,
                            .posedBones = resources->posedBones,
                            .worldClock = resources->worldClock};
            return Effects::DrawTheHighEffectBody(batch, models, effect, object, frameClock, services);
        }
        return Effects::DrawTheWizardEffectBody(batch, models, effect, object, frameClock);
    };
    if (resources != nullptr && resources->onPose && models.Find(Models::ModelId{model}) != nullptr &&
        models.SkeletonOf(Models::ModelId{model}) != nullptr)
    {
        if (auto published = resources->onPose(object); !published.HasValue())
            return published;
        // 0x600328: with +0x58 at −2 (the selection's butterfly 0xae), 0x6002e0 leaves after posing.
        if (effect.objectState.hiddenMesh != Kernel::HiddenObject)
        {
            if (resources->posedMeshes)
                object.retainedMeshes = resources->posedMeshes();
            const auto special = drawTheSpecialBody();
            if (!special.HasValue())
                return Core::Unexpected(special.Error());
            if (!special.Value())
            {
                if (auto drawn =
                        Actors::Drawing::DrawPublishedObject(batch, models, object, frameClock, resources->posedMeshes);
                    !drawn.HasValue())
                    return drawn;
            }
        }
    }
    else if (effect.objectState.hiddenMesh != Kernel::HiddenObject)
    {
        const auto special = drawTheSpecialBody();
        if (!special.HasValue())
            return Core::Unexpected(special.Error());
        if (!special.Value())
            Actors::Drawing::DrawObject(batch, models, object, frameClock);
    }
    constexpr std::int16_t KalimaType = 9;
    if (effect.type == KalimaType && resources != nullptr && resources->kalimaPass != nullptr &&
        Maps::IsKalimaMap(resources->kalimaPass->map))
    {
        Kernel::WorldObject view;
        view.type = effect.type;
        return MapScripts::Kalima::DrawKalimaObjectTail(batch, models, view, object, frameClock,
                                                        *resources->kalimaPass);
    }
    return {};
}

Core::Expected<void, std::string>
DrawGameModelEffectsChecked(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                            std::span<Effects::ModelEffect> effects, const Terrain::SphereFrustum& frustum,
                            const Terrain::TerrainLighting* lighting, Core::Duration frameClock, float alpha,
                            const ModelEffectDrawResources* resources, std::uint8_t mode)
{
    for (std::size_t i = 0; i < effects.size(); ++i)
    {
        auto& effect = effects[i];
        if (!effect.alive)
            continue;
        effect.seen = Terrain::SphereVisible(frustum, effect.transform.position, 400);
        if (!effect.seen)
            continue;
        // 0x103 (flashing.bmd) and 0x105/0 (Piercing.bmd) have no own case: 0x52d6c3 and 0x52d78f send them to 0x52da92
        // → 0x602e90 with their mesh, like the rest of 0xbe..0x150 (GenericEffect, below). The high CL11 types (0x134,
        // 0x13b, 0x141 and 0xc1a) too; 0x19f and 0x61c have no case and are not drawn.
        if (mode != 0)
        {
            const auto blendMesh = effect.objectState.blendMesh;
            if (blendMesh == -1 || blendMesh < -2)
                continue;
            const auto modelId = static_cast<std::uint16_t>(effect.type);
            if (modelId >= Tables::ModelTableSize)
                return Core::Unexpected(std::format("0x52d68c: modelo {:#x} fuera de la tabla", modelId));
            const auto* model = models.Find(Models::ModelId{modelId});
            // 0x60dbe0 initializes the table to zero. A missing BMD leaves its mesh count at zero.
            const auto meshCount = model != nullptr ? static_cast<std::int64_t>(model->meshes.size()) : 0;
            if (meshCount < blendMesh)
                continue;
        }
        if (OmittedEffect(effect))
            continue;
        if ((effect.type == 0xcf && effect.subtype >= 3 && effect.subtype <= 7) || effect.type == 0x14e)
        {
            if (auto drawn = DrawModelDecal(batch, effect, resources); !drawn.HasValue())
                return drawn;
            continue;
        }
        if (effect.type == 0x135)
        {
            // 0x52d86a..0x52d89f: two bodies; the reflection at +184+200 stays written, without being restored.
            if (auto drawn = DrawPlainModelEffect(batch, effect, models, lighting, frameClock, alpha, resources, i);
                !drawn.HasValue())
                return drawn;
            const double plane = static_cast<double>(effect.start.z) + 400.0;
            const double distance = static_cast<double>(effect.transform.position.z) - effect.start.z;
            effect.transform.position.z = F32(plane - distance);
            if (auto drawn = DrawPlainModelEffect(batch, effect, models, lighting, frameClock, alpha, resources, i);
                !drawn.HasValue())
                return drawn;
            continue;
        }
        if (effect.type >= 0x5e8 && effect.type <= 0x5ea)
        {
            const auto drawn = DrawMovingLightEffect(batch, effect, resources);
            if (!drawn.HasValue())
                return Core::Unexpected(drawn.Error());
            if (drawn.Value())
                return {};
            continue;
        }
        if (effect.type == 0xf4 || effect.type == 0xfa)
        {
            if (auto drawn = DrawItemModelEffect(batch, models, effect, lighting, frameClock, resources);
                !drawn.HasValue())
                return drawn;
            continue;
        }
        if (effect.type == 0x10f)
        {
            if (auto drawn = DrawBloodCastleModelEffect(batch, models, effect, frameClock, resources);
                !drawn.HasValue())
                return drawn;
            continue;
        }
        if (SpecialEffect(effect))
            return Core::Unexpected(std::format("0x52d699: el caso de dibujo del efecto {} ({:#x}/{}) no está portado",
                                                i, effect.type, effect.subtype));
        if (GenericEffect(effect.type))
        {
            if (auto drawn = DrawPlainModelEffect(batch, effect, models, lighting, frameClock, alpha, resources, i);
                !drawn.HasValue())
                return drawn;
        }
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawGameModelEffects(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                       std::span<Effects::ModelEffect> effects,
                                                       const Terrain::SphereFrustum& frustum,
                                                       const Terrain::TerrainLighting* lighting,
                                                       Core::Duration frameClock, float alpha, std::uint8_t mode)
{
    return DrawGameModelEffectsChecked(batch, models, effects, frustum, lighting, frameClock, alpha, nullptr, mode);
}

Core::Expected<void, std::string> DrawGameModelEffects(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                       std::span<Effects::ModelEffect> effects,
                                                       const Terrain::SphereFrustum& frustum,
                                                       const Terrain::TerrainLighting* lighting,
                                                       Core::Duration frameClock, float alpha,
                                                       const ModelEffectDrawResources& resources, std::uint8_t mode)
{
    return DrawGameModelEffectsChecked(batch, models, effects, frustum, lighting, frameClock, alpha, &resources, mode);
}
} // namespace Client::Scenes
