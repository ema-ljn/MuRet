#include "World/TerrainCells.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
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

// Las UV de las cuatro esquinas de un tile en la celda (x, y), como 0x5fa5c0 sin agua: la u avanza
// 64 / ancho por celda y la v, 64 / alto.
std::array<glm::vec2, 4> TileUvs(const TileSize& tile, std::uint8_t x, std::uint8_t y)
{
    const float du = TileSpan / tile.width;
    const float dv = TileSpan / tile.height;
    const float u = du * static_cast<float>(x);
    const float v = dv * static_cast<float>(y);
    const float u1 = u + du;
    const float v1 = v + dv;
    return {glm::vec2(u, v), glm::vec2(u1, v), glm::vec2(u1, v1), glm::vec2(u, v1)};
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

void DrawTerrainCell(Render::ModelBatch& batch, const TerrainView& view, const TerrainWater& /*water*/, std::uint8_t x,
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
    DrawPass(batch, view, base, Render::ModelBlend::Opaque, positions, corners, TileUvs(view.tiles[base], x, y));
    if (someBelowOne && someAboveZero)
    {
        const std::uint8_t upper = view.mapping->layer2[corners[0]];
        DrawPass(batch, view, upper, Render::ModelBlend::Alpha, positions, corners, TileUvs(view.tiles[upper], x, y));
    }
}
} // namespace Client::World
