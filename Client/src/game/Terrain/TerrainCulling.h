#pragma once

#include "Core/Expected.h"
#include "Render/ModelBatch.h"
#include "Render/Pass3D.h"
#include "Terrain/TerrainCells.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <string>

// Which terrain cells pass the original's culling: the block range of 0x5fbb00 (the frustum's five points, the camera
// and the four far corners) and the trapezoid of 0x5fb570 around the hero, which 0x5fba90 tests at the center of each
// block (with a margin of −40, DAT_006c9d60) and of each cell. Checked: in Lorencia and in the selection it gives the
// cells of their traces, in their order (06 § Terreno). All in double where the x87 does not round and in float where
// it stores a float, with the constants by their bits.
namespace Client::Terrain
{
// The scene of DAT_006d5dc8, which picks the trapezoid: the login (2), the selection (4) and the game (5). In the
// others, 0x5fba90 lets everything pass.
enum class CullingScene : std::uint8_t
{
    Login = 2,
    CharacterSelect = 4,
    Game = 5,
    Other = 0,
};

// 0x5fbb00 with the camera of `pass` (its modelview, that of 0x620db0, which 0x620370 reads from GL) and `camera`: the
// far without the × 1.4 (DAT_006d5d44) and the fov (DAT_006d5d48). `widthFactor` is the first argument, the world
// width of 0x5bb8d0 (640 without windows) times 0.0015625.
[[nodiscard]] TerrainRange TerrainRangeOf(const glm::mat4& view, const Render::Camera3D& camera,
                                          float widthFactor = 1.0f);

// The trapezoid of 0x5fb570 in cells: far left, far right, near right, near left. The original's global (0x78ecbf0
// and 0x78ecc00) is Kernel::WorldRuntime::terrainPolygon; the default value, the zeros of the .bss.
struct TerrainPolygon
{
    std::array<float, 4> x{};
    std::array<float, 4> y{};
    // In scenes other than 2, 4 and 5, 0x5fba90 lets everything pass.
    bool everything = false;
};

// The persistent effective level (0x7b4ccb8), distinct from the camera's requested one. Its owner is
// Kernel::WorldRuntime, one for every scene; the DTO's 0 is the .bss's (*static*).
struct TerrainPolygonState
{
    std::int32_t effectiveLevel = 0;
};

// A snapshot without aliases: translation is argument 3, objectPosition is map 30's XY selector. The booleans are the
// full results of two independent name queries on map 6. Representable finite geometry; yaw used within
// OriginalRotation's domain. Selector 30 allows quiet NaN/inf and picks the small profile (06 § El trapecio completo).
struct TerrainPolygonInput
{
    std::int32_t scene = 5;
    std::int32_t map = 0;
    glm::vec3 translation{};
    glm::vec2 objectPosition{};
    float cameraYaw = 0.0f;
    std::int32_t worldWidth = 640;
    bool webzenProfile = false;
    bool webzenYaw = false;
};

struct TerrainPolygonCameraInput
{
    std::int16_t requestedLevel = 0;
    float distance = 1000.0f;
    float targetDistance = 1000.0f;
};

enum class TerrainPolygonError : std::uint8_t
{
    InvalidCameraDistance,
    InvalidEffectiveLevel,
};

struct TerrainPolygonFrame
{
    TerrainPolygonState nextState;
    TerrainPolygon polygon;
};

// One original frame: specials 6/30 hold without consuming the camera. In the normal case it validates D/meta in
// [-2^63, 2^63), truncates to int64 and compares the low 32 bits, signed; then it validates the chosen level 0..4. An
// error publishes no state or geometry (ADR 0009); the function does not change previous.
[[nodiscard]] Core::Expected<TerrainPolygonFrame, TerrainPolygonError>
TerrainPolygonFrameOf(const TerrainPolygonState& previous, const TerrainPolygonInput& input,
                      const TerrainPolygonCameraInput& camera);

// A query without a transition. Other computes arrays and only turns off the consumer's culling.
[[nodiscard]] Core::Expected<TerrainPolygon, TerrainPolygonError> TerrainPolygonOf(const TerrainPolygonInput& input,
                                                                                   std::int32_t effectiveLevel);

// 0x5fb570 around `hero` (DAT_0743a170: in the login, the (11679, 14191, 333) that 0x6350b0 writes on each frame): the
// scene's trapezoid, turned by the camera's −yaw on map 0x37 and 45° on the others. `screenWidth` is that of
// 0x5bb8d0. A wrapper of level 0 with both predicates false: it also uses hero as selector 30/5. Other keeps the
// legacy sentinel of arrays+0; no hidden state.
[[nodiscard]] TerrainPolygon TerrainPolygonOf(CullingScene scene, std::uint8_t map, const glm::vec3& hero,
                                              float cameraYaw, int screenWidth = 640);

// 0x5fba90(x, y, margin): passes if all four sides give more than `margin`.
[[nodiscard]] bool PolygonContains(const TerrainPolygon& polygon, double x, double y, double margin);

// The margin of each block (DAT_006c9d60, −40 in the .data, which nobody writes) and of each cell.
inline constexpr double BlockMargin = -40.0;
inline constexpr double CellMargin = 0.0;

// The terrain pass of 0x5fc190 and 0x5fc0e0 over the range: the 4 × 4 blocks by block row (firstY to lastY, by 4) and
// then by column (firstX to lastX); the one whose center (x + 2, y + 2) passes the trapezoid with BlockMargin, its 16
// cells by row and then by column, and the one whose center (x + 0.5, y + 0.5) passes, with DrawTerrainCell.
// DAT_07d36899, the top view that lets everything pass, is never turned on.
void DrawTerrain(Render::ModelBatch& batch, const TerrainView& view, const TerrainRange& range,
                 const TerrainPolygon& polygon, const TerrainWater& water);

// What 0x5fa830 does on starting, before checking the pass (0x5fa83f: 0x405e00(x, y)), with each cell 0x5fac90 does
// not skip for bit 0x08 of the .att.
using TerrainCellHook = std::function<Core::Expected<void, std::string>(std::uint8_t x, std::uint8_t y)>;

// DrawTerrain with `beforeCell` on each cell that is drawn, before drawing it. Stops with its error, without drawing
// that cell or the next ones.
[[nodiscard]] Core::Expected<void, std::string> DrawTerrain(Render::ModelBatch& batch, const TerrainView& view,
                                                            const TerrainRange& range, const TerrainPolygon& polygon,
                                                            const TerrainWater& water,
                                                            const TerrainCellHook& beforeCell);

// The grass pass (0x5fc353: 0x5fc190 with 0x7d0d31c at 2): the same walk of blocks and cells as DrawTerrain, with
// DrawGrassCell on each cell that passes. Stops with the error of the first cell that fails.
[[nodiscard]] Core::Expected<void, std::string> DrawGrass(Render::ModelBatch& batch, const TerrainView& view,
                                                          const TerrainRange& range, const TerrainPolygon& polygon,
                                                          const TerrainWater& water, const TerrainGrass& grass);
} // namespace Client::Terrain
