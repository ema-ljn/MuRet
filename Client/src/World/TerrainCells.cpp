#include "World/TerrainCells.h"

#include "Core/Expected.h"
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
#include <format>
#include <optional>
#include <string>

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

// El pase del pasto (0x5fa9f6..0x5fac7b): la u de 0,25 por celda (0x6b89e0), la x de arriba 50 menos
// (0x6b86b0) y la v de 0 arriba y 1 abajo; los mapas del Blood Castle no lo dibujan (0x5faa79).
constexpr float GrassStep = 0.25f;
static_assert(std::bit_cast<std::uint32_t>(GrassStep) == 0x3E800000, "la constante de 0x6b89e0");
constexpr float GrassLean = 50.0f;
static_assert(std::bit_cast<std::uint32_t>(GrassLean) == 0x42480000, "la constante de 0x6b86b0");
constexpr float GrassTop = 0.0f;
constexpr float GrassBottom = 1.0f;
constexpr std::uint8_t FirstWithoutGrassCellsMap = 11;
constexpr std::uint8_t LastWithoutGrassCellsMap = 17;
// El primer slot que 0x61a560 no deja vacío después del pasto: las wt00..31 de 0x616ec0 (0x41..0x60).
constexpr std::uint16_t FirstUnknownGrassSlot = 0x41;

std::int64_t Milliseconds(Core::Duration clock)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock).count();
}

// La altura de una esquina (0x5fac90): la fija con el bit 0x40, la del .OZB si no.
float CornerHeight(const TerrainView& view, std::size_t corner)
{
    return (view.attributes->cells[corner] & FixedHeightBit) != 0 ? view.fixedHeight : view.heights->cells[corner];
}

// La ola de una esquina (0x78acbd8): sin olas, 0, como la .bss.
float WaveAt(const TerrainWater& water, std::size_t corner)
{
    return water.waves.size() == TerrainCellCount ? water.waves[corner] : 0.0f;
}

// Un fld de un float, un fadd de otro y el fstp a un float: la suma en double, redondeada una vez.
float StoredSum(float first, float second)
{
    return static_cast<float>(static_cast<double>(first) + static_cast<double>(second));
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
    // 0x5fa7ab..0x5fa7c9: la ola de la esquina 1 pasa por un float antes de sumarse; la de la 0 se suma a
    // la v desde la x87, sin redondear el producto.
    const bool waves = water->waves.size() == TerrainCellCount;
    const float left =
        waves ? static_cast<float>((static_cast<double>(water->waves[corners[0]]) * static_cast<double>(WaveToV)) +
                                   static_cast<double>(v))
              : v;
    const float right =
        waves ? static_cast<float>(static_cast<double>(water->waves[corners[1]] * WaveToV) + static_cast<double>(v))
              : v;
    const float u1 = u + du;
    return {glm::vec2(u, left), glm::vec2(u1, right), glm::vec2(u1, right + dv), glm::vec2(u, left + dv)};
}

// 0x5fa830..0x5fa98e: en el mapa 7, la capa 2 en 5 con alguna esquina en alfa va con el agua de 0x5fa4b0 (el
// tile 0x1e + DAT_07d2d360, el slot 0x41 + cuadro) en vez del pase de alfa de la capa 2. 0x5fa5c0 con el 5.º argumento
// en 1 toma 16 (0x6b8f00) en vez de 64 y, con el 4.º en 0, no corre ni ondula.
constexpr std::uint8_t AnimatedWaterMap = 7;
constexpr std::uint8_t FirstAnimatedWaterTile = 0x1e;
constexpr float AnimatedWaterSpan = 16.0f;
static_assert(std::bit_cast<std::uint32_t>(AnimatedWaterSpan) == 0x41800000, "la constante de 0x6b8f00");

// 0x5fa4b0: 0x620a90 (aditiva), el slot del tile + 0x23 y un GL_TRIANGLE_FAN con glColor3f del alfa de cada
// esquina (0x7ccccc4), sin la luz. 0x7d2d35e, que lo saltearía, sólo se lee: queda en el 0 de la .bss.
void DrawAnimatedWater(Render::ModelBatch& batch, const TerrainView& view, std::uint8_t x, std::uint8_t y,
                       const std::array<glm::vec3, 4>& positions, const std::array<std::size_t, 4>& corners)
{
    const auto tile = static_cast<std::uint8_t>(FirstAnimatedWaterTile + view.waterFrame);
    const TileSize& size = view.tiles[tile];
    const float du = AnimatedWaterSpan / size.width;
    const float dv = AnimatedWaterSpan / size.height;
    const float u = du * static_cast<float>(x);
    const float v = dv * static_cast<float>(y);
    const std::array<glm::vec2, 4> uvs{glm::vec2(u, v), glm::vec2(u + du, v), glm::vec2(u + du, v + dv),
                                       glm::vec2(u, v + dv)};
    batch.SetState(
        Render::ModelDrawState{.texture = Render::TextureSlot{static_cast<std::uint16_t>(FirstTileSlot + tile)},
                               .blend = Render::ModelBlend::Additive});
    std::array<Render::ModelVertex, 4> vertices{};
    for (std::size_t k = 0; k < 4; ++k)
    {
        const float grey = view.mapping->alpha[corners[k]];
        vertices[k] =
            Render::ModelVertex{.position = positions[k], .uv = uvs[k], .color = Render::Color{grey, grey, grey, 1.0f}};
    }
    batch.Triangle({vertices[0], vertices[1], vertices[2]});
    batch.Triangle({vertices[0], vertices[2], vertices[3]});
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

// 0x5fa9f6..0x5faa73: el pasto va en las celdas con las cuatro esquinas en alfa 0 o menos (fcomp con 0 y
// test ah, 0x41) y con 0x7b4ccb4 en 0, que nadie escribe (Q-C226); 0x5faa79, fuera de los mapas 11 a 17.
bool CellTakesGrass(const TerrainView& view, const std::array<std::size_t, 4>& corners)
{
    for (const std::size_t corner : corners)
    {
        if (view.mapping->alpha[corner] > 0.0f)
        {
            return false;
        }
    }
    return view.map < FirstWithoutGrassCellsMap || view.map > LastWithoutGrassCellsMap;
}

// 0x5faa8c..0x5faaab: el slot 0x32 + capa 1 y su textura, si está (+0x34 distinto de 0). Del 0x35 al 0x40
// no los sube nadie y 0x6070a0 los suelta en cada carga (Q-C230); del 0x41 en adelante MuRet no sabe.
Core::Expected<std::optional<float>, std::string> GrassTextureHeight(const TerrainGrass& grass, std::uint16_t slot)
{
    if (slot >= FirstUnknownGrassSlot)
    {
        return Core::Unexpected(
            std::format("0x5faaa3: el pasto lee el slot {:#x} (0x32 + la capa 1), que MuRet no sube", slot));
    }
    const std::size_t grassTexture = slot - FirstGrassSlot;
    if (grassTexture >= grass.heights.size())
    {
        return std::optional<float>{};
    }
    return grass.heights[grassTexture];
}

// 0x5faab1..0x5fac7b: el quad de la celda (x, y) con la textura de `slot` y el alto `height` de su
// buffer, en el orden de los cuatro glVertex3fv: arriba sobre (x, y), arriba sobre (x + 1, y + 1), abajo
// en (x + 1, y + 1) y abajo en (x, y), cada uno con la luz de la esquina 0, 1, 2 y 3 (Q-C227).
void DrawGrassQuad(Render::ModelBatch& batch, const TerrainView& view, const TerrainWater& water,
                   const TerrainGrass& grass, std::uint16_t slot, float height, std::uint8_t x, std::uint8_t y,
                   const std::array<std::size_t, 4>& corners)
{
    // 0x5faab7..0x5faac7: el doble del alto, guardado en un float.
    const float stem = StoredSum(height, height);
    // 0x5faaca..0x5faada: x · 0,25 desde la x87 más el corrimiento de la fila, guardado en un float.
    const auto u = static_cast<float>((static_cast<double>(x) * static_cast<double>(GrassStep)) +
                                      static_cast<double>(grass.offsets[y]));
    const float u1 = StoredSum(u, GrassStep);
    // 0x5fac90 deja las esquinas en 0x79ecc30..0x79ecc5c; 0x5fab27..0x5fabbc sube las de arriba.
    const float left = static_cast<float>(x) * TerrainCellSize;
    const float top = static_cast<float>(y) * TerrainCellSize;
    const float right = left + TerrainCellSize;
    const float bottom = top + TerrainCellSize;
    const float below = CornerHeight(view, corners[0]);
    const float across = CornerHeight(view, corners[2]);
    const std::array<glm::vec3, 4> positions{
        glm::vec3(left - GrassLean, StoredSum(top, WaveAt(water, corners[0])), StoredSum(below, stem)),
        glm::vec3(right - GrassLean, StoredSum(bottom, WaveAt(water, corners[1])), StoredSum(across, stem)),
        glm::vec3(right, bottom, across), glm::vec3(left, top, below)};
    const std::array<glm::vec2, 4> uvs{glm::vec2(u, GrassTop), glm::vec2(u1, GrassTop), glm::vec2(u1, GrassBottom),
                                       glm::vec2(u, GrassBottom)};
    batch.SetState(Render::ModelDrawState{.texture = Render::TextureSlot{slot}, .blend = Render::ModelBlend::Alpha});
    std::array<Render::ModelVertex, 4> vertices{};
    for (std::size_t k = 0; k < vertices.size(); ++k)
    {
        const auto& light = view.lighting->cells[corners[k]];
        vertices[k] = Render::ModelVertex{
            .position = positions[k], .uv = uvs[k], .color = Render::Color{light[0], light[1], light[2], 1.0f}};
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
        heights[k] = CornerHeight(view, corners[k]);
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
    // 0x5fa973..0x5fa9b1: el mapa 7 mira la capa 2 (0x7aacc8c) de la primera esquina, con la base ya dibujada.
    if (someAboveZero && view.map == AnimatedWaterMap && view.mapping->layer2[corners[0]] == WaterTile)
    {
        DrawAnimatedWater(batch, view, x, y, positions, corners);
        return;
    }
    if (someBelowOne && someAboveZero)
    {
        const std::uint8_t upper = view.mapping->layer2[corners[0]];
        const bool upperIsWater = baseIsWater && upper == WaterTile;
        DrawPass(batch, view, upper, Render::ModelBlend::Alpha, positions, corners,
                 TileUvs(view, upper, x, y, corners, upperIsWater ? &water : nullptr));
    }
}

Core::Expected<void, std::string> DrawGrassCell(Render::ModelBatch& batch, const TerrainView& view,
                                                const TerrainWater& water, const TerrainGrass& grass, std::uint8_t x,
                                                std::uint8_t y)
{
    if (grass.offsets.size() < GrassOffsetRows)
    {
        return Core::Unexpected(std::string("0x5faad3: el corrimiento del pasto (0x7b0cca4, de 0x5f8300) no está"));
    }
    const std::array<std::size_t, 4> corners = CornersOf(x, y);
    // 0x5facb6..0x5facce, en 0x5fac90 antes de 0x5fa830 (que llama a 0x405e00, sin rand() fuera de los
    // mapas 18 a 23, que no tienen este pase).
    if ((view.attributes->cells[corners[0]] & HiddenCellBit) != 0 || !CellTakesGrass(view, corners))
    {
        return {};
    }
    const auto slot = static_cast<std::uint16_t>(FirstGrassSlot + view.mapping->layer1[corners[0]]);
    const auto height = GrassTextureHeight(grass, slot);
    if (!height.HasValue())
    {
        return Core::Unexpected(height.Error());
    }
    if (height.Value().has_value())
    {
        DrawGrassQuad(batch, view, water, grass, slot, *height.Value(), x, y, corners);
    }
    return {};
}

} // namespace Client::World
