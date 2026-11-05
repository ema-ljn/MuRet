#include "World/TerrainCulling.h"

#include "Render/ModelBatch.h"
#include "Render/Pass3D.h"
#include "World/TerrainCells.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
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
// 0x5fb570: 45° fijos (0x5fb9e8), o −yaw de la cámara en el mapa 0x37; π/180 en float (0x6ba360) y
// las celdas por 0,01 (0x6b88d4).
constexpr float DefaultAngle = 45.0f;
constexpr std::uint8_t LoginMap = 0x37;
constexpr float Degrees = std::bit_cast<float>(0x3C8EFA35u);
constexpr float CellsPerUnit = std::bit_cast<float>(0x3C23D70Au);

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
} // namespace

TerrainRange TerrainRangeOf(const glm::mat4& view, const Render::Camera3D& camera, float widthFactor)
{
    const float depth = F32(static_cast<double>(camera.farPlane) * static_cast<double>(FarFactor));
    const double halfWidth = (std::tan(static_cast<double>(camera.fieldOfView) * static_cast<double>(HalfFieldOfView)) *
                              static_cast<double>(depth) * static_cast<double>(widthFactor)) +
                             static_cast<double>(WidthMargin);
    const float w = F32(halfWidth);
    const float h = F32(halfWidth * static_cast<double>(HeightFactor));
    const std::array<glm::vec3, 5> local{glm::vec3(0.0f), glm::vec3(-w, h, -depth), glm::vec3(w, h, -depth),
                                         glm::vec3(w, -h, -depth), glm::vec3(-w, -h, -depth)};
    // 0x620370 da la modelview por filas (m[i · 4 + j] = M[i][j]; glm la guarda por columnas) y 0x5fcc30
    // la aplica transpuesta: el punto de la vista en el mundo, sin la traslación, más la cámara.
    const auto m = [&view](int row, int column) { return static_cast<double>(view[column][row]); };
    float minX = 25600.0f;
    float minY = 25600.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    for (const glm::vec3& p : local)
    {
        const double vx = p.x;
        const double vy = p.y;
        const double vz = p.z;
        const float ox = F32(((m(2, 0) * vz) + (m(1, 0) * vy)) + (vx * m(0, 0)));
        const float oy = F32(((m(0, 1) * vx) + (m(2, 1) * vz)) + (m(1, 1) * vy));
        const float x = F32(static_cast<double>(camera.position.x) + static_cast<double>(ox));
        const float y = F32(static_cast<double>(camera.position.y) + static_cast<double>(oy));
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

TerrainPolygon TerrainPolygonOf(CullingScene scene, std::uint8_t map, const glm::vec3& hero, float cameraYaw,
                                int screenWidth)
{
    TerrainPolygon polygon;
    if (scene == CullingScene::Other)
    {
        polygon.everything = true;
        return polygon;
    }
    float widthFactor = GameWidthFactor;
    float far = GameFar;
    if (scene == CullingScene::Login)
    {
        widthFactor = LoginWidthFactor;
        far = LoginFar;
    }
    else if (scene == CullingScene::CharacterSelect)
    {
        widthFactor = SelectionWidthFactor;
        far = SelectionFar;
    }
    const float a = F32(static_cast<double>(screenWidth) * static_cast<double>(widthFactor));
    const float nearDistance = F32(static_cast<double>(far) * static_cast<double>(NearFactor));
    const float target = F32(static_cast<double>(far) * static_cast<double>(TargetFactor));
    const float farWidth = F32(static_cast<double>(a) * static_cast<double>(FarWidth));
    const float nearWidth = F32(static_cast<double>(a) * static_cast<double>(NearWidth));
    const float farY = F32(static_cast<double>(far) - static_cast<double>(target));
    const float nearY = F32(static_cast<double>(nearDistance) - static_cast<double>(target));
    const std::array<glm::vec2, 4> corners{glm::vec2(-farWidth, farY), glm::vec2(farWidth, farY),
                                           glm::vec2(nearWidth, nearY), glm::vec2(-nearWidth, nearY)};
    // 0x5fc8f0 con (0, 0, ángulo): solo el yaw; cos(yaw) queda en la x87 y el seno se guarda en float.
    const float angle = map == LoginMap ? -cameraYaw : DefaultAngle;
    const double radians = static_cast<double>(angle) * static_cast<double>(Degrees);
    const float sinYaw = F32(std::sin(radians));
    const double cosYaw = std::cos(radians);
    const float m00 = F32(cosYaw);
    const float m10 = F32(sinYaw);
    const float m01 = F32(-static_cast<double>(sinYaw));
    const float m11 = F32(cosYaw);
    for (std::size_t k = 0; k < corners.size(); ++k)
    {
        // 0x5fcbd0: la fila por el punto (z = 0).
        const double vx = corners[k].x;
        const double vy = corners[k].y;
        const float ox = F32((static_cast<double>(m01) * vy) + (vx * static_cast<double>(m00)));
        const float oy = F32((static_cast<double>(m10) * vx) + (static_cast<double>(m11) * vy));
        polygon.x[k] = F32(static_cast<double>(F32(static_cast<double>(ox) + static_cast<double>(hero.x))) *
                           static_cast<double>(CellsPerUnit));
        polygon.y[k] = F32(static_cast<double>(F32(static_cast<double>(oy) + static_cast<double>(hero.y))) *
                           static_cast<double>(CellsPerUnit));
    }
    return polygon;
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
    for (int blockY = range.firstY; blockY <= range.lastY; blockY += Block)
    {
        for (int blockX = range.firstX; blockX <= range.lastX; blockX += Block)
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
