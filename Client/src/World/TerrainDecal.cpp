#include "World/TerrainDecal.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "World/OriginalFtol.h"
#include "World/TerrainCells.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::World
{
namespace
{
using Data::Terrain::TerrainSide;

// 0x6b86c0: del mundo a las celdas.
constexpr float CellsPerUnit = 0.01f;
static_assert(std::bit_cast<std::uint32_t>(CellsPerUnit) == 0x3C23D70A, "la constante de 0x6b86c0");
// 0x6b882c: el medio lado, y el centro del calco en las UV, alrededor del que giran.
constexpr double Half = 0.5;
// 0x6b87d4: el paso de las celdas.
constexpr double One = 1.0;
// 0x5faee9..0x5faefa: 0x5faec0 dibuja las celdas de 0 a 254 (cmp 0xff y jge).
constexpr std::int32_t FirstUndrawnCell = 255;
// 0x5fb514..0x5fb516: 0x5fb2f0 llama a 0x5faec0 con un lado de 1 celda y el paso 1, sin la luz del terreno.
constexpr float DecalCellSide = 1.0f;

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

// 0x69fd3c, que da el dword bajo del entero de 64 bits truncado.
Core::Expected<std::int32_t, std::string> FtolOf(float value, std::string_view where)
{
    const std::optional<std::int32_t> whole = OriginalFtolLow(D(value));
    if (!whole)
    {
        return Core::Unexpected(
            std::format("{}: el ftol de {} en el calco del suelo no da un entero de 64 bits", where, D(value)));
    }
    return *whole;
}

// 0x5faec0(x, y, 1, 1, uvs, 0, alfa, lift): la celda (ftol(x), ftol(y)) como un fan de cuatro vértices, de
// (x, y) a (x + 1, y + 1) por 100, cada uno con su UV y la altura de su esquina de 0x7accca4 más `lift`, con
// el color de 0x5fb2f0. No mira los bits 0x40 ni 0x08 del atributo, que el terreno sí mira (Q-C232). Deja las
// esquinas en 0x78acbc4..0x78acbd0 y los vértices en 0x79ecc30..0x79ecc5c, globales que el terreno pisa
// antes de leerlos.
Core::Expected<void, std::string> DrawDecalCell(Render::ModelBatch& batch, const Data::Terrain::TerrainHeights& heights,
                                                float x, float y, const std::array<glm::vec2, 4>& uvs,
                                                const Render::Color& color, float lift)
{
    const auto column = FtolOf(x, "0x5faeca");
    const auto row = FtolOf(y, "0x5faed4");
    if (!column.HasValue() || !row.HasValue())
    {
        return Core::Unexpected(column.HasValue() ? row.Error() : column.Error());
    }
    // 0x5faedb..0x5faefa: con signo, de 0 a 254.
    if (column.Value() < 0 || row.Value() < 0 || column.Value() >= FirstUndrawnCell || row.Value() >= FirstUndrawnCell)
    {
        return {};
    }
    // 0x5faf00..0x5faf26: el lado por 100, guardado; x e y por 100, en el registro.
    const float side = F32(D(DecalCellSide) * D(TerrainCellSize));
    const double left = D(x) * D(TerrainCellSize);
    const double top = D(y) * D(TerrainCellSize);
    const float right = F32(left + D(side));
    const float bottom = F32(top + D(side));
    const std::size_t first =
        (static_cast<std::size_t>(row.Value()) * TerrainSide) + static_cast<std::size_t>(column.Value());
    // Las esquinas (x, y), (x + 1, y), (x + 1, y + 1) y (x, y + 1).
    const std::array<std::size_t, 4> corners{first, first + 1, first + TerrainSide + 1, first + TerrainSide};
    const std::array<glm::vec2, 4> places{glm::vec2(F32(left), F32(top)), glm::vec2(right, F32(top)),
                                          glm::vec2(right, bottom), glm::vec2(F32(left), bottom)};
    std::array<Render::ModelVertex, 4> vertices{};
    for (std::size_t k = 0; k < vertices.size(); ++k)
    {
        const float z = F32(D(lift) + D(heights.cells[corners[k]]));
        vertices[k] = Render::ModelVertex{.position = glm::vec3(places[k], z), .uv = uvs[k], .color = color};
    }
    // 0x5fb05e..0x5fb0d9: GL_TRIANGLE_FAN, sin glColor por vértice (el sexto argumento en 0).
    batch.Triangle({vertices[0], vertices[1], vertices[2]});
    batch.Triangle({vertices[0], vertices[2], vertices[3]});
    return {};
}

// 0x5fb4a3..0x5fb4fe: cada UV menos 0,5, girada (0x5fcbd0), con la u por el estiramiento, y más 0,5.
std::array<glm::vec2, 4> TurnedUvs(const std::array<glm::vec2, 4>& uvs, const Render::Models::OriginalMatrix& rotation,
                                   float stretch)
{
    std::array<glm::vec2, 4> turned{};
    for (std::size_t k = 0; k < uvs.size(); ++k)
    {
        const glm::vec3 centered(F32(D(uvs[k].x) - Half), F32(D(uvs[k].y) - Half), 0.0f);
        const glm::vec3 rotated = Render::Models::OriginalVectorRotate(centered, rotation);
        turned[k] = glm::vec2(F32((D(stretch) * D(rotated.x)) + Half), F32(D(rotated.y) + Half));
    }
    return turned;
}
} // namespace

Core::Expected<void, std::string>
DrawTerrainDecal(Render::ModelBatch& batch, const Data::Terrain::TerrainHeights& heights, const TerrainDecal& decal)
{
    if (decal.slot < 0 || decal.slot >= static_cast<std::int32_t>(Render::OriginalTextureSlotCount))
    {
        return Core::Unexpected(
            std::format("0x5fb355: el slot {:#x} del calco del suelo (0x620780) está fuera de la tabla", decal.slot));
    }
    // 0x5fb2f9..0x5fb32d: el color. glColor3fv, con el alfa en 1, deja el mismo alfa que glColor4f.
    const Render::Color color{decal.color.x, decal.color.y, decal.color.z, decal.alpha};
    // 0x5fb32d..0x5fb34c: el giro de (0, 0, giro).
    const Render::Models::OriginalMatrix rotation =
        Render::Models::OriginalAngleMatrix(glm::vec3(0.0f, 0.0f, decal.angle));
    // 0x5fb351: 0x620780(slot), con la mezcla y la profundidad que ya estaban.
    Render::ModelDrawState state = batch.State();
    state.texture = Render::TextureSlot{static_cast<std::uint16_t>(decal.slot)};
    state.rawTextureName.reset();
    batch.SetState(state);
    // 0x5fb35a..0x5fb383: el centro en celdas, guardado en float, y su celda.
    const float centerX = F32(D(decal.x) * D(CellsPerUnit));
    const float centerY = F32(D(decal.y) * D(CellsPerUnit));
    const auto column = FtolOf(centerX, "0x5fb378");
    const auto row = FtolOf(centerY, "0x5fb383");
    // 0x5fb388..0x5fb39d: el lado mayor; con NaN, el alto.
    const float side = !(decal.width >= decal.height) ? decal.height : decal.width;
    const auto whole = FtolOf(side, "0x5fb3d7");
    if (!column.HasValue() || !row.HasValue() || !whole.HasValue())
    {
        return Core::Unexpected(!column.HasValue() ? column.Error() : (!row.HasValue() ? row.Error() : whole.Error()));
    }
    // 0x5fb3a0..0x5fb3c4: dónde cae el centro en su celda, más medio lado. La x la toma del float guardado y
    // la y del entero en el registro.
    const float columnStart = F32(static_cast<double>(column.Value()));
    const float rowStart = F32(static_cast<double>(row.Value()));
    const double half = D(side) * Half;
    const float startU = F32((D(columnStart) - D(centerX)) + half);
    const float startV = F32((static_cast<double>(row.Value()) - D(centerY)) + half);
    // 0x5fb3c9..0x5fb3d4: 1 / lado, guardado.
    const float inverse = F32(One / D(side));
    // 0x5fb3dc..0x5fb3fa: n = ftol(lado) + 1 (inc, con la vuelta de 32 bits), el estiramiento ancho / alto y −n.
    const auto reach = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(whole.Value()) + 1u);
    const float last = F32(static_cast<double>(reach));
    const float stretch = F32(D(decal.width) / D(decal.height));
    const float first = -last;
    // 0x5fb3f7..0x5fb408: con n < −n, nada.
    if (last < first)
    {
        return {};
    }
    for (float j = first; j <= last; j = F32(D(j) + One))
    {
        // 0x5fb42b..0x5fb44e: las v de la fila; la de abajo suma el 1 en el registro, y la u de la derecha
        // parte de un float guardado.
        const double v = D(j) + D(startV);
        const float top = F32(v * D(inverse));
        const float bottom = F32((v + One) * D(inverse));
        const float startURight = F32(D(startU) + One);
        for (float i = first; i <= last; i = F32(D(i) + One))
        {
            // 0x5fb451..0x5fb4a0: las UV de la celda en el calco, sin girar.
            const float leftU = F32((D(i) + D(startU)) * D(inverse));
            const float rightU = F32((D(startURight) + D(i)) * D(inverse));
            const std::array<glm::vec2, 4> uvs{glm::vec2(leftU, top), glm::vec2(rightU, top), glm::vec2(rightU, bottom),
                                               glm::vec2(leftU, bottom)};
            // 0x5fb500..0x5fb529: 0x5faec0 con la celda (cx + i, cy + j).
            const float cellX = F32(D(columnStart) + D(i));
            const float cellY = F32(D(rowStart) + D(j));
            if (auto drawn =
                    DrawDecalCell(batch, heights, cellX, cellY, TurnedUvs(uvs, rotation, stretch), color, decal.lift);
                !drawn.HasValue())
            {
                return drawn;
            }
        }
    }
    return {};
}
} // namespace Client::World
