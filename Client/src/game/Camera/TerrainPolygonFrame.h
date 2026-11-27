#pragma once

#include "Core/Expected.h"
#include "Camera/GameCamera.h"
#include "Terrain/TerrainCulling.h"

#include <string>

namespace Client::Kernel
{
struct WorldRuntime;
} // namespace Client::Kernel

// 0x5fb570's global trapezoid and its effective level (WorldRuntime::terrainPolygon and terrainPolygonState), as the
// selection's (0x63475c) and the game's (0x637366) draws leave them: 0x5fbb00 → 0x5fb570 once per original frame,
// after the camera (0x635ae0). Contract: Client/docs/research/06-render-escenas-y-ui.md, "El trapecio completo" and
// "El paso de los personajes".
namespace Client::Camera
{

// 0x5fb570 in one original frame: the effective level's transition with the requested level (0x7d3a0ec), the D
// (0x7d36ba4) and target (0x6d5ee0) that frame's camera left, and `input`'s trapezoid. Leaves both in `runtime`. On
// error it leaves nothing (ADR 0009) and the text names 0x5fb570.
[[nodiscard]] Core::Expected<Terrain::TerrainPolygon, std::string>
StepTheTerrainPolygon(Kernel::WorldRuntime& runtime, const Terrain::TerrainPolygonInput& input,
                      const Camera::GameCamera& camera);

// A MuRet draw in between, not an original frame: `input`'s trapezoid with the current effective level, without the
// transition and writing nothing.
[[nodiscard]] Core::Expected<Terrain::TerrainPolygon, std::string>
TerrainPolygonBetweenFrames(const Kernel::WorldRuntime& runtime, const Terrain::TerrainPolygonInput& input);
} // namespace Client::Camera
