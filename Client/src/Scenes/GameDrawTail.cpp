#include "Scenes/GameDrawTail.h"
#include "Maps/MapId.h"
#include "Math/X87.h"
#include "Models/IModelLibrary.h"
#include "Actors/Drawing/VertexEffects.h"
#include "Scenes/CrywolfMissileDrawing.h"
#include "Scenes/KalimaObjectServices.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/KalimaWorld.h"
#include "World/WorldObjects.h"
#include "World/ChaosCastle.h"

#include "Core/IClock.h"
#include "Models/ObjectState.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Render/Billboard.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Math/OriginalRotation.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Scenes/GameFloorItemDrawing.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"
#include "Scenes/GameTailDrawingPrimitives.h"
#include "Scenes/HighEffectDrawing.h"
#include "Actors/Drawing/ModelPoseCallbacks.h"
#include "Scenes/MeteorEffects.h"
#include "Scenes/SkinShells.h"
#include "Scenes/WizardEffectDrawing.h"
#include "World/GameModelEffects.h"
#include "World/Leaves.h"
#include "World/ModelEffects.h"
#include "World/SkinShellEffects.h"
#include "World/Sprites.h"
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
void LeafInWorld(Render::ModelBatch& batch, const World::Leaf& leaf, float a, float b)
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
bool SpecialEffect(const World::ModelEffect& effect)
{
    const auto type = effect.type;
    return (type == 0xcf && effect.subtype >= 3 && effect.subtype <= 7) || type == 0xf4 || type == 0xfa ||
           type == 0x10f || type == 0x135 || type == 0x14e || (type >= 0x5e8 && type <= 0x5ea);
}
bool OmittedEffect(const World::ModelEffect& effect)
{
    return effect.type == 0xf3 || effect.type == 0xf8 || (effect.type == 0x105 && effect.subtype == 3) ||
           (effect.type == 0x115 && effect.subtype == 1);
}
} // namespace
void StepDamage(std::span<DamageNumberState> damage)
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

Core::Expected<void, std::string> DrawFloorItems(std::span<FloorItemState> items, const World::SphereFrustum& frustum)
{
    constexpr float Radius = 400;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        auto& item = items[i];
        if (!item.alive)
            continue;
        item.seen = World::SphereVisible(frustum, item.position, Radius);
        if (!item.seen)
            continue;
        return Core::Unexpected(
            std::format("0x608a53: el modelo y RenderItem del ítem del piso {} no están portados", i));
    }
    return {};
}

Core::Expected<void, std::string> DrawFloorItems(std::span<FloorItemState> items, const FloorItemDrawContext& context)
{
    return DrawFloorItemDrawing(items, context);
}
Core::Expected<void, std::string> DrawRibbons(Render::ModelBatch& batch, std::span<const RibbonState> ribbons)
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
        if (ribbon.count > static_cast<std::int32_t>(RibbonPointCount))
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
Core::Expected<void, std::string> DrawDamage(Render::ModelBatch& batch, std::span<const DamageNumberState> damage,
                                             std::uint8_t mode)
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
    state.texture = Render::TextureSlot{1}; // 0x621710: el atlas común de cifras/miss.
    state.rawTextureName.reset();
    state.space = Render::ModelCoordinateSpace::Eye;
    batch.ApplyOperation(state, Render::ModelStateOperation::Binding);
    const auto corners = Render::MakeBillboardGeometry(view, position, width, height, 0, uv, extent);
    std::array<Render::ModelVertex, 4> vertices{};
    for (std::size_t i = 0; i < vertices.size(); ++i)
        vertices[i] = {corners[i].position, corners[i].uv, {light.x, light.y, light.z, alpha}};
    Quad(batch, vertices);
}

void DamageDigits(Render::ModelBatch& batch, const DamageNumberState& number, const Math::OriginalMatrix& view)
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

Core::Expected<void, std::string> DrawDamage(Render::ModelBatch& batch, std::span<const DamageNumberState> damage,
                                             std::uint8_t mode, const Math::OriginalMatrix& view)
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

Core::Expected<void, std::string> DrawGameLeaves(Render::ModelBatch& batch, const World::Leaves& leaves,
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
        // 5306a0 llama621400 sin identidad propia. En el primer pase2 queda la vista activa y
        // vuelve a transformar las coordenadas ya calculadas; el segundo pase llega bajo identidad.
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
        else if (leaf.type == World::EmberTexture)
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

Core::Expected<void, std::string> DrawModelDecal(Render::ModelBatch& batch, const World::ModelEffect& effect,
                                                 const ModelEffectDrawResources* resources)
{
    Blend(batch, Render::ModelBlend::Additive);
    const bool rotating = effect.type == 0xcf;
    if (resources == nullptr)
        return Core::Unexpected(std::format("{}: faltan las alturas del calco del efecto {:#x}",
                                            rotating ? "0x52d749" : "0x52d90d", effect.type));
    const World::TerrainDecal decal{.slot = rotating ? 0x6ad : 0x690 + effect.textureFrame % 5,
                                    .x = effect.transform.position.x,
                                    .y = effect.transform.position.y,
                                    .width = rotating ? 2.0f : 0.6f,
                                    .height = rotating ? 2.0f : 0.6f,
                                    .color = rotating ? glm::vec3(0.3f) : effect.light,
                                    .angle = rotating ? resources->worldClock : 0.0f,
                                    .alpha = 1,
                                    .lift = 5};
    if (auto drawn = World::DrawTerrainDecal(batch, resources->heights, decal); !drawn.HasValue())
        return drawn;
    if (!rotating)
        Opaque(batch); // 52d915 llama620980 sólo después del calco completo.
    return {};
}

// 52d941: el guard de asedio devuelve toda la pasada, después del sprite y antes de sumar luz.
Core::Expected<bool, std::string> DrawMovingLightEffect(Render::ModelBatch& batch, World::ModelEffect& effect,
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
            const World::TerrainDecal decal{.slot = 0x5f2,
                                            .x = point.x,
                                            .y = point.y,
                                            .width = 4,
                                            .height = 4,
                                            .color = effect.light,
                                            .angle = -effect.transform.angles.z,
                                            .alpha = 1,
                                            .lift = 5};
            if (auto drawn = World::DrawTerrainDecal(batch, resources->heights, decal); !drawn.HasValue())
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
            const auto attribute = World::TerrainAttributeQueryAt(*resources->attributes, point.x, point.y);
            if (!attribute.HasValue())
                return Core::Unexpected(std::string("0x52da46: el atributo de asedio queda fuera de la tabla"));
            constexpr std::uint32_t SiegeBarrier = 0x100;
            if ((attribute.Value() & SiegeBarrier) != 0)
                return true;
        }
        if (resources->dynamicLighting == nullptr)
            return Core::Unexpected(std::string("0x52da6a: falta la luz dinámica del efecto5e8..5ea"));
        World::AddTerrainLight(*resources->dynamicLighting, point.x, point.y,
                               Data::Terrain::TerrainLightColor{effect.light.x, effect.light.y, effect.light.z}, 2);
    }
    return false;
}

// 0x52d35b, 0x52d52b y 0x52d7f8: 0x4eb860 en el buffer global (0x68b181c) con Translate y padre en 0 (Q-C411):
// la raíz gira con los ángulos del efecto, sin su escala ni su origen. Después, 0x60cf70 pasa la escala (+0xc) y
// la posición (+0x10) al modelo (+0x68 y +0x6c) y llama a 0x4ebc80 con Translate en 1 (el undécimo argumento,
// `push 1` en 0x52d3a1, 0x52d53a y 0x52d82f): cada vértice por el hueso, guardado en float, por la escala, en
// float, y más el origen, en float (0x4ebdf8..0x4ebf02), como SkinMesh. Los sprites de 0x60b3d0 leen esos huesos
// con 0x4ec110: con Translate en 0, más la posición del objeto (0x60be6e, 0x60c0e9); con 1, por la escala y más
// el origen (0x60c44b).
Core::Expected<void, std::string> PublishEffectItem(const Models::IModelLibrary& models, const DrawnObject& object,
                                                    const ModelEffectDrawResources* resources)
{
    if (models.Find(object.model) == nullptr || models.SkeletonOf(object.model) == nullptr)
        return Core::Unexpected(std::string("0x4eb860: faltan modelo o esqueleto del efecto con ítem"));
    if (resources == nullptr || !resources->onUntranslatedPose)
        return Core::Unexpected(std::string("0x4eb860: falta la pose compartida sin trasladar del efecto con ítem"));
    auto posed = object;
    posed.alpha = 1; // 52d35b/52d52b/52d7f8 posan antes de cualquier guarda de alpha.
    return resources->onUntranslatedPose(posed);
}
Core::Expected<std::span<const Models::SkinnedMesh>, std::string>
EffectItemMeshes(const Models::IModelLibrary& models, const DrawnObject& object,
                 const ModelEffectDrawResources* resources)
{
    if (resources == nullptr || !resources->posedMeshes)
        return Core::Unexpected(std::string("0x4ebc80: falta el scratch retenido del efecto con ítem"));
    const auto meshes = resources->posedMeshes();
    const auto* model = models.Find(object.model);
    if (model == nullptr || meshes.size() < model->meshes.size())
        return Core::Unexpected(std::string("0x4ebc80: el scratch no cubre las mallas del efecto con ítem"));
    return meshes;
}
Core::Expected<void, std::string> DrawBloodCastleModelEffect(Render::ModelBatch& batch,
                                                             const Models::IModelLibrary& models,
                                                             World::ModelEffect& effect, Core::Duration frameClock,
                                                             const ModelEffectDrawResources* resources)
{
    DrawnObject object;
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
    return DrawItemMeshPass(batch, models, object, frameClock, meshes.Value(), -1, 0x42);
}
float EffectItemScale(std::int16_t type)
{
    // 607beb..607bfd y las excepciones de607f14..607f52.
    if (type == 0x1a15 || type == 0x1e27)
        return 0.5f;
    if (type == 0x1e13)
        return 1;
    if ((type >= 0x812 && type < 0xe12) || type == 0x2012)
        return 0.7f;
    return 0.8f;
}
void InitializeEffectItemPrefix(World::ModelEffect& effect)
{
    // 607b74..607bfd: stores durables anteriores a cualquier CRT específico del tipo.
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
Core::Expected<void, std::string> InitializeEffectItem(World::ModelEffect& effect, Core::Duration clock,
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
                                                      World::ModelEffect& effect,
                                                      const World::TerrainLighting* lighting, Core::Duration clock,
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
        effect.turnDirection = originalTurnDirection; // 52d4b9 después de volver de607b60.
    DrawnObject object;
    object.model = model;
    object.animation = effect.animation;
    object.transform = effect.transform;
    object.state = effect.objectState;
    object.alpha = originalAlpha;
    object.vertexLight = effect.vertexLight;
    const auto setColor = [&]
    {
        const auto terrain =
            lighting != nullptr ? World::LightAt(*lighting, originalPosition.x, originalPosition.y) : glm::vec3(0);
        for (int axis = 0; axis < 3; ++axis)
            object.color[axis] = F32(static_cast<double>(terrain[axis]) + effect.light[axis]);
    };
    if (!spinning)
        setColor(); // fa:52d4c5 precede a la pose52d52b.
    if (auto posed = PublishEffectItem(models, object, resources); !posed.HasValue())
        return posed;
    if (spinning)
        setColor(); // f4:52d36c sigue a la pose52d35b.
    if (effect.itemLevel == nullptr)
        return Core::Unexpected(std::string(spinning ? "0x52d3a6: falta el nivel del ítem del dueño de f4"
                                                     : "0x52d549: falta el nivel del ítem del dueño de fa"));
    const auto meshes = EffectItemMeshes(models, object, resources);
    if (!meshes.HasValue())
        return Core::Unexpected(meshes.Error());
    if (originalAlpha > 0.01f)
    {
        auto itemContext =
            resources != nullptr && resources->itemContext != nullptr ? *resources->itemContext : ItemDrawContext{};
        auto special = itemContext.special ? *itemContext.special : ItemSpecialDrawContext{};
        special.objectLight = &effect.light;
        special.publishedObject = &object;
        itemContext.special = &special;
        itemContext.ownerContext = &effect; // Dueño opaco real; no reinterpretar como FaunaObject.
        itemContext.storedAlpha = &effect.alpha;
        const auto drawn =
            DrawItemFromMeshes(batch, models, object, clock, meshes.Value(), *effect.itemLevel, 0, 0, itemContext);
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
DrawPlainModelEffect(Render::ModelBatch& batch, World::ModelEffect& effect, const Models::IModelLibrary& models,
                     const World::TerrainLighting* lighting, Core::Duration frameClock, float alpha,
                     const ModelEffectDrawResources* resources, std::uint64_t physicalSlot)
{
    // 0x602e90 llama primero a 0x5ffdf0 (0x602ea6). 0x5ffdff..0x5ffe24: en los mapas 18 a 23, con +0x1a1 del héroe, el
    // alpha del efecto (+0x174) baja 0,15f y queda guardado, una vez por llamada en un frame del original (como los
    // objetos en WorldObjectsDrawing). 0x5ffe2a..0x5ffe3d: por debajo de 0,01f (0x6b88d4; fcomp y test ah,1, también
    // sin orden, con NaN) devuelve al = 0 sin posar, y 0x602eb0 salta 0x6002e0: ni la pose, ni el cuerpo, ni el caso
    // propio (0x438fe0 del 0x125, sus sprites, ni los rand() del 0x11b).
    if (resources != nullptr && resources->originalFrame && resources->kalimaPass != nullptr)
        effect.alpha = World::ChaosCastlePoseAlpha(resources->kalimaPass->map, resources->heroByte1a1, effect.alpha);
    constexpr float VisibleAlpha = 0.01f; // 0x6b88d4
    if (!(effect.alpha >= VisibleAlpha))
        return {};
    const auto model = static_cast<std::uint16_t>(effect.type);
    DrawnObject object;
    object.model = model;
    object.animation = effect.animation;
    object.transform = effect.transform;
    object.transform.position += (effect.transform.position - effect.previousPosition) * alpha;
    object.state = effect.objectState;
    const auto terrain = lighting != nullptr
                             ? World::LightAt(*lighting, effect.transform.position.x, effect.transform.position.y)
                             : glm::vec3(0);
    object.color = terrain + effect.light;
    object.alpha = effect.alpha;
    object.vertexLight = effect.vertexLight;
    if ((effect.type == 0xe1 && effect.subtype == 1) || effect.type == World::SpiritTrailEffect ||
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
        const auto body = DrawCastleWeaponBody(
            batch, models, effect, object, frameClock,
            [&](std::int32_t type, const DrawnObject& owner,
                std::span<const Models::SkinnedMesh> meshes) -> Core::Expected<void, std::string>
            {
                if (resources == nullptr || resources->effects == nullptr || resources->random == nullptr)
                    return Core::Unexpected(std::string("0x601c90: falta pool o CRT del impacto por vértices"));
                return EmitVertexEffects(models, owner, meshes, 0, type, 0, *resources->effects, *resources->random);
            });
        if (!body.HasValue())
            return body;
        if ((effect.type == 0xed || effect.type == 0xee) && effect.objectState.hiddenMesh != World::HiddenObject)
        {
            if (resources == nullptr || resources->particles == nullptr || resources->sprites == nullptr ||
                resources->random == nullptr || !resources->posedBones)
                return Core::Unexpected(std::string("0x602596: faltan los servicios de la cola del misil de Crywolf"));
            return DrawCrywolfMissileTail(effect, physicalSlot, *resources->particles, *resources->sprites,
                                          *resources->random, resources->posedBones(), resources->worldClock);
        }
        return {};
    }
    const bool castleGate = effect.type >= 0x3d && effect.type <= 0x40 && resources != nullptr &&
                            resources->kalimaPass != nullptr && resources->kalimaPass->map == 30;
    if (castleGate && effect.objectState.hiddenMesh != World::HiddenObject)
    {
        if (resources->onPose)
        {
            if (auto posed = resources->onPose(object); !posed.HasValue())
                return posed;
            if (resources->posedMeshes)
                object.retainedMeshes = resources->posedMeshes();
        }
        const auto drawn = DrawCastleSiegeBody(
            batch, models, object,
            CastleSiegeBodyPass{
                .type = effect.type,
                .field04 = effect.subtype,
                .siegeActive = resources->siegeActive,
                .worldClock = resources->worldClock,
                .emitGateVertices = [&](const DrawnObject& gate, std::span<const Models::SkinnedMesh> meshes)
                    -> Core::Expected<void, std::string>
                {
                    if (resources->effects == nullptr || resources->random == nullptr)
                        return Core::Unexpected(std::string("0x42cfcb: falta el pool o CRT del fragmento de puerta"));
                    return EmitVertexEffects(models, gate, meshes, 0, 0x12f, 0, *resources->effects,
                                             *resources->random);
                }},
            frameClock);
        if (!drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        return {};
    }
    // 0x6004aa (+0x74 en 0x80) y los colores del 0xcc/3, /4 y el 0xcb/2, /3 (Scenes/WizardEffectDrawing, CL11); el
    // 0x137 (Scenes/SkinShells, 0x600557); el 0x13d y el 0x13e por 0x442e10 (Scenes/MeteorEffects); el 0x11b, el
    // 0x114/3..5, el 0x140 y, por 0x438fe0, el 0x125 (Scenes/HighEffectDrawing), después del +0x74 en 0x80, que
    // 0x6002e0 mira antes que el tipo.
    const auto drawTheSpecialBody = [&]() -> Core::Expected<bool, std::string>
    {
        if (effect.type == World::SkinShellEffect && effect.renderFlags != World::CrackRenderFlags)
            return DrawTheSkinShellBody(batch, models, effect, object, frameClock);
        if (effect.type == MeteorEffect || effect.type == MeteorShellEffect)
            return DrawTheMeteorBody(batch, models, effect, object, frameClock,
                                     resources != nullptr ? std::optional<float>(resources->worldClock) : std::nullopt);
        if (effect.renderFlags != World::CrackRenderFlags && HasHighEffectBody(effect))
        {
            HighEffectDrawServices services;
            if (resources != nullptr)
                services = {.sprites = resources->sprites,
                            .random = resources->random,
                            .posedBones = resources->posedBones,
                            .worldClock = resources->worldClock};
            return DrawTheHighEffectBody(batch, models, effect, object, frameClock, services);
        }
        return DrawTheWizardEffectBody(batch, models, effect, object, frameClock);
    };
    if (resources != nullptr && resources->onPose && models.Find(model) != nullptr &&
        models.SkeletonOf(model) != nullptr)
    {
        if (auto published = resources->onPose(object); !published.HasValue())
            return published;
        // 0x600328: con +0x58 en −2 (la mariposa 0xae de la selección), 0x6002e0 sale después de posar.
        if (effect.objectState.hiddenMesh != World::HiddenObject)
        {
            if (resources->posedMeshes)
                object.retainedMeshes = resources->posedMeshes();
            const auto special = drawTheSpecialBody();
            if (!special.HasValue())
                return Core::Unexpected(special.Error());
            if (!special.Value())
            {
                if (auto drawn = DrawPublishedObject(batch, models, object, frameClock, resources->posedMeshes);
                    !drawn.HasValue())
                    return drawn;
            }
        }
    }
    else if (effect.objectState.hiddenMesh != World::HiddenObject)
    {
        const auto special = drawTheSpecialBody();
        if (!special.HasValue())
            return Core::Unexpected(special.Error());
        if (!special.Value())
            DrawObject(batch, models, object, frameClock);
    }
    constexpr std::int16_t KalimaType = 9;
    if (effect.type == KalimaType && resources != nullptr && resources->kalimaPass != nullptr &&
        Maps::IsKalimaMap(resources->kalimaPass->map))
    {
        World::WorldObject view;
        view.type = effect.type;
        return DrawKalimaObjectTail(batch, models, view, object, frameClock, *resources->kalimaPass);
    }
    return {};
}

Core::Expected<void, std::string>
DrawGameModelEffectsChecked(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                            std::span<World::ModelEffect> effects, const World::SphereFrustum& frustum,
                            const World::TerrainLighting* lighting, Core::Duration frameClock, float alpha,
                            const ModelEffectDrawResources* resources, std::uint8_t mode)
{
    for (std::size_t i = 0; i < effects.size(); ++i)
    {
        auto& effect = effects[i];
        if (!effect.alive)
            continue;
        effect.seen = World::SphereVisible(frustum, effect.transform.position, 400);
        if (!effect.seen)
            continue;
        // 0x103 (flashing.bmd) y 0x105/0 (Piercing.bmd) no tienen caso propio: 0x52d6c3 y 0x52d78f los mandan a
        // 0x52da92 → 0x602e90 con su malla, como el resto de 0xbe..0x150 (GenericEffect, más abajo). Los tipos altos de
        // la CL11 (0x134, 0x13b, 0x141 y 0xc1a) también; el 0x19f y el 0x61c no tienen caso y no se dibujan.
        if (mode != 0)
        {
            const auto blendMesh = effect.objectState.blendMesh;
            if (blendMesh == -1 || blendMesh < -2)
                continue;
            const auto modelId = static_cast<std::uint16_t>(effect.type);
            if (modelId >= Tables::ModelTableSize)
                return Core::Unexpected(std::format("0x52d68c: modelo {:#x} fuera de la tabla", modelId));
            const auto* model = models.Find(modelId);
            // 0x60dbe0 inicializa la tabla a cero. Un BMD ausente deja en cero su cuenta de mallas.
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
            // 52d86a..52d89f: dos cuerpos; el reflejo en +184+200 queda escrito, sin restauración.
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
                                                       std::span<World::ModelEffect> effects,
                                                       const World::SphereFrustum& frustum,
                                                       const World::TerrainLighting* lighting,
                                                       Core::Duration frameClock, float alpha, std::uint8_t mode)
{
    return DrawGameModelEffectsChecked(batch, models, effects, frustum, lighting, frameClock, alpha, nullptr, mode);
}

Core::Expected<void, std::string> DrawGameModelEffects(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                       std::span<World::ModelEffect> effects,
                                                       const World::SphereFrustum& frustum,
                                                       const World::TerrainLighting* lighting,
                                                       Core::Duration frameClock, float alpha,
                                                       const ModelEffectDrawResources& resources, std::uint8_t mode)
{
    return DrawGameModelEffectsChecked(batch, models, effects, frustum, lighting, frameClock, alpha, &resources, mode);
}
} // namespace Client::Scenes
