#include "World/TerrainCulling.h"

#include "Core/Expected.h"
#include "Render/ModelBatch.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Pass3D.h"
#include "World/Ftol.h"
#include "World/TerrainCells.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Client::World
{
namespace
{
float F32(double value)
{
    return static_cast<float>(value);
}

// 0x5fbb00: el far por 0,9 (0x6b8a1c), el medio fov (0x6ba70c, casi π/360), el 100 (0x6b86b4), el
// alto por 0,75 (0x6ba36c) y el −0,01 de los ftol (0x6ba110).
constexpr float FarFactor = std::bit_cast<float>(0x3F666666u);
constexpr float HalfFieldOfView = std::bit_cast<float>(0x3C0EFA33u);
constexpr float WidthMargin = 100.0f;
constexpr float HeightFactor = 0.75f;
constexpr float ToCells = std::bit_cast<float>(0xBC23D70Au);
constexpr int LastBlock = 0xfb;

// 0x5fb570, nivel 0: el ancho por k (0x6ba6d8 en el login, 0x6ba6d4 en la selección, 0x6ba070 en el
// juego), lo lejos K (0x6ba6d0, 0x6ba6cc, 0x6ba6c8), cerca y el objetivo por 0,19 y 0,47 (0x6ba364 y
// 0x6ba6c4) y los anchos lejos y cerca por 1.190 y 540 (0x6ba6c0 y 0x6ba000).
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
// 0x5fb570: 45° fijos (0x5fb9e8), o −yaw en mapa55 y mapa6/webzenYaw.
// Las celdas por 0,01 (0x6b88d4); OriginalRotation conserva el contrato angular existente.
constexpr float DefaultAngle = 45.0f;
constexpr std::int32_t LoginMap = 55;
constexpr std::int32_t WebzenMap = 6;
constexpr std::int32_t CastleMap = 30;
constexpr std::int32_t LoginScene = 2;
constexpr std::int32_t SelectionScene = 4;
constexpr std::int32_t GameScene = 5;
constexpr float LevelWidthAddition = std::bit_cast<float>(0x3DCCCCCDu);
constexpr float CellsPerUnit = std::bit_cast<float>(0x3C23D70Au);

// 06 § El trapecio completo: los especiales omiten la conversión y el dispatch por nivel.
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
        break; // DimensionsOf ya separó el dispatch normal.
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
    // F/T/N inmediatos; q guarda DESPUÉS de sumar 0,1f al producto aún en 53 bits.
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
    const auto matrix = Render::Models::OriginalAngleMatrix({0.0f, 0.0f, angle});
    TerrainPolygon polygon;
    polygon.everything = input.scene != LoginScene && input.scene != SelectionScene && input.scene != GameScene;
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        const auto rotated = Render::Models::OriginalVectorRotate(corners[i], matrix);
        // 0x5fba22..49 guarda XYZ antes de volver a cargar y escalar XY (0x5fba4d..68).
        const glm::vec3 translated{F32(static_cast<double>(rotated.x) + input.translation.x),
                                   F32(static_cast<double>(rotated.y) + input.translation.y),
                                   F32(static_cast<double>(rotated.z) + input.translation.z)};
        polygon.x[i] = F32(static_cast<double>(translated.x) * CellsPerUnit);
        polygon.y[i] = F32(static_cast<double>(translated.y) * CellsPerUnit);
    }
    return polygon;
}

// `cdq; and edx, 3; add eax, edx; sar eax, 2`: la división por 4 de C, que trunca hacia el 0.
int DivideBy4(int value)
{
    return value / 4;
}

// ftol(v · −0,01), con el producto en 53 bits.
int CellsOf(float value)
{
    return static_cast<int>(static_cast<double>(value) * static_cast<double>(ToCells));
}

int ClampBlock(int value)
{
    return std::clamp(value, 0, LastBlock);
}
// 5fbb00: la misma geometría de TerrainRangeOf, ahora conserva también Z para los cinco planos.
std::array<glm::vec3, 5> ModelEffectPointsOf(const glm::mat4& view, const Render::Camera3D& camera, float widthFactor)
{
    const float depth = F32(static_cast<double>(camera.farPlane) * static_cast<double>(FarFactor));
    const double halfWidth = (std::tan(static_cast<double>(camera.fieldOfView) * static_cast<double>(HalfFieldOfView)) *
                              static_cast<double>(depth) * static_cast<double>(widthFactor)) +
                             static_cast<double>(WidthMargin);
    const float w = F32(halfWidth);
    const float h = F32(halfWidth * static_cast<double>(HeightFactor));
    const std::array<glm::vec3, 5> local{glm::vec3(0.0f), glm::vec3(-w, h, -depth), glm::vec3(w, h, -depth),
                                         glm::vec3(w, -h, -depth), glm::vec3(-w, -h, -depth)};
    Render::Models::OriginalMatrix matrix{};
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            matrix[static_cast<std::size_t>((row * 4) + column)] = view[column][row];
        }
    }
    std::array<glm::vec3, 5> points{};
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const glm::vec3 offset = Render::Models::OriginalVectorIRotate(local[i], matrix);
        points[i].x = F32(static_cast<double>(camera.position.x) + offset.x);
        points[i].y = F32(static_cast<double>(camera.position.y) + offset.y);
        points[i].z = F32(static_cast<double>(camera.position.z) + offset.z);
    }
    return points;
}
// 5fd000: Uy/Vz/Uz y Nz permanecen en 53 bits; Vy/Vx/Ux/Nx/Ny tienen store F32.
void UpdateModelEffectNormal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, glm::vec3& normal)
{
    const double uy = static_cast<double>(b.y) - a.y;
    const double vz = static_cast<double>(c.z) - a.z;
    const double uz = static_cast<double>(b.z) - a.z;
    const float vy = F32(static_cast<double>(c.y) - a.y);
    const float nx = F32((vz * uy) - (static_cast<double>(vy) * uz));
    const float vx = F32(static_cast<double>(c.x) - a.x);
    const float ux = F32(static_cast<double>(b.x) - a.x);
    const float ny = F32((static_cast<double>(vx) * uz) - (static_cast<double>(ux) * vz));
    const double nz = (static_cast<double>(ux) * vy) - (static_cast<double>(vx) * uy);
    const double z2 = nz * nz;
    const double y2 = static_cast<double>(ny) * ny;
    const double x2 = static_cast<double>(nx) * nx;
    const double length = std::sqrt((z2 + y2) + x2);
    // C3: igualdad y unordered no escriben los 12 B; no inventar normal cero ni epsilon.
    if (length == 0.0 || std::isnan(length))
    {
        return;
    }
    normal.x = F32(static_cast<double>(nx) / length);
    normal.y = F32(static_cast<double>(ny) / length);
    normal.z = F32(nz / length);
}
} // namespace

std::uint8_t ModelEffectVisibility(const ModelEffectPlanes& planes, const glm::vec3& position, float margin)
{
    const float threshold = -margin;
    for (std::size_t i = 0; i < planes.normals.size(); ++i)
    {
        const glm::vec3& normal = planes.normals[i];
        const double x = static_cast<double>(normal.x) * position.x;
        const double z = static_cast<double>(normal.z) * position.z;
        const double xz = x + z;
        const double y = static_cast<double>(normal.y) * position.y;
        const double distance = (xz + y) + planes.distances[i];
        if (!(distance >= threshold))
        {
            return 0;
        }
    }
    return 1;
}

ModelEffectPlanes ModelEffectPlanesOf(const ModelEffectPlanes& previous, const glm::mat4& view,
                                      const Render::Camera3D& camera, float widthFactor)
{
    const auto points = ModelEffectPointsOf(view, camera, widthFactor);
    ModelEffectPlanes planes = previous;
    UpdateModelEffectNormal(points[0], points[1], points[2], planes.normals[0]);
    UpdateModelEffectNormal(points[0], points[2], points[3], planes.normals[1]);
    UpdateModelEffectNormal(points[0], points[3], points[4], planes.normals[2]);
    UpdateModelEffectNormal(points[0], points[4], points[1], planes.normals[3]);
    UpdateModelEffectNormal(points[3], points[2], points[1], planes.normals[4]);
    for (std::size_t i = 0; i < planes.normals.size(); ++i)
    {
        const glm::vec3& anchor = points[i < 4 ? 0 : 1];
        const glm::vec3& normal = planes.normals[i];
        const double z = static_cast<double>(normal.z) * anchor.z;
        const double y = static_cast<double>(normal.y) * anchor.y;
        const double x = static_cast<double>(normal.x) * anchor.x;
        planes.distances[i] = F32(-((z + y) + x));
    }
    return planes;
}

TerrainRange TerrainRangeOf(const glm::mat4& view, const Render::Camera3D& camera, float widthFactor)
{
    const auto points = ModelEffectPointsOf(view, camera, widthFactor);
    float minX = 25600.0f;
    float minY = 25600.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    for (const glm::vec3& point : points)
    {
        minX = minX > point.x ? point.x : minX;
        minY = minY > point.y ? point.y : minY;
        maxX = maxX < point.x ? point.x : maxX;
        maxY = maxY < point.y ? point.y : maxY;
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
        // __ftol trunca a int64 y el CMP consume sólo EAX con signo.
        const auto distance = FtolLowChecked(static_cast<double>(camera.distance));
        const auto target = FtolLowChecked(static_cast<double>(camera.targetDistance));
        if (!distance || !target)
        {
            return Core::Unexpected(TerrainPolygonError::InvalidCameraDistance);
        }
        if (*target >= *distance)
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

void DrawTerrain(Render::ModelBatch& batch, const TerrainView& view, const TerrainRange& range,
                 const TerrainPolygon& polygon, const TerrainWater& water)
{
    constexpr int Block = 4;
    constexpr double BlockCenter = 2.0;
    constexpr double CellCenter = 0.5;
    // TerrainRangeOf ya los deja entre 0 y 0xfb; uno de afuera haría dar la vuelta a las celdas.
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
            for (int y = blockY; y < blockY + Block; ++y)
            {
                for (int x = blockX; x < blockX + Block; ++x)
                {
                    if (PolygonContains(polygon, static_cast<double>(x) + CellCenter,
                                        static_cast<double>(y) + CellCenter, CellMargin))
                    {
                        DrawTerrainCell(batch, view, water, static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y));
                    }
                }
            }
        }
    }
}
} // namespace Client::World
