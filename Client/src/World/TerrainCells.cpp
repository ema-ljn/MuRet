#include "World/TerrainCells.h"

#include "Core/IClock.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Client::World
{
namespace
{
using Data::Terrain::TerrainCellCount;
using Data::Terrain::TerrainSide;

// Un índice de la grilla de una esquina. Con la fila 255, el de abajo se sale de la capa: el original
// lee la memoria que sigue; MuRet da la vuelta a la capa (la fila 0). Ninguna escena lo dibuja.
std::size_t Wrapped(std::size_t index)
{
    return index % TerrainCellCount;
}

// Las cuatro esquinas de la celda: (x, y), (x + 1, y), (x + 1, y + 1) y (x, y + 1), por índice
// (0x5fac90: DAT_078acbc4, DAT_078acbc8, DAT_078acbcc y DAT_078acbd0).
std::array<std::size_t, 4> CornersOf(std::uint8_t x, std::uint8_t y)
{
    const std::size_t first = (static_cast<std::size_t>(y) * TerrainSide) + x;
    const std::size_t below = first + TerrainSide;
    return {first, Wrapped(first + 1), Wrapped(below + 1), Wrapped(below)};
}

// El tile del agua (TileWater01).
constexpr std::uint8_t WaterTile = 5;
// El mapa donde el agua corre al revés (0x5fa5c0).
constexpr std::uint8_t ReversedWaterMap = 0x1e;
// La ola en la v: por 0,002 (0x6b89bc).
constexpr float WaveToV = 0.002f;
static_assert(std::bit_cast<std::uint32_t>(WaveToV) == 0x3B03126F, "la constante de 0x6b89bc");
// El corrimiento de 0x5fc260: 5e-5 (0x6b8a28) cada 20 s y, en el mapa 8, 2,5e-5 (0x6ba710) cada 40 s.
constexpr float WaterStep = std::bit_cast<float>(0x3851B717u);
constexpr float SlowWaterStep = std::bit_cast<float>(0x37D1B717u);
constexpr std::int64_t WaterPeriodMs = 20000;
constexpr std::int64_t SlowWaterPeriodMs = 40000;
constexpr std::uint8_t SlowWaterMap = 8;
// La ola de 0x5fbf60: sin(x · 5 (0x6b87f8; 50 en el mapa 8, 0x6b86b0) + (ftol(T) % 720000) · 0,002) · 10
// (0x6b87a8).
constexpr std::int64_t WavePeriodMs = 720000;
constexpr double WaveColumn = 5.0;
constexpr double SlowWaveColumn = 50.0;
constexpr double WaveHeight = 10.0;

std::int64_t Milliseconds(Core::Duration clock)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock).count();
}

// Las UV de las cuatro esquinas de un tile en la celda (x, y), como 0x5fa5c0: la u avanza 64 / ancho
// por celda y la v, 64 / alto. El agua corre la u y suma en la v la ola de la esquina (x, y) a las dos
// de la izquierda y la de (x + 1, y) a las dos de la derecha.
std::array<glm::vec2, 4> TileUvs(const TerrainView& view, std::uint8_t tile, std::uint8_t x, std::uint8_t y,
                                 const std::array<std::size_t, 4>& corners, const TerrainWater* water)
{
    const TileSize& size = view.tiles[tile];
    const float du = TileSpan / size.width;
    const float dv = TileSpan / size.height;
    float u = du * static_cast<float>(x);
    const float v = dv * static_cast<float>(y);
    if (water == nullptr)
    {
        const float u1 = u + du;
        const float v1 = v + dv;
        return {glm::vec2(u, v), glm::vec2(u1, v), glm::vec2(u1, v1), glm::vec2(u, v1)};
    }
    u = view.map == ReversedWaterMap && tile == WaterTile ? u - water->offset : water->offset + u;
    const float left = water->waves.empty() ? v : (water->waves[corners[0]] * WaveToV) + v;
    const float right = water->waves.empty() ? v : (water->waves[corners[1]] * WaveToV) + v;
    const float u1 = u + du;
    return {glm::vec2(u, left), glm::vec2(u1, right), glm::vec2(u1, right + dv), glm::vec2(u, left + dv)};
}

// Un pase de la celda: el fan de cuatro vértices en dos triángulos, con la textura del tile, la luz de
// cada esquina y, en el de alfa, su alfa (0x5fa260 y 0x5fa350).
void DrawPass(Render::ModelBatch& batch, const TerrainView& view, std::uint8_t tile, Render::ModelBlend blend,
              const std::array<glm::vec3, 4>& positions, const std::array<std::size_t, 4>& corners,
              const std::array<glm::vec2, 4>& uvs)
{
    batch.SetState(Render::ModelDrawState{
        .texture = Render::TextureSlot{static_cast<std::uint16_t>(FirstTileSlot + tile)}, .blend = blend});
    std::array<Render::ModelVertex, 4> vertices{};
    for (std::size_t k = 0; k < 4; ++k)
    {
        const auto& light = view.lighting->cells[corners[k]];
        const float alpha = blend == Render::ModelBlend::Alpha ? view.mapping->alpha[corners[k]] : 1.0f;
        vertices[k] = Render::ModelVertex{
            .position = positions[k], .uv = uvs[k], .color = Render::Color{light[0], light[1], light[2], alpha}};
    }
    batch.Triangle({vertices[0], vertices[1], vertices[2]});
    batch.Triangle({vertices[0], vertices[2], vertices[3]});
}
} // namespace

float WaterOffsetAt(Core::Duration clock, std::uint8_t map)
{
    const std::int64_t ms = Milliseconds(clock);
    if (map == SlowWaterMap)
    {
        return static_cast<float>(ms % SlowWaterPeriodMs) * SlowWaterStep;
    }
    return static_cast<float>(ms % WaterPeriodMs) * WaterStep;
}

void UpdateWaves(TerrainWater& water, Core::Duration clock, const TerrainRange& range, std::uint8_t map)
{
    if (water.waves.size() != TerrainCellCount)
    {
        water.waves.assign(TerrainCellCount, 0.0f);
    }
    const double phase = static_cast<double>(Milliseconds(clock) % WavePeriodMs) * static_cast<double>(WaveToV);
    const double column = map == SlowWaterMap ? SlowWaveColumn : WaveColumn;
    // La ventana de 0x5fbf60: tres celdas más que el rango en x y en y.
    constexpr int Window = 3;
    for (int y = range.firstY; y <= range.lastY + Window; ++y)
    {
        for (int x = range.firstX; x <= range.lastX + Window; ++x)
        {
            const auto cell = static_cast<std::size_t>((y * static_cast<int>(TerrainSide)) + x);
            if (cell < water.waves.size())
            {
                water.waves[cell] =
                    static_cast<float>(std::sin((static_cast<double>(x) * column) + phase) * WaveHeight);
            }
        }
    }
}

void DrawTerrainCell(Render::ModelBatch& batch, const TerrainView& view, const TerrainWater& water, std::uint8_t x,
                     std::uint8_t y)
{
    const std::array<std::size_t, 4> corners = CornersOf(x, y);
    if ((view.attributes->cells[corners[0]] & HiddenCellBit) != 0)
    {
        return;
    }
    // 0x5fac90: x · 100 e y · 100, la de la derecha y la de abajo 100 más, y la altura de cada esquina
    // (la fija con el bit 0x40).
    const float left = static_cast<float>(x) * TerrainCellSize;
    const float top = static_cast<float>(y) * TerrainCellSize;
    const float right = left + TerrainCellSize;
    const float bottom = top + TerrainCellSize;
    std::array<float, 4> heights{};
    for (std::size_t k = 0; k < 4; ++k)
    {
        heights[k] = (view.attributes->cells[corners[k]] & FixedHeightBit) != 0 ? FixedCornerHeight
                                                                                : view.heights->cells[corners[k]];
    }
    const std::array<glm::vec3, 4> positions{glm::vec3(left, top, heights[0]), glm::vec3(right, top, heights[1]),
                                             glm::vec3(right, bottom, heights[2]), glm::vec3(left, bottom, heights[3])};

    // 0x5fa830: con alguna esquina por debajo de 1, la capa 1 abajo y la 2 encima; si no, la 2 sola.
    bool someBelowOne = false;
    bool someAboveZero = false;
    for (const std::size_t corner : corners)
    {
        someBelowOne = someBelowOne || view.mapping->alpha[corner] < 1.0f;
        someAboveZero = someAboveZero || view.mapping->alpha[corner] > 0.0f;
    }
    const std::uint8_t base = someBelowOne ? view.mapping->layer1[corners[0]] : view.mapping->layer2[corners[0]];
    // local_8 de 0x5fa830: es agua la de abajo cuando es la capa 1 en 5; la de alfa, si además la 2 es 5.
    const bool baseIsWater = someBelowOne && base == WaterTile;
    DrawPass(batch, view, base, Render::ModelBlend::Opaque, positions, corners,
             TileUvs(view, base, x, y, corners, baseIsWater ? &water : nullptr));
    if (someBelowOne && someAboveZero)
    {
        const std::uint8_t upper = view.mapping->layer2[corners[0]];
        const bool upperIsWater = baseIsWater && upper == WaterTile;
        DrawPass(batch, view, upper, Render::ModelBlend::Alpha, positions, corners,
                 TileUvs(view, upper, x, y, corners, upperIsWater ? &water : nullptr));
    }
}

} // namespace Client::World
