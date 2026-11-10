#include "Scenes/GameDrawTail.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Render/Billboard.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/ModelPoseCallbacks.h"
#include "World/Leaves.h"
#include "World/ModelEffects.h"
#include "World/Sprites.h"
#include "World/SphereFrustum.h"
#include "World/TerrainLighting.h"
#include "World/TerrainDecal.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
void Blend(Render::ModelBatch& batch, Render::ModelBlend blend)
{
    auto state = batch.State();
    state.blend = blend;
    state.cullEnabled = false;
    state.depthWrite = blend == Render::ModelBlend::Alpha;
    state.alphaTest = blend == Render::ModelBlend::Alpha;
    state.texture2d = true;
    constexpr std::array operations{Render::ModelStateOperation::Blend, Render::ModelStateOperation::Cull,
                                    Render::ModelStateOperation::DepthWrite, Render::ModelStateOperation::AlphaTest,
                                    Render::ModelStateOperation::Texture2d};
    for (const auto operation : operations)
        batch.ApplyOperation(state, operation);
}
void Quad(Render::ModelBatch& batch, const std::array<Render::ModelVertex, 4>& vertices)
{
    batch.Triangle({vertices[0], vertices[1], vertices[2]});
    batch.Triangle({vertices[0], vertices[2], vertices[3]});
}
float F32(double value)
{
    return static_cast<float>(value);
}
bool AdditiveLeaves(std::int32_t map)
{
    return map == 2 || map == 7 || map == 10 || map == 30 || map == 55;
}
void LeafInWorld(Render::ModelBatch& batch, const World::Leaf& leaf, float a, float b)
{
    const auto turn = Render::Models::OriginalAngleMatrix(leaf.angles);
    const std::array<glm::vec3, 4> local{{{-a, -a, -b}, {a, a, -b}, {a, a, b}, {-a, -a, b}}};
    const std::array<glm::vec2, 4> uv{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};
    std::array<Render::ModelVertex, 4> vertices{};
    for (std::size_t i = 0; i < vertices.size(); ++i)
        vertices[i] = {leaf.position + Render::Models::OriginalVectorTransform(local[i], turn), uv[i], {1, 1, 1, 1}};
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
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        auto& item = items[i];
        if (!item.alive)
            continue;
        item.seen = World::SphereVisible(context.frustum, item.position, 400);
        if (!item.seen)
            continue;
        // Primera familia sin remapeo, color especial de609640 ni ajuste de nivel de60b3d0:
        // Sword01..Sword19. Los casos225 y siguientes tienen sus propios pases.
        constexpr std::int16_t FirstPlainSword = 0x212;
        constexpr std::int16_t LastPlainSword = 0x224;
        if (item.type < FirstPlainSword || item.type > LastPlainSword)
            return Core::Unexpected(
                std::format("0x608a53: la familia del ítem de piso {} ({:#x}) no está portada", i, item.type));
        const auto modelId = static_cast<std::uint16_t>(item.type);
        if (context.models.Find(modelId) == nullptr || context.models.SkeletonOf(modelId) == nullptr)
            return Core::Unexpected(std::format("0x608a98: faltan el modelo o la pose del ítem de piso {}", i));
        const glm::vec3 terrain = context.lighting != nullptr
                                      ? World::LightAt(*context.lighting, item.position.x, item.position.y)
                                      : glm::vec3(0);
        const glm::vec3 light = terrain + item.light;
        DrawnObject object;
        object.model = modelId;
        object.transform = {.position = item.position, .angles = item.angles, .scale = item.scale};
        object.animation = item.animation;
        object.state = item.objectState;
        object.color = light;
        object.alpha = item.alpha;
        object.vertexLight = item.vertexLight;
        // 608b20 posa incluso si 60cf70 luego omite el cuerpo por alpha.
        if (context.onPose)
            if (auto published = context.onPose(object); !published.HasValue())
                return published;
        if (context.map == 10 || (context.map >= 24 && context.map <= 29) || context.map == 36)
            return Core::Unexpected(
                std::string("0x608cf4: el desplazamiento acuático del ítem de piso no está portado"));
        // 60cf70 sólo entra con alpha>.01; la proyección posterior corre igualmente.
        if (item.alpha > 0.01f)
        {
            if (((item.value >> 3) & 15) != 0 || item.excellent != 0 || item.ancient != 0)
                return Core::Unexpected(std::string("0x60b3d0: el brillo del ítem de piso no está portado"));
            if (context.onPose)
            {
                if (auto drawn = DrawPublishedObject(context.batch, context.models, object, context.frameClock,
                                                     context.posedMeshes);
                    !drawn.HasValue())
                    return drawn;
            }
            else
                DrawObject(context.batch, context.models, object, context.frameClock);
        }
        if (!context.project)
            return Core::Unexpected(std::string("0x608da5: falta la proyección 0x6205b0 del ítem de piso"));
        glm::vec3 point = item.position;
        constexpr float NameHeight = 30.0f; // 608d84:6b8858=00 00 f0 41.
        point.z = F32(static_cast<double>(point.z) + NameHeight);
        const auto projected = context.project(point);
        if (!projected.HasValue())
            return Core::Unexpected(projected.Error());
        item.screenX = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(projected.Value().x));
        item.screenY = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(projected.Value().y));
    }
    return {};
}
namespace
{
void JointFace(Render::ModelBatch& batch, const JointState& joint, std::size_t segment, std::size_t first,
               std::size_t second, float u, float nextU, float v, float otherV)
{
    double factor = 1;
    if (joint.type == 0x10f && (joint.subtype == 1 || joint.subtype == 2))
    {
        const auto life = joint.life < 20 ? joint.life : 20;
        factor = static_cast<double>(life) * static_cast<double>(0.05f);
    }
    const Render::Color color{F32(factor * joint.light.x), F32(factor * joint.light.y), F32(factor * joint.light.z), 1};
    Quad(batch, {{{joint.points[segment][first], {u, v}, color},
                  {joint.points[segment][second], {u, otherV}, color},
                  {joint.points[segment + 1][second], {nextU, otherV}, color},
                  {joint.points[segment + 1][first], {nextU, v}, color}}});
}

void IcarusSprites(JointState& joint, const JointDrawResources& resources)
{
    if (!resources.originalFrame && !joint.icarusRotations.has_value())
        return; // Sin muestra original previa no inventar giros para los dibujos intermedios.
    if (resources.originalFrame)
        joint.icarusRotations = std::array<float, 2>{};
    glm::vec3 center{};
    for (int component = 0; component < 3; ++component)
    {
        double sum = 0;
        for (const auto& point : joint.points[0])
            sum += point[component];
        center[component] = F32(sum * 0.25);
    }
    const float firstScale = joint.subtype == 6 ? 0.5f : 0.3f;
    const float secondScale = joint.subtype == 6 ? 1.5f : 1.0f;
    const auto first =
        resources.originalFrame ? static_cast<float>(resources.random.Next() % 360) : (*joint.icarusRotations)[0];
    (*joint.icarusRotations)[0] = first;
    static_cast<void>(resources.sprites.Create(0x62b, center, firstScale, joint.light, first, 3));
    const auto second =
        resources.originalFrame ? static_cast<float>(resources.random.Next() % 360) : (*joint.icarusRotations)[1];
    (*joint.icarusRotations)[1] = second;
    static_cast<void>(resources.sprites.Create(0x5fd, center, secondScale, joint.light, second, 3));
}

Core::Expected<void, std::string> CommonJoint(Render::ModelBatch& batch, JointState& joint,
                                              const JointDrawResources* resources)
{
    const bool commonBloodCastle =
        joint.subtype == 1 || joint.subtype == 2 || joint.subtype == 3 || (joint.subtype >= 5 && joint.subtype <= 8);
    if ((joint.type == 0x10f && !commonBloodCastle) || (joint.type == 0x62b && joint.subtype == 20) ||
        (joint.type == 0x5f2 && joint.subtype == 0))
        return Core::Unexpected(std::string("0x53c74c: el color especial del joint no está portado"));
    auto state = batch.State();
    state.texture = Render::TextureSlot{joint.texture};
    state.rawTextureName.reset();
    state.space = Render::ModelCoordinateSpace::World;
    batch.ApplyOperation(state, Render::ModelStateOperation::Binding);
    if (joint.count >= static_cast<std::int32_t>(joint.points.size()))
        return Core::Unexpected(std::string("0x53c82b: el joint excede sus puntos canónicos"));
    for (std::int32_t segment = 0; segment < joint.count; ++segment)
    {
        if (joint.type == 0x619 && (joint.subtype == 9 || joint.subtype == 10) && segment == joint.count - 1)
            continue;
        if (joint.repeatedTexture || joint.type == 0x614 || joint.type == 0x615 ||
            (joint.type == 0x62c && joint.subtype >= 0 && joint.subtype <= 4) ||
            (joint.subtype >= 11 && joint.subtype <= 13))
            return Core::Unexpected(std::string("0x53c8ee: las UV animadas del joint no están portadas"));
        const double divisor = static_cast<double>(joint.maximumCount) - 1;
        float u = F32(static_cast<double>(joint.count - segment) / divisor);
        float nextU = F32(static_cast<double>(joint.count - segment - 1) / divisor);
        if (joint.orientation == 3)
        {
            u = F32(1 - static_cast<double>(segment) / divisor);
            nextU = F32(1 - static_cast<double>(segment + 1) / divisor);
        }
        if (joint.type == 0x61c)
            return Core::Unexpected(std::string("0x53c9b8: el color/sprite del joint no está portado"));
        if (joint.type == 0x610 && (joint.subtype == 6 || joint.subtype == 8) && segment == 0)
        {
            if (resources == nullptr)
                return Core::Unexpected(std::string("0x53cd83: faltan el RNG y los sprites del joint 0x610"));
            IcarusSprites(joint, *resources);
        }
        const float v = joint.orientation == 1 ? 1.0f : 0.0f;
        const float otherV = 1 - v;
        if (joint.orientation == 2)
        {
            u = F32(1 - static_cast<double>(u));
            nextU = F32(1 - static_cast<double>(nextU));
        }
        const auto at = static_cast<std::size_t>(segment);
        if ((joint.drawEnabled & 1u) != 0)
            JointFace(batch, joint, at, 2, 3, u, nextU, otherV, v);
        if ((joint.drawEnabled & 2u) != 0)
            JointFace(batch, joint, at, 0, 1, u, nextU, v, otherV);
    }
    return {};
}
} // namespace

namespace
{
Core::Expected<void, std::string> DrawJointsChecked(Render::ModelBatch& batch, std::span<JointState> joints,
                                                    std::uint8_t mode, const JointDrawResources* resources)
{
    for (std::size_t i = 0; i < joints.size(); ++i)
    {
        auto& joint = joints[i];
        if (!joint.alive || joint.count <= 0 || !joint.drawEnabled)
            continue;
        if ((mode == 1 && joint.category == 2) || (mode == 2 && joint.category == 1))
            continue;
        switch (joint.blend)
        {
        case 1:
            Blend(batch, Render::ModelBlend::Additive);
            break;
        case 2:
            Blend(batch, Render::ModelBlend::Alpha);
            break;
        case 3:
            Blend(batch, Render::ModelBlend::Subtractive);
            break;
        case 4:
            Blend(batch, Render::ModelBlend::InverseColorAdditive);
            break;
        default:
            break;
        }
        if (joint.type == 0x619 && joint.subtype == 8)
        {
            auto state = batch.State();
            state.depthTest = false;
            batch.ApplyState(state);
        }
        if (auto drawn = CommonJoint(batch, joint, resources); !drawn.HasValue())
            return drawn;
        if (joint.type == 0x619 && joint.subtype == 8)
        {
            auto state = batch.State();
            state.depthTest = true;
            batch.ApplyState(state);
        }
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawJoints(Render::ModelBatch& batch, std::span<JointState> joints, std::uint8_t mode)
{
    return DrawJointsChecked(batch, joints, mode, nullptr);
}
Core::Expected<void, std::string> DrawJoints(Render::ModelBatch& batch, std::span<JointState> joints, std::uint8_t mode,
                                             const JointDrawResources& resources)
{
    return DrawJointsChecked(batch, joints, mode, &resources);
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
void DamageGlyph(Render::ModelBatch& batch, const Render::Models::OriginalMatrix& view, const glm::vec3& position,
                 float width, float height, const glm::vec3& light, float alpha, glm::vec2 uv, glm::vec2 extent)
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

void DamageDigits(Render::ModelBatch& batch, const DamageNumberState& number,
                  const Render::Models::OriginalMatrix& view)
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
                                             std::uint8_t mode, const Render::Models::OriginalMatrix& view)
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
                                                 std::int32_t map, const Render::Models::OriginalMatrix& view,
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

Core::Expected<void, std::string> DrawPlainModelEffect(Render::ModelBatch& batch, const World::ModelEffect& effect,
                                                       const IModelLibrary& models,
                                                       const World::TerrainLighting* lighting,
                                                       Core::Duration frameClock, float alpha,
                                                       const ModelEffectDrawResources* resources)
{
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
    if (resources != nullptr && resources->onPose && !(effect.alpha < 0.01f) && models.Find(model) != nullptr &&
        models.SkeletonOf(model) != nullptr)
    {
        if (auto published = resources->onPose(object); !published.HasValue())
            return published;
        return DrawPublishedObject(batch, models, object, frameClock, resources->posedMeshes);
    }
    DrawObject(batch, models, object, frameClock);
    return {};
}

Core::Expected<void, std::string>
DrawGameModelEffectsChecked(Render::ModelBatch& batch, const IModelLibrary& models,
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
        if (mode != 0)
        {
            const auto blendMesh = effect.objectState.blendMesh;
            if (blendMesh == -1 || blendMesh < -2)
                continue;
            const auto* model = models.Find(static_cast<std::uint16_t>(effect.type));
            if (model == nullptr)
                return Core::Unexpected(std::string("0x52d68c: falta el modelo del filtro de blendMesh"));
            if (static_cast<std::int64_t>(model->meshes.size()) < blendMesh)
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
            if (auto drawn = DrawPlainModelEffect(batch, effect, models, lighting, frameClock, alpha, resources);
                !drawn.HasValue())
                return drawn;
            const double plane = static_cast<double>(effect.start.z) + 400.0;
            const double distance = static_cast<double>(effect.transform.position.z) - effect.start.z;
            effect.transform.position.z = F32(plane - distance);
            if (auto drawn = DrawPlainModelEffect(batch, effect, models, lighting, frameClock, alpha, resources);
                !drawn.HasValue())
                return drawn;
            continue;
        }
        if (SpecialEffect(effect))
            return Core::Unexpected(std::format("0x52d699: el caso de dibujo del efecto {} ({:#x}/{}) no está portado",
                                                i, effect.type, effect.subtype));
        if (GenericEffect(effect.type))
        {
            if (auto drawn = DrawPlainModelEffect(batch, effect, models, lighting, frameClock, alpha, resources);
                !drawn.HasValue())
                return drawn;
        }
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawGameModelEffects(Render::ModelBatch& batch, const IModelLibrary& models,
                                                       std::span<World::ModelEffect> effects,
                                                       const World::SphereFrustum& frustum,
                                                       const World::TerrainLighting* lighting,
                                                       Core::Duration frameClock, float alpha, std::uint8_t mode)
{
    return DrawGameModelEffectsChecked(batch, models, effects, frustum, lighting, frameClock, alpha, nullptr, mode);
}

Core::Expected<void, std::string> DrawGameModelEffects(Render::ModelBatch& batch, const IModelLibrary& models,
                                                       std::span<World::ModelEffect> effects,
                                                       const World::SphereFrustum& frustum,
                                                       const World::TerrainLighting* lighting,
                                                       Core::Duration frameClock, float alpha,
                                                       const ModelEffectDrawResources& resources, std::uint8_t mode)
{
    return DrawGameModelEffectsChecked(batch, models, effects, frustum, lighting, frameClock, alpha, &resources, mode);
}
} // namespace Client::Scenes
