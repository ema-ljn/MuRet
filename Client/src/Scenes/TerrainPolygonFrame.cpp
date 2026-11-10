#include "Scenes/TerrainPolygonFrame.h"

#include "Core/Expected.h"
#include "Scenes/WorldRuntime.h"
#include "World/GameCamera.h"
#include "World/TerrainCulling.h"

#include <cstdint>
#include <string>

namespace Client::Scenes
{
namespace
{
std::string DescribePolygonError(World::TerrainPolygonError error)
{
    switch (error)
    {
    case World::TerrainPolygonError::InvalidCameraDistance:
        return "0x5fb570: la distancia de la cámara no entra en un int64";
    case World::TerrainPolygonError::InvalidEffectiveLevel:
        return "0x5fb570: el nivel del trapecio no es de 0 a 4";
    }
    return "0x5fb570: un error desconocido";
}
} // namespace

Core::Expected<World::TerrainPolygon, std::string>
StepTheTerrainPolygon(WorldRuntime& runtime, const World::TerrainPolygonInput& input, const World::GameCamera& camera)
{
    const World::TerrainPolygonCameraInput cameraInput{.requestedLevel = static_cast<std::int16_t>(camera.level),
                                                       .distance = camera.distance,
                                                       .targetDistance = camera.targetDistance};
    const auto frame = World::TerrainPolygonFrameOf(runtime.terrainPolygonState, input, cameraInput);
    if (!frame.HasValue())
    {
        return Core::Unexpected(DescribePolygonError(frame.Error()));
    }
    // 0x5fb731 y 0x5fba55..0x5fba68: el nivel y el trapecio, en los globales que leen las tres escenas.
    runtime.terrainPolygonState = frame.Value().nextState;
    runtime.terrainPolygon = frame.Value().polygon;
    return frame.Value().polygon;
}

Core::Expected<World::TerrainPolygon, std::string> TerrainPolygonBetweenFrames(const WorldRuntime& runtime,
                                                                               const World::TerrainPolygonInput& input)
{
    const auto polygon = World::TerrainPolygonOf(input, runtime.terrainPolygonState.effectiveLevel);
    if (!polygon.HasValue())
    {
        return Core::Unexpected(DescribePolygonError(polygon.Error()));
    }
    return polygon.Value();
}
} // namespace Client::Scenes
