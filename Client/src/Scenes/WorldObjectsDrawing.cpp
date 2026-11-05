#include "Scenes/WorldObjectsDrawing.h"

#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/CharacterDrawing.h"
#include "World/TerrainCulling.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

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
                      Core::Duration frameClock, float alpha)
{
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
        switch (draw.kind)
        {
        case World::ObjectDrawKind::Hidden:
            break;
        case World::ObjectDrawKind::Body:
            DrawObject(batch, models, Drawn(object, draw, skeleton, alpha), frameClock);
            break;
        case World::ObjectDrawKind::SubtractiveMesh:
            DrawSubtractiveMesh(batch, models, Drawn(object, draw, skeleton, alpha), frameClock);
            break;
        }
    }
}
} // namespace Client::Scenes
