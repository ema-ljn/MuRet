#pragma once

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainLight.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

// A world's terrain light with the shade of its heights, and the light at a point of the world, as the models that do
// not use per-vertex light use it (Client/docs/research/06-render-escenas-y-ui.md, "La luz del terreno en un
// punto"). The login's logo and character take it from their place on map 55, and the terrain uses it too.
namespace Client::Terrain
{
// Map 30 (0x1e) shades with −1 and 1 (0x6b8b9c and 0x6b87d4) where the others use −0.5 and 0.5.
inline constexpr std::uint8_t BrightShadingMap = 30;

// A 256 × 256 RGB table: the shaded base of 0x7c0ccc4 or an explicit destination for the window and the sums of
// 0x7b4ccc4. The type does not decide when it is initialized or updated.
struct TerrainLighting
{
    Data::Terrain::TerrainLayer<Data::Terrain::TerrainLightColor> cells;
};

// 0x5f8b10 and 0x5f8d90, when a world loads (0x5f8f40):
//   - each cell's (x, y) normal is that of 0x5fd000 (Terrain::FaceNormal) with the triangle (x + 1, y),
//     (x + 1, y + 1), (x, y + 1), with the cells 100 units apart and the heights of `heights`. The heights are
//     looked up with x + 1 and y + 1 in a byte (column 255 takes the height of column 0, and row 255 that of row 0),
//     and the positions are not;
//   - the shade is (b · n.z + a · n.y) + n.x · 0.5 + 0.5, with a = −0.5 and b = 0.5 (−1 and 1 on map 30), from 0
//     to 1, and it multiplies each light channel.
// The ASM's PC53/nearest profile and no FMA, as in the picking: the shade stays in 53 bits up to each product, and
// only the product is stored as float. It does not set the original's CW.
// `map` is the map number (SceneWorld's), not its folder's.
[[nodiscard]] std::unique_ptr<TerrainLighting> ShadeTerrainLight(const Data::Terrain::TerrainLight& light,
                                                                 const Data::Terrain::TerrainHeights& heights,
                                                                 std::uint8_t map);

// 0x5f8c10 → 0x5f8e40: an 8 × 8 window, the center clamped to 4..252, new normals and shade 0.5 even on map 30.
void ReshadeTerrainLightWindow(TerrainLighting& lighting, const Data::Terrain::TerrainLight& raw,
                               const Data::Terrain::TerrainHeights& heights, std::int32_t centerX,
                               std::int32_t centerY);

// 0x5f8f40 with the world of `map`: reads TerrainHeight.OZB and TerrainLight.OZJ from its folder (Maps::WorldFolder)
// in `dataRoot` and shades them with ShadeTerrainLight. A file that is missing or unreadable is an error that names
// it.
[[nodiscard]] Core::Expected<std::unique_ptr<TerrainLighting>, std::string>
LoadTerrainLighting(const std::filesystem::path& dataRoot, std::uint8_t map);

// The first RGB block of 0x5fbf60: copies by bits the inclusive window up to lastX/lastY + 3, with both coordinates
// wrapped to a byte. The destination's other cells stay intact. Source and destination must be disjoint tables or the
// same table (a stable self-copy). The + 3 limits and the increment after the last index must fit in int32. It does
// not include the second, scalar block of waves, or set the initial state or the cadence.
void CopyTerrainLightWindow(const TerrainLighting& source, TerrainLighting& destination, std::int32_t firstX,
                            std::int32_t lastX, std::int32_t firstY, std::int32_t lastY);

// 0x5f9780: a radial sum in row, column, channel order, wrapping on both axes with a floor at +0; there is no ceiling
// at 1. color may reference a cell of destination: it is read again for each channel. The truncated center only bounds
// the sweep: the distance keeps the scaled float32 coordinates with their fraction. A non-positive weight does not
// touch the cell; an exact -0 is kept. Numeric model PC53/RN: separate double operations, float stores and no FMA
// contraction; it does not claim the effective CW of every run of the original. Requires a positive radius, finite
// coordinates whose product stored as float times 0.01 truncates to int32, center ± radius limits and a later increment
// representable in int32, and finite colors and updates.
void AddTerrainLight(TerrainLighting& destination, float worldX, float worldY,
                     const Data::Terrain::TerrainLightColor& color, std::int32_t radius);

// 0x5f99e0, the core over an explicit, stable table: scales x/y by 0.01f and stores F32; converts both to a truncated
// signed64 and keeps the signed low32, in X/Y order. A low32 outside [0, 254] is rejected with RGB +0; in the ordinary
// band it accepts −1 < scaled coordinate < 255, and also large coordinates whose low32 passes. Masked invalid projects
// 0 and keeps interpolating. The chosen portable profile: binary32/binary64, PC53/RN, no FMA; weights and columns stay
// in double, first Y and then X, up to the F32 store per channel. It does not set the native CW or reproduce x87 flags,
// traps or NaN payloads, and promises no equivalence for other PC/RC or its nested stack. Returns a value: it does not
// model out3 aliasing the table or stack, or interference between channels. The original's global guards belong to the
// binding; this API does not receive those globals.
[[nodiscard]] glm::vec3 LightAt(const TerrainLighting& lighting, float x, float y);
} // namespace Client::Terrain
