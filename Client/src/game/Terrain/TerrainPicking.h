#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Terrain
{
struct TerrainPickingSegment;
struct TerrainPickingState;
struct TerrainPickingTarget;
struct TerrainPolygon;
struct TerrainRange;
struct TerrainView;

// 0x5fc260(1): resets flag/maxT, sweeps and retries internally only if it selected. Keeps hit/XY on a miss. 06 § El
// rayo contra el terreno. forceVisible accepts after evaluating both frustums. Numeric contract PC53/nearest, the ASM's
// float32 stores and no FMA; it does not prove the original's CW. Requires heights/attributes, blocks 0..251, a finite
// ray, heights in [0, 765] and fixedHeight in [0, 1201]. Validates only the visited corners. The retained selection
// allows floats 0..254; the stock caller produces integers. Both cases have positive widths and defined normals. A
// stable snapshot and nearest rounding are preconditions; it does not reproduce x87 flags/exceptions. Explicit errors
// keep the input state. It needs no mapping, light or textures.
[[nodiscard]] Core::Expected<void, std::string> PickTerrain(TerrainPickingState& state, const TerrainView& view,
                                                            const TerrainRange& range, const TerrainPolygon& polygon,
                                                            const TerrainPickingSegment& segment,
                                                            bool forceVisible = false);

// The pure branch of the MoveHero consumer: the previous pass and ANOTHER retry, even after a miss, on the retained
// cell. Publishes target only if that attempt hits (Value() == true); a miss or an error keeps target. The
// integration of Shift, cadence/UI, the last render's ray and walking is elsewhere.
[[nodiscard]] Core::Expected<bool, std::string>
PickTerrainTarget(TerrainPickingState& state, TerrainPickingTarget& target, const TerrainView& view,
                  const TerrainRange& range, const TerrainPolygon& polygon, const TerrainPickingSegment& segment,
                  bool forceVisible = false);
} // namespace Client::Terrain
