#include "Camera/TerrainPolygonFrame.h"

#include "Core/Expected.h"
#include "Kernel/WorldRuntime.h"
#include "Camera/GameCamera.h"
#include "Terrain/TerrainCulling.h"

#include <cstdint>
#include <string>

namespace Client::Camera
{
namespace
{
std::string DescribePolygonError(Terrain::TerrainPolygonError error)
{
    switch (error)
    {
    case Terrain::TerrainPolygonError::InvalidCameraDistance:
        return "0x5fb570: la distancia de la cámara no entra en un int64";
    case Terrain::TerrainPolygonError::InvalidEffectiveLevel:
        return "0x5fb570: el nivel del trapecio no es de 0 a 4";
    }
    return "0x5fb570: un error desconocido";
}
} // namespace

Core::Expected<Terrain::TerrainPolygon, std::string> StepTheTerrainPolygon(Kernel::WorldRuntime& runtime,
                                                                           const Terrain::TerrainPolygonInput& input,
                                                                           const Camera::GameCamera& camera)
{
    const Terrain::TerrainPolygonCameraInput cameraInput{.requestedLevel = static_cast<std::int16_t>(camera.level),
                                                         .distance = camera.distance,
                                                         .targetDistance = camera.targetDistance};
    const auto frame = Terrain::TerrainPolygonFrameOf(runtime.terrainPolygonState, input, cameraInput);
    if (!frame.HasValue())
    {
        return Core::Unexpected(DescribePolygonError(frame.Error()));
    }
    // 0x5fb731 y 0x5fba55..0x5fba68: el nivel y el trapecio, en los globales que leen las tres escenas.
    runtime.terrainPolygonState = frame.Value().nextState;
    runtime.terrainPolygon = frame.Value().polygon;
    return frame.Value().polygon;
}

Core::Expected<Terrain::TerrainPolygon, std::string>
TerrainPolygonBetweenFrames(const Kernel::WorldRuntime& runtime, const Terrain::TerrainPolygonInput& input)
{
    const auto polygon = Terrain::TerrainPolygonOf(input, runtime.terrainPolygonState.effectiveLevel);
    if (!polygon.HasValue())
    {
        return Core::Unexpected(DescribePolygonError(polygon.Error()));
    }
    return polygon.Value();
}
} // namespace Client::Camera
