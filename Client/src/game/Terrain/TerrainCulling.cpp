#include "Terrain/TerrainCulling.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Maps/MapId.h"
#include "Math/X87.h"
#include "Render/ModelBatch.h"
#include "Math/OriginalRotation.h"
#include "Render/Pass3D.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/SphereFrustum.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Terrain
{
namespace
{
using Math::F32;

// 0x5fbb00: the −0.01 of the ftol calls (0x6ba110).
constexpr float ToCells = std::bit_cast<float>(0xBC23D70Au);
constexpr int LastBlock = 0xfb;

// 0x5fb570, level 0: the width times k (0x6ba6d8 in the login, 0x6ba6d4 in the selection, 0x6ba070 in the game), the
// far K (0x6ba6d0, 0x6ba6cc, 0x6ba6c8), near and the target times 0.19 and 0.47 (0x6ba364 and 0x6ba6c4) and the far and
// near widths times 1,190 and 540 (0x6ba6c0 and 0x6ba000).
constexpr float LoginWidthFactor = std::bit_cast<float>(0x3E233333u);
constexpr float LoginFar = std::bit_cast<float>(0x486F1000u);
constexpr float SelectionWidthFactor = std::bit_cast<float>(0x3C137469u);
constexpr float SelectionFar = std::bit_cast<float>(0x4657FF86u);
constexpr float GameWidthFactor = std::bit_cast<float>(0x3ACCCCCDu);
constexpr float GameFar = std::bit_cast<float>(0x45160000u);
constexpr float NearFactor = std::bit_cast<float>(0x3E428F5Cu);
constexpr float TargetFactor = std::bit_cast<float>(0x3EF0A3D7u);
constexpr float FarWidth = std::bit_cast<float>(0x4494C000u);
constexpr float NearWidth = std::bit_cast<float>(0x44070000u);
// 0x5fb570: a fixed 45° (0x5fb9e8), or −yaw on map 55 and map 6/webzenYaw. The cells times 0.01 (0x6b88d4);
// OriginalRotation keeps the existing angular contract.
constexpr float DefaultAngle = 45.0f;
using Maps::LoginMap;
constexpr std::int32_t WebzenMap = 6;
constexpr std::int32_t CastleMap = Maps::CastleSiegeMap;
constexpr std::int32_t LoginScene = 2;
constexpr std::int32_t SelectionScene = 4;
constexpr std::int32_t GameScene = 5;
constexpr float LevelWidthAddition = std::bit_cast<float>(0x3DCCCCCDu);
constexpr float CellsPerUnit = std::bit_cast<float>(0x3C23D70Au);

// 06 § El trapecio completo: the specials skip the conversion and the dispatch by level.
enum class SpecialProfile : std::uint8_t
{
    Normal,
    Webzen,
    CastleLarge,
    CastleSmall,
};

SpecialProfile SpecialProfileOf(const TerrainPolygonInput& input)
{
    if (input.map == WebzenMap && input.webzenProfile)
    {
        return SpecialProfile::Webzen;
    }
    if (input.map != CastleMap || input.scene != GameScene)
    {
        return SpecialProfile::Normal;
    }
    constexpr float Left = 16100.0f;
    constexpr float Right = 19000.0f;
    constexpr float Bottom = 18900.0f;
    constexpr float Top = 21700.0f;
    constexpr float CenterLeft = 17100.0f;
    constexpr float CenterRight = 18300.0f;
    const float x = input.objectPosition.x;
    const float y = input.objectPosition.y;
    const bool large = x >= Left && x <= Right && y >= Bottom && y <= Top && (x < CenterLeft || x > CenterRight);
    return large ? SpecialProfile::CastleLarge : SpecialProfile::CastleSmall;
}

struct PolygonProfile
{
    float far;
    float target;
    float near;
    float farWidth;
    float nearWidth;
};

PolygonProfile SpecialDimensions(SpecialProfile profile)
{
    switch (profile)
    {
    case SpecialProfile::Webzen:
        return {8500.0f, 3995.0f, 425.0f, 3000.0f, 540.0f};
    case SpecialProfile::CastleLarge:
        return {5100.0f, 2397.0f, 969.0f, 2250.0f, 540.0f};
    case SpecialProfile::CastleSmall:
        return {3300.0f, 1551.0f, 627.0f, 1300.0f, 580.0f};
    case SpecialProfile::Normal:
        break; // DimensionsOf already split off the normal dispatch.
    }
    return {};
}

PolygonProfile LevelZeroDimensions(std::int32_t scene, std::int32_t worldWidth)
{
    const float far = scene == LoginScene ? LoginFar : (scene == SelectionScene ? SelectionFar : GameFar);
    const float factor =
        scene == LoginScene ? LoginWidthFactor : (scene == SelectionScene ? SelectionWidthFactor : GameWidthFactor);
    const float width = F32(static_cast<double>(worldWidth) * static_cast<double>(factor));
    return {far, F32(static_cast<double>(far) * TargetFactor), F32(static_cast<double>(far) * NearFactor),
            F32(static_cast<double>(width) * FarWidth), F32(static_cast<double>(width) * NearWidth)};
}

Core::Expected<PolygonProfile, TerrainPolygonError> DimensionsOf(const TerrainPolygonInput& input,
                                                                 std::int32_t effectiveLevel, SpecialProfile special)
{
    if (special != SpecialProfile::Normal)
    {
        PolygonProfile profile = SpecialDimensions(special);
        const float width = F32(static_cast<double>(input.worldWidth) * GameWidthFactor);
        profile.farWidth = F32(static_cast<double>(width) * profile.farWidth);
        profile.nearWidth = F32(static_cast<double>(width) * profile.nearWidth);
        return profile;
    }
    constexpr std::int32_t LastLevel = 4;
    if (effectiveLevel < 0 || effectiveLevel > LastLevel)
    {
        return Core::Unexpected(TerrainPolygonError::InvalidEffectiveLevel);
    }
    if (effectiveLevel == 0)
    {
        return LevelZeroDimensions(input.scene, input.worldWidth);
    }
    // Immediate F/T/N; q stores AFTER adding 0.1f to the product, still in 53 bits.
    constexpr std::array<PolygonProfile, LastLevel> Levels{{{2700.0f, 1269.0f, 513.0f, 1200.0f, 540.0f},
                                                            {3000.0f, 1410.0f, 570.0f, 1300.0f, 540.0f},
                                                            {3300.0f, 1551.0f, 627.0f, 1500.0f, 580.0f},
                                                            {3400.0f, 1598.0f, 646.0f, 1600.0f, 660.0f}}};
    PolygonProfile profile = Levels[static_cast<std::size_t>(effectiveLevel - 1)];
    const float q = F32((static_cast<double>(input.worldWidth) * GameWidthFactor) + LevelWidthAddition);
    profile.farWidth = F32(static_cast<double>(q) * profile.farWidth);
    profile.nearWidth = F32(static_cast<double>(q) * profile.nearWidth);
    return profile;
}

TerrainPolygon PolygonOf(const TerrainPolygonInput& input, const PolygonProfile& profile)
{
    const float farY = F32(static_cast<double>(profile.far) - profile.target);
    const float nearY = F32(static_cast<double>(profile.near) - profile.target);
    const std::array<glm::vec3, 4> corners{
        glm::vec3(-profile.farWidth, farY, 0.0f), glm::vec3(profile.farWidth, farY, 0.0f),
        glm::vec3(profile.nearWidth, nearY, 0.0f), glm::vec3(-profile.nearWidth, nearY, 0.0f)};
    const bool negativeYaw = (input.map == WebzenMap && input.webzenYaw) || input.map == LoginMap;
    const float angle = negativeYaw ? -input.cameraYaw : DefaultAngle;
    const auto matrix = Math::OriginalAngleMatrix({0.0f, 0.0f, angle});
    TerrainPolygon polygon;
    polygon.everything = input.scene != LoginScene && input.scene != SelectionScene && input.scene != GameScene;
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        const auto rotated = Math::OriginalVectorRotate(corners[i], matrix);
        // 0x5fba22..0x5fba49 stores XYZ before loading and scaling XY again (0x5fba4d..0x5fba68).
        const glm::vec3 translated{F32(static_cast<double>(rotated.x) + input.translation.x),
                                   F32(static_cast<double>(rotated.y) + input.translation.y),
                                   F32(static_cast<double>(rotated.z) + input.translation.z)};
        polygon.x[i] = F32(static_cast<double>(translated.x) * CellsPerUnit);
        polygon.y[i] = F32(static_cast<double>(translated.y) * CellsPerUnit);
    }
    return polygon;
}

bool IsCameraDistanceValid(float distance)
{
    // +2^63 does not fit: INT64_MAX converted to float/double rounds to that same limit.
    constexpr double Int64Limit = 0x1p63;
    return std::isfinite(distance) && static_cast<double>(distance) >= -Int64Limit &&
           static_cast<double>(distance) < Int64Limit;
}

std::int32_t LowDwordOf(float distance)
{
    // Precondition: IsCameraDistanceValid. __ftol truncates to int64; CMP uses only EAX, signed.
    const auto converted = static_cast<std::int64_t>(distance);
    const auto low = static_cast<std::uint32_t>(converted);
    return std::bit_cast<std::int32_t>(low);
}

// `cdq; and edx, 3; add eax, edx; sar eax, 2`: C's division by 4, which truncates toward 0.
int DivideBy4(int value)
{
    return value / 4;
}

// ftol(v · −0.01), with the product in 53 bits.
int CellsOf(float value)
{
    return static_cast<int>(static_cast<double>(value) * static_cast<double>(ToCells));
}

int ClampBlock(int value)
{
    return std::clamp(value, 0, LastBlock);
}
} // namespace

TerrainRange TerrainRangeOf(const glm::mat4& view, const Render::Camera3D& camera, float widthFactor)
{
    float minX = 25600.0f;
    float minY = 25600.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    for (const glm::vec3& point : FrustumPointsOf(view, camera, widthFactor))
    {
        const float x = point.x;
        const float y = point.y;
        minX = minX > x ? x : minX;
        minY = minY > y ? y : minY;
        maxX = maxX < x ? x : maxX;
        maxY = maxY < y ? y : maxY;
    }
    return TerrainRange{.firstX = ClampBlock((-1 - DivideBy4(CellsOf(minX))) * 4),
                        .lastX = ClampBlock(4 - (DivideBy4(CellsOf(maxX)) * 4)),
                        .firstY = ClampBlock((-1 - DivideBy4(CellsOf(minY))) * 4),
                        .lastY = ClampBlock(4 - (DivideBy4(CellsOf(maxY)) * 4))};
}

Core::Expected<TerrainPolygonFrame, TerrainPolygonError> TerrainPolygonFrameOf(const TerrainPolygonState& previous,
                                                                               const TerrainPolygonInput& input,
                                                                               const TerrainPolygonCameraInput& camera)
{
    const SpecialProfile special = SpecialProfileOf(input);
    TerrainPolygonState next = previous;
    if (special == SpecialProfile::Normal)
    {
        if (!IsCameraDistanceValid(camera.distance) || !IsCameraDistanceValid(camera.targetDistance))
        {
            return Core::Unexpected(TerrainPolygonError::InvalidCameraDistance);
        }
        const std::int32_t distance = LowDwordOf(camera.distance);
        const std::int32_t target = LowDwordOf(camera.targetDistance);
        if (target >= distance)
        {
            next.effectiveLevel = camera.requestedLevel;
        }
    }
    const auto profile = DimensionsOf(input, next.effectiveLevel, special);
    if (!profile.HasValue())
    {
        return Core::Unexpected(profile.Error());
    }
    return TerrainPolygonFrame{next, PolygonOf(input, profile.Value())};
}

Core::Expected<TerrainPolygon, TerrainPolygonError> TerrainPolygonOf(const TerrainPolygonInput& input,
                                                                     std::int32_t effectiveLevel)
{
    const auto profile = DimensionsOf(input, effectiveLevel, SpecialProfileOf(input));
    if (!profile.HasValue())
    {
        return Core::Unexpected(profile.Error());
    }
    return PolygonOf(input, profile.Value());
}

TerrainPolygon TerrainPolygonOf(CullingScene scene, std::uint8_t map, const glm::vec3& hero, float cameraYaw,
                                int screenWidth)
{
    if (scene == CullingScene::Other)
    {
        TerrainPolygon sentinel;
        sentinel.everything = true;
        return sentinel;
    }
    TerrainPolygonInput input;
    input.scene = static_cast<std::int32_t>(scene);
    input.map = map;
    input.translation = hero;
    input.objectPosition = {hero.x, hero.y};
    input.cameraYaw = cameraYaw;
    input.worldWidth = screenWidth;
    return TerrainPolygonOf(input, 0).Value();
}

bool PolygonContains(const TerrainPolygon& polygon, double x, double y, double margin)
{
    if (polygon.everything)
    {
        return true;
    }
    std::size_t j = 3;
    for (std::size_t i = 0; i < 4; ++i)
    {
        const double c = ((static_cast<double>(polygon.y[j]) - y) * (static_cast<double>(polygon.x[i]) - x)) -
                         ((static_cast<double>(polygon.x[j]) - x) * (static_cast<double>(polygon.y[i]) - y));
        if (!(c > margin))
        {
            return false;
        }
        j = i;
    }
    return true;
}

namespace
{
// The 16 cells of a block that passes (0x5fc0e0), by row and then by column: the one whose center passes the
// trapezoid goes to `cell`. Stops at the first error.
template <typename Cell>
Core::Expected<void, std::string> VisitTheBlock(const TerrainPolygon& polygon, int blockX, int blockY, Cell& cell)
{
    constexpr int Block = 4;
    constexpr double CellCenter = 0.5;
    for (int y = blockY; y < blockY + Block; ++y)
    {
        for (int x = blockX; x < blockX + Block; ++x)
        {
            if (!PolygonContains(polygon, static_cast<double>(x) + CellCenter, static_cast<double>(y) + CellCenter,
                                 CellMargin))
            {
                continue;
            }
            if (auto drawn = cell(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y)); !drawn.HasValue())
            {
                return drawn;
            }
        }
    }
    return {};
}

// 0x5fc190, the same in both passes (it does not check 0x7d0d31c): the range's 4 × 4 blocks by block row and then by
// column, and the cells of the one whose center passes the trapezoid.
template <typename Cell>
Core::Expected<void, std::string> VisitTheRange(const TerrainRange& range, const TerrainPolygon& polygon, Cell cell)
{
    constexpr int Block = 4;
    constexpr double BlockCenter = 2.0;
    // TerrainRangeOf already leaves them between 0 and 0xfb; one outside would make the cells wrap around.
    const int firstY = std::clamp(range.firstY, 0, LastBlock);
    const int lastY = std::clamp(range.lastY, 0, LastBlock);
    const int firstX = std::clamp(range.firstX, 0, LastBlock);
    const int lastX = std::clamp(range.lastX, 0, LastBlock);
    for (int blockY = firstY; blockY <= lastY; blockY += Block)
    {
        for (int blockX = firstX; blockX <= lastX; blockX += Block)
        {
            if (!PolygonContains(polygon, static_cast<double>(static_cast<float>(blockX)) + BlockCenter,
                                 static_cast<double>(static_cast<float>(blockY)) + BlockCenter, BlockMargin))
            {
                continue;
            }
            if (auto visited = VisitTheBlock(polygon, blockX, blockY, cell); !visited.HasValue())
            {
                return visited;
            }
        }
    }
    return {};
}
} // namespace

void DrawTerrain(Render::ModelBatch& batch, const TerrainView& view, const TerrainRange& range,
                 const TerrainPolygon& polygon, const TerrainWater& water)
{
    // The pass with 0x7d0d31c at 0 does not fail: each cell that passes is drawn.
    static_cast<void>(VisitTheRange(range, polygon,
                                    [&](std::uint8_t x, std::uint8_t y) -> Core::Expected<void, std::string>
                                    {
                                        DrawTerrainCell(batch, view, water, x, y);
                                        return {};
                                    }));
}

Core::Expected<void, std::string> DrawTerrain(Render::ModelBatch& batch, const TerrainView& view,
                                              const TerrainRange& range, const TerrainPolygon& polygon,
                                              const TerrainWater& water, const TerrainCellHook& beforeCell)
{
    return VisitTheRange(range, polygon,
                         [&](std::uint8_t x, std::uint8_t y) -> Core::Expected<void, std::string>
                         {
                             // 0x5facb6..0x5facce: a cell with bit 0x08 leaves without calling 0x5fa830.
                             if ((view.attributes->cells[Data::Terrain::CellIndex(x, y)] & HiddenCellBit) != 0)
                                 return {};
                             if (beforeCell)
                             {
                                 if (auto before = beforeCell(x, y); !before.HasValue())
                                     return before;
                             }
                             DrawTerrainCell(batch, view, water, x, y);
                             return {};
                         });
}

Core::Expected<void, std::string> DrawGrass(Render::ModelBatch& batch, const TerrainView& view,
                                            const TerrainRange& range, const TerrainPolygon& polygon,
                                            const TerrainWater& water, const TerrainGrass& grass)
{
    return VisitTheRange(range, polygon, [&](std::uint8_t x, std::uint8_t y)
                         { return DrawGrassCell(batch, view, water, grass, x, y); });
}
} // namespace Client::Terrain
