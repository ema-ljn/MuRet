#pragma once

#include "Core/Expected.h"
#include "Camera/CameraState.h"
#include "Camera/GameCamera.h"
#include "Terrain/TerrainCulling.h"

#include <string>

// 0x5fb570's global trapezoid and its effective level (WorldRuntime::terrainPolygon and CameraState::terrainPolygon),
// as the selection's (0x63475c) and the game's (0x637366) draws leave them: 0x5fbb00 → 0x5fb570 once per original
// frame, after the camera (0x635ae0). Contract: Client/docs/research/06-render-escenas-y-ui.md, "El trapecio completo"
// and "El paso de los personajes".
namespace Client::Camera
{

// 0x5fb570 in one original frame: the effective level's transition with the requested level (0x7d3a0ec), the D
// (0x7d36ba4) and target (0x6d5ee0) that frame's camera left, and `input`'s trapezoid. Leaves the level in `state`
// and the trapezoid in `trapezoid`. On error it leaves nothing (ADR 0009) and the text names 0x5fb570.
[[nodiscard]] Core::Expected<Terrain::TerrainPolygon, std::string>
StepTheTerrainPolygon(CameraState& state, Terrain::TerrainPolygon& trapezoid, const Terrain::TerrainPolygonInput& input,
                      const Camera::GameCamera& camera);

// A MuRet draw in between, not an original frame: `input`'s trapezoid with the current effective level, without the
// transition and writing nothing.
[[nodiscard]] Core::Expected<Terrain::TerrainPolygon, std::string>
TerrainPolygonBetweenFrames(const CameraState& state, const Terrain::TerrainPolygonInput& input);
} // namespace Client::Camera
