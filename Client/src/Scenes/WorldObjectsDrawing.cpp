#include "Scenes/WorldObjectsDrawing.h"

#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/SpriteVertex.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "World/Leaves.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "Render/Billboard.h"
#include "Render/Models/OriginalRotation.h"
#include "World/TerrainCulling.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Client::Scenes
{
namespace
{
// 0x6046a0: el margen del sector (−180) y el de cada objeto (+0xdc, −30 de 0x606660), y el paso de la
// posición a celdas.
constexpr double SectorMargin = -180.0;
constexpr double ObjectMargin = -30.0;
constexpr double CellsPerUnit = static_cast<double>(0.01f);
constexpr int CellsPerSector = 16;
constexpr double SectorCenter = 8.0;

bool SectorPasses(const World::TerrainPolygon& polygon, int sectorX, int sectorY)
{
    return World::PolygonContains(polygon, (sectorX * CellsPerSector) + SectorCenter,
                                  (sectorY * CellsPerSector) + SectorCenter, SectorMargin);
}

// 0x6046a0 pasa la posición por 0,01f (0x6b88d4) guardada en float.
bool ObjectPasses(const World::TerrainPolygon& polygon, const World::WorldObject& object)
{
    const auto cells = [](float coordinate)
    { return static_cast<double>(static_cast<float>(static_cast<double>(coordinate) * CellsPerUnit)); };
    return World::PolygonContains(polygon, cells(object.transform.position.x), cells(object.transform.position.y),
                                  ObjectMargin);
}

DrawnObject Drawn(const World::WorldObject& object, const World::ObjectDraw& draw,
                  const Render::Models::Skeleton* skeleton, float alpha)
{
    DrawnObject drawn;
    drawn.model = static_cast<std::uint16_t>(object.type);
    drawn.animation = object.animation;
    if (skeleton != nullptr && object.animation.action < skeleton->actions.size())
    {
        drawn.animation = Render::Models::Interpolated(object.animation, skeleton->actions[object.animation.action],
                                                       object.speed, alpha);
    }
    drawn.transform = object.transform;
    drawn.state = draw.state;
    drawn.color = draw.color;
    drawn.alpha = draw.alpha;
    drawn.vertexLight = draw.vertexLight;
    return drawn;
}
} // namespace

void StepWorldObjects(std::span<World::WorldObject> objects, std::uint8_t map, const IModelLibrary& models,
                      const SpecialObjectStep& special)
{
    for (const std::size_t index : World::DrawOrder(objects))
    {
        World::WorldObject& object = objects[index];
        if (!object.seen || (special && special(object)))
        {
            continue;
        }
        World::StepWorldObject(object, map, models.SkeletonOf(static_cast<std::uint16_t>(object.type)));
    }
}

void DrawWorldObjects(Render::ModelBatch& batch, const IModelLibrary& models, std::span<World::WorldObject> objects,
                      const World::TerrainPolygon& polygon, const World::TerrainLighting* lighting, std::uint8_t map,
                      Core::Duration frameClock, float alpha, const ObjectFrameEffects& effects)
{
    // El buffer global de la pose, en el orden en que lo pisan: lo de antes de los objetos y cada objeto
    // que 0x5ffdf0 posa. Solo hace falta con los sprites del farol.
    const bool glows =
        effects.random != nullptr && effects.sprites != nullptr && effects.scene == World::ObjectScene::Login;
    std::vector<PosedInTheBuffer> posed;
    if (glows)
    {
        posed.assign(effects.posedBefore.begin(), effects.posedBefore.end());
    }
    for (const std::size_t index : World::DrawOrder(objects))
    {
        World::WorldObject& object = objects[index];
        object.seen =
            SectorPasses(polygon, World::SectorX(object), World::SectorY(object)) && ObjectPasses(polygon, object);
        if (!object.seen)
        {
            continue;
        }
        const glm::vec3 light =
            lighting != nullptr ? World::LightAt(*lighting, object.transform.position.x, object.transform.position.y)
                                : glm::vec3(0.0f);
        const World::ObjectDraw draw = World::DrawOf(object, light, map);
        const Render::Models::Skeleton* skeleton = models.SkeletonOf(static_cast<std::uint16_t>(object.type));
        const DrawnObject drawn = Drawn(object, draw, skeleton, alpha);
        switch (draw.kind)
        {
        case World::ObjectDrawKind::Hidden:
            break;
        case World::ObjectDrawKind::Body:
            DrawObject(batch, models, drawn, frameClock);
            break;
        case World::ObjectDrawKind::SubtractiveMesh:
            DrawSubtractiveMesh(batch, models, drawn, frameClock);
            break;
        }
        if (glows && draw.posed)
        {
            posed.push_back(PosedInTheBuffer{&models, drawn});
        }
        // 0x602ed0: el rand() de cada objeto, y con el farol en la escena 2, sus dos sprites con esa luz.
        if (effects.random == nullptr)
        {
            continue;
        }
        const int random = effects.random->Next();
        if (glows && object.type == World::StreetLight)
        {
            const glm::vec3 glow = World::StreetLightGlowLight(random);
            for (const std::size_t bone : World::StreetLightGlowBones)
            {
                (void)effects.sprites->Create(
                    World::StreetLightGlowTexture,
                    LastPosedBonePoint(posed, bone, glm::vec3(0.0f)).value_or(glm::vec3(0.0f)), 1.0f, glow, 0.0f, 0);
            }
        }
    }
}

void StepModelEffects(std::span<World::ModelEffect> effects, const IModelLibrary& models)
{
    for (World::ModelEffect& effect : effects)
    {
        if (effect.alive)
        {
            World::StepModelEffect(effect, models.SkeletonOf(static_cast<std::uint16_t>(effect.type)));
        }
    }
}

void DrawModelEffects(Render::ModelBatch& batch, const IModelLibrary& models,
                      std::span<const World::ModelEffect> effects, const World::TerrainLighting* lighting,
                      Core::Duration frameClock, float alpha)
{
    for (const World::ModelEffect& effect : effects)
    {
        if (!effect.alive)
        {
            continue;
        }
        const glm::vec3 terrain =
            lighting != nullptr ? World::LightAt(*lighting, effect.transform.position.x, effect.transform.position.y)
                                : glm::vec3(0.0f);
        DrawnObject drawn;
        drawn.model = static_cast<std::uint16_t>(effect.type);
        drawn.animation = effect.animation;
        drawn.transform = effect.transform;
        drawn.transform.position += (effect.transform.position - effect.previousPosition) * alpha;
        drawn.state = effect.objectState;
        drawn.color = terrain + effect.light;
        drawn.alpha = effect.alpha;
        drawn.vertexLight = effect.vertexLight;
        DrawObject(batch, models, drawn, frameClock);
    }
}

namespace
{
// 0x549290 con la textura del atlas: el cuadro por su +4 (0x54941c), de 64 × 64, con un tercio de la
// textura (0x3ea8f5c3 y 0x3f28f5c3).
constexpr float AtlasSide = 64.0f;
constexpr float AtlasThird = 0.33f;
constexpr float AtlasTwoThirds = 0.66f;

glm::vec2 AtlasCell(std::int32_t cell)
{
    switch (cell)
    {
    case 1:
        return {AtlasThird, 0.0f};
    case 2:
        return {AtlasTwoThirds, 0.0f};
    case 3:
        return {0.0f, AtlasThird};
    case 4:
        return {AtlasThird, AtlasThird};
    case 5:
        return {AtlasTwoThirds, AtlasThird};
    case 6:
        return {0.0f, AtlasTwoThirds};
    case 7:
        return {AtlasThird, AtlasTwoThirds};
    default:
        return {0.0f, 0.0f};
    }
}

Render::ModelBlend SpriteBlend(const World::Sprite& sprite)
{
    if (sprite.texture == World::AtlasSprite)
    {
        return Render::ModelBlend::Alpha;
    }
    switch (sprite.blend)
    {
    case 1:
        return Render::ModelBlend::Subtractive;
    case 2:
        return Render::ModelBlend::Alpha;
    default:
        return Render::ModelBlend::Additive;
    }
}
} // namespace

void DrawFrameSprites(Render::ModelBatch& batch, World::FrameSprites& sprites, const IModelLibrary& textures,
                      const Render::Models::OriginalMatrix& view)
{
    for (World::Sprite& sprite : sprites.Sprites())
    {
        if (!sprite.alive)
        {
            continue;
        }
        World::StepSpriteAlpha(sprite);
        const Render::TextureSlot slot{sprite.texture};
        Render::Billboard billboard;
        // El tamaño: la escala por el alpha en la x87, por el ancho y el alto de la textura.
        const double size = static_cast<double>(sprite.scale) * static_cast<double>(sprite.alpha);
        if (sprite.texture == World::AtlasSprite)
        {
            billboard =
                Render::MakeBillboard(view, sprite.position, AtlasSide, AtlasSide, sprite.light, sprite.rotation,
                                      AtlasCell(sprite.blend), glm::vec2(AtlasThird), sprite.texture, 3);
        }
        else
        {
            const glm::vec2 texture = textures.TextureSize(slot).value_or(glm::vec2(0.0f));
            const auto width = static_cast<float>(size * static_cast<double>(texture.x));
            const auto height = static_cast<float>(size * static_cast<double>(texture.y));
            billboard = Render::MakeBillboard(view, sprite.position, width, height, sprite.light, sprite.rotation,
                                              glm::vec2(0.0f), glm::vec2(1.0f), sprite.texture, 3);
        }
        batch.SetState(Render::ModelDrawState{.texture = slot, .blend = SpriteBlend(sprite)});
        const auto vertex = [&billboard](std::size_t k)
        { return Render::ModelVertex{billboard.corners[k].position, billboard.corners[k].uv, billboard.color}; };
        batch.Triangle({vertex(0), vertex(1), vertex(2)});
        batch.Triangle({vertex(0), vertex(2), vertex(3)});
        // 0x549440(0) lo apaga después de dibujarlo.
        sprite.alive = false;
    }
}

void DrawParticles(Render::ModelBatch& batch, const World::Particles& particles, const IModelLibrary& textures,
                   const Render::Models::OriginalMatrix& view)
{
    constexpr int JpegComponents = 3;
    for (const World::Particle& particle : particles.All())
    {
        if (!particle.alive)
        {
            continue;
        }
        const Render::TextureSlot slot{static_cast<std::uint16_t>(particle.texture)};
        const std::optional<glm::vec2> size = textures.TextureSize(slot);
        if (!size)
        {
            continue;
        }
        const std::optional<World::ParticleDraw> drawn = World::ParticleDrawOf(particle, *size, JpegComponents);
        if (!drawn)
        {
            continue;
        }
        const Render::Billboard billboard =
            Render::MakeBillboard(view, drawn->position, drawn->width, drawn->height, drawn->light, drawn->rotation,
                                  drawn->uv, drawn->uvSize, slot.value, JpegComponents);
        batch.SetState(Render::ModelDrawState{
            .texture = slot, .blend = drawn->additive ? Render::ModelBlend::Additive : Render::ModelBlend::Alpha});
        const auto vertex = [&billboard](std::size_t k)
        { return Render::ModelVertex{billboard.corners[k].position, billboard.corners[k].uv, billboard.color}; };
        batch.Triangle({vertex(0), vertex(1), vertex(2)});
        batch.Triangle({vertex(0), vertex(2), vertex(3)});
    }
}

void DrawLeaves(Render::ModelBatch& batch, const World::Leaves& leaves)
{
    const Render::Color white{1.0f, 1.0f, 1.0f, 1.0f};
    for (const World::Leaf& leaf : leaves.All())
    {
        if (!leaf.alive)
        {
            continue;
        }
        const World::LeafQuad quad = World::LeafQuadOf(leaf);
        batch.SetState(Render::ModelDrawState{.texture = Render::TextureSlot{static_cast<std::uint16_t>(leaf.type)},
                                              .blend = Render::ModelBlend::Additive});
        // glTranslatef y el vértice: la suma la hace GL, en float.
        const auto vertex = [&quad, &white](std::size_t k)
        { return Render::ModelVertex{quad.translation + quad.corners[k], quad.uv[k], white}; };
        batch.Triangle({vertex(0), vertex(1), vertex(2)});
        batch.Triangle({vertex(0), vertex(2), vertex(3)});
    }
}
} // namespace Client::Scenes
