#include "Scenes/WorldObjectsDrawing.h"

#include "Core/IClock.h"
#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainGrid.h"
#include "World/TerrainCells.h"
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
#include "World/WorldObjectDrawing.h"
#include "World/WorldObjectDrawingError.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <optional>
#include <ratio>
#include <span>
#include <vector>

namespace Client::Scenes
{
namespace
{
// 0x6046a0: el margen del sector (−180) y el paso de la posición a celdas.
constexpr double SectorMargin = -180.0;
constexpr double CellsPerUnit = static_cast<double>(0.01f);
constexpr int CellsPerSector = 16;
constexpr double SectorCenter = 8.0;
constexpr std::uint8_t LoginCloudMap = 55;
constexpr std::int16_t LoginCloudMarker = 0x54;
constexpr int LoginCloudChance = 5;
// 52dc20: binder de los giros y binder del calco, respectivamente.
constexpr Render::TextureSlot ArrivalRingTexture{static_cast<std::uint16_t>(World::ArrivalEffect)};
constexpr Render::TextureSlot ArrivalGroundTexture{0x61e};

bool SectorPasses(const World::TerrainPolygon& polygon, int sectorX, int sectorY)
{
    return World::PolygonContains(polygon, (sectorX * CellsPerSector) + SectorCenter,
                                  (sectorY * CellsPerSector) + SectorCenter, SectorMargin);
}

// 0x6046a0 pasa la posición por 0,01f (0x6b88d4) guardada en float y el margen individual (+0xdc).
bool ObjectPasses(const World::TerrainPolygon& polygon, const World::WorldObject& object)
{
    const auto cells = [](float coordinate)
    { return static_cast<double>(static_cast<float>(static_cast<double>(coordinate) * CellsPerUnit)); };
    return World::PolygonContains(polygon, cells(object.transform.position.x), cells(object.transform.position.y),
                                  object.cullingMargin);
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
// La pose se toma una vez por dibujo; la emisión sigue el orden frame → objetos.
struct SeenObjectEffects
{
    World::WorldObject* object = nullptr;
    std::size_t index = 0;
    std::optional<std::array<glm::vec3, World::StreetLightGlowBones.size()>> glowPositions;
    std::optional<int> lastRandom;
};

void CreateObjectCloud(const World::WorldObject& object, std::uint8_t map, float clock,
                       const ObjectFrameEffects& effects)
{
    // 0x6044bc: el marcador oculto del mapa 55, después del rand común, sin guard de escena.
    if (map != LoginCloudMap || object.type != LoginCloudMarker || effects.random->Next() % LoginCloudChance != 0 ||
        effects.particles == nullptr)
    {
        return;
    }
    // El caller original pasa luz indeterminada; el subtipo 10 la sobrescribe con RGB cero.
    (void)effects.particles->Create(World::CloudParticle, object.transform.position, object.transform.angles,
                                    glm::vec3(0.0f), World::LoginCloudSubtype, object.transform.scale, clock,
                                    *effects.random);
}

void AdvanceObjectEffects(std::span<SeenObjectEffects> seen, std::uint8_t map, Core::Duration frameClock,
                          const ObjectFrameEffects& effects)
{
    const auto clock = static_cast<float>(std::chrono::duration<double, std::milli>(frameClock).count());
    for (int frame = 0; frame < effects.frames; ++frame)
    {
        for (SeenObjectEffects& entry : seen)
        {
            entry.lastRandom = effects.random->Next();
            CreateObjectCloud(*entry.object, map, clock, effects);
        }
    }
}

void DrawObjectGlows(std::span<const SeenObjectEffects> seen, const ObjectFrameEffects& effects)
{
    if (effects.frames > 0)
    {
        // Solo se puede repetir una muestra del último frame original, no de una aparición anterior.
        for (std::optional<int>& random : effects.lastRandoms)
        {
            random.reset();
        }
    }
    for (const SeenObjectEffects& entry : seen)
    {
        if (entry.index < effects.lastRandoms.size())
        {
            effects.lastRandoms[entry.index] = entry.lastRandom;
        }
        if (!entry.glowPositions || !entry.lastRandom)
        {
            continue;
        }
        const glm::vec3 glow = World::StreetLightGlowLight(*entry.lastRandom);
        for (const glm::vec3& position : *entry.glowPositions)
        {
            (void)effects.sprites->Create(World::StreetLightGlowTexture, position, 1.0f, glow, 0.0f, 0);
        }
    }
}
} // namespace

Core::Expected<void, World::WorldObjectDrawingError> DrawBoundWorldObject(World::WorldObject*& object,
                                                                          World::WorldObjectDrawingContext& context,
                                                                          const WorldObjectBodyGate& gate,
                                                                          std::uint32_t argument2,
                                                                          std::uint32_t argument3, std::uint32_t mode)
{
    if (!gate)
        return Core::Unexpected(World::WorldObjectDrawingError::MissingBinding);
    const auto prepared = gate(object, argument2, argument3, mode);
    if (!prepared.HasValue())
        return Core::Unexpected(prepared.Error());
    if ((prepared.Value() & 0xffu) == 0)
        return {};
    return World::DrawWorldObject(*object, context, mode);
}

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
    std::vector<SeenObjectEffects> seen;
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
        if (effects.random == nullptr)
        {
            continue;
        }
        SeenObjectEffects entry{.object = &object,
                                .index = index,
                                .glowPositions = std::nullopt,
                                .lastRandom =
                                    index < effects.lastRandoms.size() ? effects.lastRandoms[index] : std::nullopt};
        if (glows && object.type == World::StreetLight)
        {
            entry.glowPositions.emplace();
            for (std::size_t k = 0; k < World::StreetLightGlowBones.size(); ++k)
            {
                (*entry.glowPositions)[k] = LastPosedBonePoint(posed, World::StreetLightGlowBones[k], glm::vec3(0.0f))
                                                .value_or(glm::vec3(0.0f));
            }
        }
        seen.push_back(entry);
    }
    AdvanceObjectEffects(seen, map, frameClock, effects);
    DrawObjectGlows(seen, effects);
}

namespace
{
void StepModelEffectsWithRandom(std::span<World::ModelEffect> effects, const IModelLibrary& models,
                                Render::Models::CrtRandom* random)
{
    for (World::ModelEffect& effect : effects)
    {
        if (!effect.alive)
        {
            continue;
        }
        if (effect.type == World::ArrivalEffect)
        {
            if (random != nullptr && effect.subtype == 0)
            {
                World::StepArrivalModelEffect(effect, *random);
            }
            continue;
        }
        if (random != nullptr)
        {
            // 5208c5: prefijo común también para B4; la hoja 61f ya lo consume una vez.
            (void)random->Next();
        }
        World::StepModelEffect(effect, models.SkeletonOf(static_cast<std::uint16_t>(effect.type)));
    }
}
} // namespace

void StepModelEffects(std::span<World::ModelEffect> effects, const IModelLibrary& models)
{
    StepModelEffectsWithRandom(effects, models, nullptr);
}

void StepModelEffects(std::span<World::ModelEffect> effects, const IModelLibrary& models,
                      Render::Models::CrtRandom& random)
{
    StepModelEffectsWithRandom(effects, models, &random);
}

Core::Expected<void, World::ModelEffectStepError> StepModelEffects(std::span<World::ModelEffect> effects,
                                                                   const IModelLibrary& models,
                                                                   Render::Models::CrtRandom& random,
                                                                   const World::ModelEffectStepBindings& bindings)
{
    for (World::ModelEffect& effect : effects)
    {
        if (!effect.alive)
        {
            continue;
        }
        if (!World::IsConstructedMapEffect(effect.type))
        {
            StepModelEffectsWithRandom(std::span(&effect, 1), models, &random);
            continue;
        }
        auto result = World::StepConstructedModelEffect(effect, random, bindings);
        if (!result.HasValue())
        {
            return Core::Unexpected(result.Error());
        }
    }
    return {};
}

void DrawModelEffectBody(World::ModelEffect& effect, const ModelEffectBodyDraw& drawBody)
{
    constexpr std::int16_t MirroredEffect = 0x135;
    constexpr double MirrorHeight = 400.0;
    const bool mirrored = effect.type == MirroredEffect;
    drawBody(effect);
    if (!mirrored)
    {
        return;
    }
    // El primer helper puede cambiar todos estos campos. No se releen alive/tipo ni se restaura Z.
    const double firstAnchor = effect.start.z;
    const double upper = firstAnchor + MirrorHeight;
    const double position = effect.transform.position.z;
    const double secondAnchor = effect.start.z;
    effect.transform.position.z = static_cast<float>(upper - (position - secondAnchor));
    drawBody(effect);
}

namespace
{
std::optional<ModelEffectDrawError> ModelEffectBindingError(const IModelLibrary& models, std::uint16_t id)
{
    const Data::Models::Model* model = models.Find(id);
    if (model == nullptr)
    {
        return ModelEffectDrawError{ModelEffectDrawErrorKind::MissingModel, id};
    }
    if (models.SkeletonOf(id) == nullptr)
    {
        return ModelEffectDrawError{ModelEffectDrawErrorKind::MissingSkeleton, id};
    }
    if (models.MeshTextures(id).size() < model->meshes.size())
    {
        return ModelEffectDrawError{ModelEffectDrawErrorKind::MissingTextures, id};
    }
    return std::nullopt;
}

void DrawSingleModelEffect(Render::ModelBatch& batch, const IModelLibrary& models, const World::ModelEffect& effect,
                           const World::TerrainLighting* lighting, Core::Duration frameClock, float alpha)
{
    const glm::vec3 terrain = lighting != nullptr
                                  ? World::LightAt(*lighting, effect.transform.position.x, effect.transform.position.y)
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
} // namespace

void DrawModelEffects(Render::ModelBatch& batch, const IModelLibrary& models,
                      std::span<const World::ModelEffect> effects, const World::TerrainLighting* lighting,
                      Core::Duration frameClock, float alpha)
{
    for (const World::ModelEffect& effect : effects)
    {
        if (!effect.alive || effect.type == World::ArrivalEffect)
        {
            continue;
        }
        DrawSingleModelEffect(batch, models, effect, lighting, frameClock, alpha);
    }
}

void VisitVisibleModelEffects(std::span<World::ModelEffect> effects, const World::ModelEffectPlanes& planes,
                              const ModelEffectVisitor& visitor)
{
    constexpr float EffectMargin = 400.0f;
    for (World::ModelEffect& effect : effects)
    {
        if (!effect.alive)
        {
            continue;
        }
        const std::uint8_t visible = World::ModelEffectVisibility(planes, effect.transform.position, EffectMargin);
        effect.visible = visible;
        if (visible != 0 && !visitor(effect))
        {
            return;
        }
    }
}

void DrawModelEffects(Render::ModelBatch& batch, const IModelLibrary& models, std::span<World::ModelEffect> effects,
                      const World::ModelEffectPlanes& planes, const World::TerrainLighting* lighting,
                      Core::Duration frameClock, float alpha)
{
    VisitVisibleModelEffects(effects, planes,
                             [&](World::ModelEffect& effect)
                             {
                                 if (effect.type != World::ArrivalEffect)
                                 {
                                     DrawModelEffectBody(
                                         effect, [&](World::ModelEffect& live)
                                         { DrawSingleModelEffect(batch, models, live, lighting, frameClock, alpha); });
                                 }
                                 return true;
                             });
}

Core::Expected<void, ModelEffectDrawError> DrawBoundModelEffects(Render::ModelBatch& batch, const IModelLibrary& models,
                                                                 std::span<World::ModelEffect> effects,
                                                                 const World::ModelEffectPlanes& planes,
                                                                 const World::TerrainLighting* lighting,
                                                                 Core::Duration frameClock, float alpha)
{
    std::optional<ModelEffectDrawError> error;
    VisitVisibleModelEffects(effects, planes,
                             [&](World::ModelEffect& effect)
                             {
                                 if (effect.type == World::ArrivalEffect)
                                 {
                                     return true;
                                 }
                                 const auto id = static_cast<std::uint16_t>(effect.type);
                                 error = ModelEffectBindingError(models, id);
                                 if (error)
                                 {
                                     return false;
                                 }
                                 DrawModelEffectBody(
                                     effect, [&](World::ModelEffect& live)
                                     { DrawSingleModelEffect(batch, models, live, lighting, frameClock, alpha); });
                                 return true;
                             });
    if (error)
    {
        return Core::Unexpected(*error);
    }
    return {};
}

namespace
{
// 53d2c0 / 5fc9c0: idénticos productos y stores que la matriz existente, en posiciones transpuestas.
// El transpose es de bits; NO usa el sentido de giro de 5fc8f0 para estos vértices.
Render::Models::OriginalMatrix ArrivalAngleMatrix(float angle)
{
    const auto forward = Render::Models::OriginalAngleMatrix({0.0f, 0.0f, angle});
    Render::Models::OriginalMatrix inverse{};
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            inverse[(row * 4) + column] = forward[(column * 4) + row];
        }
    }
    return inverse;
}

glm::vec3 ArrivalPoint(const glm::vec3& local, const Render::Models::OriginalMatrix& matrix, const glm::vec3& position)
{
    const glm::vec3 rotated = Render::Models::OriginalVectorRotate(local, matrix);
    return {static_cast<float>(static_cast<double>(rotated.x) + static_cast<double>(position.x)),
            static_cast<float>(static_cast<double>(rotated.y) + static_cast<double>(position.y)),
            static_cast<float>(static_cast<double>(rotated.z) + static_cast<double>(position.z))};
}

void DrawArrivalRing(Render::ModelBatch& batch, const glm::vec3& position, float theta)
{
    constexpr int Sectors = 12;
    constexpr double SectorAngle = 30.0;
    constexpr float SectorU = std::bit_cast<float>(0x3daaaaabu);
    constexpr float LowerRadius = 90.0f;
    constexpr float UpperRadius = 130.0f;
    constexpr float Height = 200.0f;
    const Render::Color white{1.0f, 1.0f, 1.0f, 1.0f};
    const Render::Color black{0.0f, 0.0f, 0.0f, 1.0f};
    for (int sector = 0; sector < Sectors; ++sector)
    {
        const double index = sector;
        const auto first = ArrivalAngleMatrix(static_cast<float>((index * SectorAngle) + theta));
        const auto next = ArrivalAngleMatrix(static_cast<float>(((index + 1.0) * SectorAngle) + theta));
        const float u0 = static_cast<float>(index * static_cast<double>(SectorU));
        const float u1 = static_cast<float>((index + 1.0) * static_cast<double>(SectorU));
        const std::array<Render::ModelVertex, 4> corners{{
            {ArrivalPoint({0.0f, LowerRadius, 0.0f}, first, position), {u0, 1.0f}, white},
            {ArrivalPoint({0.0f, LowerRadius, 0.0f}, next, position), {u1, 1.0f}, white},
            {ArrivalPoint({0.0f, UpperRadius, Height}, next, position), {u1, 0.0f}, black},
            {ArrivalPoint({0.0f, UpperRadius, Height}, first, position), {u0, 0.0f}, black},
        }};
        // La diagonal del fan es la misma que usa el backend para los otros quads del original.
        batch.Triangle({corners[0], corners[1], corners[2]});
        batch.Triangle({corners[0], corners[2], corners[3]});
    }
}

// La misma conversión portable ya acreditada para 69fd3c en TerrainHeight/Particles; aquí el
// argumento es el reloj float32 del caller. No hay dependencia de std::chrono ni módulo positivo.
std::int32_t ArrivalFtol(float value)
{
    constexpr double Int64Limit = 9223372036854775808.0;
    if (std::isnan(value) || static_cast<double>(value) >= Int64Limit || static_cast<double>(value) < -Int64Limit)
    {
        return 0;
    }
    const auto whole = static_cast<std::int64_t>(value);
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(whole)));
}

std::int32_t ArrivalDifference(std::int32_t first, std::int32_t second)
{
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(first) - std::bit_cast<std::uint32_t>(second));
}

// 5fb2f0: parámetros locales de la grilla, sin pool ni estado gráfico simulado.
struct ArrivalDecalGrid
{
    glm::vec2 origin{};
    glm::vec2 offset{};
    float reciprocal = 0.0f;
    float radius = 0.0f;
    float aspect = 0.0f;
    Render::Models::OriginalMatrix rotation{};
};

ArrivalDecalGrid ArrivalDecalGridOf(float x, float y, float scale, float rotation)
{
    constexpr double Half = 0.5;
    ArrivalDecalGrid grid;
    grid.rotation = Render::Models::OriginalAngleMatrix({0.0f, 0.0f, rotation});
    const auto cellX = static_cast<float>(static_cast<double>(x) * static_cast<double>(CellsPerUnit));
    const auto cellY = static_cast<float>(static_cast<double>(y) * static_cast<double>(CellsPerUnit));
    const std::int32_t ix = ArrivalFtol(cellX);
    const std::int32_t iy = ArrivalFtol(cellY);
    grid.origin = {static_cast<float>(ix), static_cast<float>(iy)};
    const double halfScale = static_cast<double>(scale) * Half;
    grid.offset.x = static_cast<float>((static_cast<double>(grid.origin.x) - cellX) + halfScale);
    // IY permanece extendido en el original; IX sí se recarga desde su store float32.
    grid.offset.y = static_cast<float>((static_cast<double>(iy) - cellY) + halfScale);
    grid.reciprocal = static_cast<float>(1.0 / static_cast<double>(scale));
    grid.radius = static_cast<float>(ArrivalDifference(ArrivalFtol(scale), -1));
    grid.aspect = static_cast<float>(static_cast<double>(scale) / static_cast<double>(scale));
    return grid;
}

std::array<glm::vec2, 4> ArrivalDecalUvs(const ArrivalDecalGrid& grid, float counterX, float offsetX1, float v0,
                                         float v1)
{
    constexpr double Half = 0.5;
    const auto u0 = static_cast<float>((static_cast<double>(counterX) + grid.offset.x) * grid.reciprocal);
    const auto u1 = static_cast<float>((static_cast<double>(offsetX1) + counterX) * grid.reciprocal);
    std::array<glm::vec2, 4> uv{{{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}}};
    for (glm::vec2& corner : uv)
    {
        const glm::vec3 centered{static_cast<float>(static_cast<double>(corner.x) - Half),
                                 static_cast<float>(static_cast<double>(corner.y) - Half), 0.0f};
        const glm::vec3 rotated = Render::Models::OriginalVectorRotate(centered, grid.rotation);
        corner.x = static_cast<float>((static_cast<double>(grid.aspect) * rotated.x) + Half);
        corner.y = static_cast<float>(static_cast<double>(rotated.y) + Half);
    }
    return uv;
}

void DrawArrivalCell(Render::ModelBatch& batch, const Data::Terrain::TerrainHeights& heights, float x, float y,
                     const std::array<glm::vec2, 4>& uv, const Render::Color& color)
{
    constexpr int LastCell = 255;
    constexpr double HeightOffset = 5.0;
    const std::int32_t ix = ArrivalFtol(x);
    const std::int32_t iy = ArrivalFtol(y);
    if (ix < 0 || iy < 0 || ix >= LastCell || iy >= LastCell)
    {
        return;
    }
    const auto first = static_cast<std::size_t>(iy) * Data::Terrain::TerrainSide + static_cast<std::size_t>(ix);
    const auto below = first + Data::Terrain::TerrainSide;
    const std::array<std::size_t, 4> indices{first, first + 1, below + 1, below};
    const double left = static_cast<double>(x) * World::TerrainCellSize;
    const double bottom = static_cast<double>(y) * World::TerrainCellSize;
    const auto right = static_cast<float>(left + World::TerrainCellSize);
    const auto top = static_cast<float>(bottom + World::TerrainCellSize);
    const std::array<glm::vec2, 4> xy{{{static_cast<float>(left), static_cast<float>(bottom)},
                                       {right, static_cast<float>(bottom)},
                                       {right, top},
                                       {static_cast<float>(left), top}}};
    std::array<Render::ModelVertex, 4> corners{};
    for (std::size_t k = 0; k < corners.size(); ++k)
    {
        corners[k] = {
            {xy[k].x, xy[k].y, static_cast<float>(static_cast<double>(heights.cells[indices[k]]) + HeightOffset)},
            uv[k],
            color};
    }
    batch.Triangle({corners[0], corners[1], corners[2]});
    batch.Triangle({corners[0], corners[2], corners[3]});
}

void DrawArrivalDecalRow(Render::ModelBatch& batch, const Data::Terrain::TerrainHeights& heights,
                         const ArrivalDecalGrid& grid, float counterY, const Render::Color& color)
{
    const double rowOffset = static_cast<double>(counterY) + grid.offset.y;
    const auto v0 = static_cast<float>(rowOffset * grid.reciprocal);
    const auto v1 = static_cast<float>((rowOffset + 1.0) * grid.reciprocal);
    const auto offsetX1 = static_cast<float>(static_cast<double>(grid.offset.x) + 1.0);
    float counterX = -grid.radius;
    do
    {
        const auto uv = ArrivalDecalUvs(grid, counterX, offsetX1, v0, v1);
        DrawArrivalCell(batch, heights, static_cast<float>(static_cast<double>(grid.origin.x) + counterX),
                        static_cast<float>(static_cast<double>(grid.origin.y) + counterY), uv, color);
        const double next = static_cast<double>(counterX) + 1.0;
        counterX = static_cast<float>(next);
        if (next > static_cast<double>(grid.radius))
        {
            return;
        }
    } while (true);
}

void DrawArrivalDecal(Render::ModelBatch& batch, const Data::Terrain::TerrainHeights& heights, float x, float y,
                      float scale, const Render::Color& color, float rotation)
{
    const ArrivalDecalGrid grid = ArrivalDecalGridOf(x, y, scale, rotation);
    batch.SetState({.texture = ArrivalGroundTexture, .blend = Render::ModelBlend::Additive});
    // No guard por escala0: 5fb2f0 hace las divisiones y el radio resulta1. Sus UV pueden ser NaN.
    if (grid.radius < -grid.radius)
    {
        return;
    }
    float counterY = -grid.radius;
    do
    {
        DrawArrivalDecalRow(batch, heights, grid, counterY, color);
        const double next = static_cast<double>(counterY) + 1.0;
        counterY = static_cast<float>(next);
        if (next > static_cast<double>(grid.radius))
        {
            return;
        }
    } while (true);
}

void DrawArrivalGround(Render::ModelBatch& batch, const World::ModelEffect& effect,
                       const Data::Terrain::TerrainHeights& heights, float worldClock)
{
    constexpr std::int32_t AnglePeriod = 3600;
    constexpr float AngleScale = std::bit_cast<float>(0x3dcccccdu);
    constexpr float FadeStep = std::bit_cast<float>(0x3e4ccccdu);
    constexpr float ScaleStep = std::bit_cast<float>(0x3e19999au);
    constexpr float GreenScale = std::bit_cast<float>(0x3ecccccdu);
    constexpr std::int32_t FadeLife = 5;
    constexpr std::int32_t InitialLife = 20;
    const auto remainder = ArrivalFtol(worldClock) % AnglePeriod;
    const auto theta = static_cast<float>(static_cast<double>(remainder) * static_cast<double>(AngleScale));
    batch.SetState({.texture = ArrivalRingTexture, .blend = Render::ModelBlend::Additive});
    DrawArrivalRing(batch, effect.transform.position, theta);
    DrawArrivalRing(batch, effect.transform.position, -theta);
    const std::int32_t life = effect.life;
    const double fade =
        life >= FadeLife
            ? 1.0
            : 1.0 - (static_cast<double>(ArrivalDifference(FadeLife, life)) * static_cast<double>(FadeStep));
    const auto scale =
        static_cast<float>(static_cast<double>(ArrivalDifference(InitialLife, life)) * static_cast<double>(ScaleStep));
    const Render::Color color{static_cast<float>(fade), static_cast<float>(fade * static_cast<double>(GreenScale)),
                              static_cast<float>(fade * static_cast<double>(FadeStep)), 1.0f};
    DrawArrivalDecal(batch, heights, effect.transform.position.x, effect.transform.position.y, scale, color,
                     -effect.transform.angles.z);
}
} // namespace

void DrawGroundModelEffects(Render::ModelBatch& batch, std::span<const World::ModelEffect> effects,
                            const Data::Terrain::TerrainHeights& heights, float worldClock,
                            Render::Models::CrtRandom& random)
{
    for (const World::ModelEffect& effect : effects)
    {
        if (!effect.alive || effect.visible == 0)
        {
            continue;
        }
        (void)random.Next();
        if (effect.type == World::ArrivalEffect && effect.subtype == 0)
        {
            DrawArrivalGround(batch, effect, heights, worldClock);
        }
    }
}

Core::Expected<void, World::ModelEffectStepError>
DrawGroundModelEffects(Render::ModelBatch& batch, std::span<World::ModelEffect> effects,
                       const Data::Terrain::TerrainHeights& heights, float worldClock,
                       Render::Models::CrtRandom& random, const GroundModelEffectService& service)
{
    if (!service)
    {
        return Core::Unexpected(World::ModelEffectStepError::MissingBinding);
    }
    for (World::ModelEffect& effect : effects)
    {
        if (!effect.alive || effect.visible == 0)
        {
            continue;
        }
        (void)random.Next();
        service();
        // El tipo es posterior a 620a90; las guardas anteriores no se vuelven a comprobar.
        if (effect.type == World::ArrivalEffect && effect.subtype == 0)
        {
            DrawArrivalGround(batch, effect, heights, worldClock);
        }
    }
    return {};
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

Render::ModelBlend ParticleBlend(World::ParticleDraw::Blend blend)
{
    switch (blend)
    {
    case World::ParticleDraw::Blend::Alpha:
        return Render::ModelBlend::Alpha;
    case World::ParticleDraw::Blend::Additive:
        return Render::ModelBlend::Additive;
    case World::ParticleDraw::Blend::Subtractive:
        return Render::ModelBlend::Subtractive;
    }
    return Render::ModelBlend::Alpha;
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

void DrawParticles(Render::ModelBatch& batch, World::Particles& particles, const IModelLibrary& textures,
                   const Render::Models::OriginalMatrix& view, float worldClock)
{
    constexpr int JpegComponents = 3;
    const std::span<World::Particle> all = particles.All();
    for (std::size_t index = 0; index < all.size(); ++index)
    {
        World::Particle& particle = all[index];
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
        const std::optional<World::ParticleDraw> drawn =
            World::ParticleDrawOf(particle, *size, JpegComponents, worldClock, index);
        if (!drawn)
        {
            continue;
        }
        const Render::Billboard billboard =
            Render::MakeBillboard(view, drawn->position, drawn->width, drawn->height, drawn->light, drawn->rotation,
                                  drawn->uv, drawn->uvSize, slot.value, JpegComponents);
        batch.SetState(Render::ModelDrawState{.texture = slot, .blend = ParticleBlend(drawn->blend)});
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
