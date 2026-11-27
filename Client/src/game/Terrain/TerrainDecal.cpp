#include "Terrain/TerrainDecal.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Math/OriginalRotation.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Math/Ftol.h"
#include "Terrain/TerrainCells.h"

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

namespace Client::Terrain
{
namespace
{
using Data::Terrain::TerrainSide;

// 0x6b86c0: from the world to the cells.
constexpr float CellsPerUnit = 0.01f;
static_assert(std::bit_cast<std::uint32_t>(CellsPerUnit) == 0x3C23D70A, "la constante de 0x6b86c0");
// 0x6b882c: the half side, and the decal's center in the UVs, around which they turn.
constexpr double Half = Math::HalfAsDouble;
// 0x6b87d4: the cells' step.
constexpr double One = 1.0;
// 0x5faee9..0x5faefa: 0x5faec0 draws cells 0 to 254 (cmp 0xff and jge).
constexpr std::int32_t FirstUndrawnCell = 255;
// 0x5fb514..0x5fb516: 0x5fb2f0 calls 0x5faec0 with a side of 1 cell and step 1, without the terrain's light.
constexpr float DecalCellSide = 1.0f;

using Math::D;
using Math::F32;

// 0x69fd3c, which gives the low dword of the truncated 64-bit integer.
Core::Expected<std::int32_t, std::string> FtolOf(float value, std::string_view where)
{
    const std::optional<std::int32_t> whole = Math::OriginalFtolLow(D(value));
    if (!whole)
    {
        return Core::Unexpected(
            std::format("{}: el ftol de {} en el calco del suelo no da un entero de 64 bits", where, D(value)));
    }
    return *whole;
}

// 0x5faec0(x, y, 1, 1, uvs, 0, alpha, lift): cell (ftol(x), ftol(y)) as a four-vertex fan, from (x, y) to (x + 1, y +
// 1) times 100, each vertex with its UV and its corner's height from 0x7accca4 plus `lift`, with the color of 0x5fb2f0.
// It does not check bits 0x40 or 0x08 of the attribute, which the terrain does check (Q-C232). It leaves the corners at
// 0x78acbc4..0x78acbd0 and the vertices at 0x79ecc30..0x79ecc5c, globals the terrain overwrites before reading them.
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
    // 0x5faedb..0x5faefa: signed, from 0 to 254.
    if (column.Value() < 0 || row.Value() < 0 || column.Value() >= FirstUndrawnCell || row.Value() >= FirstUndrawnCell)
    {
        return {};
    }
    // 0x5faf00..0x5faf26: the side times 100, stored; x and y times 100, in the register.
    const float side = F32(D(DecalCellSide) * D(TerrainCellSize));
    const double left = D(x) * D(TerrainCellSize);
    const double top = D(y) * D(TerrainCellSize);
    const float right = F32(left + D(side));
    const float bottom = F32(top + D(side));
    const std::size_t first =
        (static_cast<std::size_t>(row.Value()) * TerrainSide) + static_cast<std::size_t>(column.Value());
    const std::array<std::size_t, 4> corners{first, first + 1, first + TerrainSide + 1, first + TerrainSide};
    const std::array<glm::vec2, 4> places{glm::vec2(F32(left), F32(top)), glm::vec2(right, F32(top)),
                                          glm::vec2(right, bottom), glm::vec2(F32(left), bottom)};
    std::array<Render::ModelVertex, 4> vertices{};
    for (std::size_t k = 0; k < vertices.size(); ++k)
    {
        const float z = F32(D(lift) + D(heights.cells[corners[k]]));
        vertices[k] = Render::ModelVertex{.position = glm::vec3(places[k], z), .uv = uvs[k], .color = color};
    }
    // 0x5fb05e..0x5fb0d9: GL_TRIANGLE_FAN, without glColor per vertex (the sixth argument at 0).
    batch.Triangle({vertices[0], vertices[1], vertices[2]});
    batch.Triangle({vertices[0], vertices[2], vertices[3]});
    return {};
}

// 0x5fb4a3..0x5fb4fe: each UV minus 0.5, turned (0x5fcbd0), with u times the stretch, and plus 0.5.
std::array<glm::vec2, 4> TurnedUvs(const std::array<glm::vec2, 4>& uvs, const Math::OriginalMatrix& rotation,
                                   float stretch)
{
    std::array<glm::vec2, 4> turned{};
    for (std::size_t k = 0; k < uvs.size(); ++k)
    {
        const glm::vec3 centered(F32(D(uvs[k].x) - Half), F32(D(uvs[k].y) - Half), 0.0f);
        const glm::vec3 rotated = Math::OriginalVectorRotate(centered, rotation);
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
    // 0x5fb2f9..0x5fb32d: the color. glColor3fv, with the alpha at 1, leaves the same alpha as glColor4f.
    const Render::Color color{decal.color.x, decal.color.y, decal.color.z, decal.alpha};
    // 0x5fb32d..0x5fb34c: the turn of (0, 0, angle).
    const Math::OriginalMatrix rotation = Math::OriginalAngleMatrix(glm::vec3(0.0f, 0.0f, decal.angle));
    // 0x5fb351: 0x620780(slot), with the blend and depth that were already set.
    Render::ModelDrawState state = batch.State();
    state.texture = Render::TextureSlot{static_cast<std::uint16_t>(decal.slot)};
    state.rawTextureName.reset();
    batch.SetState(state);
    // 0x5fb35a..0x5fb383: the center in cells, stored as float, and its cell.
    const float centerX = F32(D(decal.x) * D(CellsPerUnit));
    const float centerY = F32(D(decal.y) * D(CellsPerUnit));
    const auto column = FtolOf(centerX, "0x5fb378");
    const auto row = FtolOf(centerY, "0x5fb383");
    // 0x5fb388..0x5fb39d: the larger side; with NaN, the height.
    const float side = !(decal.width >= decal.height) ? decal.height : decal.width;
    const auto whole = FtolOf(side, "0x5fb3d7");
    if (!column.HasValue() || !row.HasValue() || !whole.HasValue())
    {
        return Core::Unexpected(!column.HasValue() ? column.Error() : (!row.HasValue() ? row.Error() : whole.Error()));
    }
    // 0x5fb3a0..0x5fb3c4: where the center falls in its cell, plus half a side. The x comes from the stored float and
    // the y from the integer in the register.
    const float columnStart = F32(static_cast<double>(column.Value()));
    const float rowStart = F32(static_cast<double>(row.Value()));
    const double half = D(side) * Half;
    const float startU = F32((D(columnStart) - D(centerX)) + half);
    const float startV = F32((static_cast<double>(row.Value()) - D(centerY)) + half);
    // 0x5fb3c9..0x5fb3d4: 1 / side, stored.
    const float inverse = F32(One / D(side));
    // 0x5fb3dc..0x5fb3fa: n = ftol(side) + 1 (inc, with the 32-bit wrap), the width / height stretch and −n.
    const auto reach = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(whole.Value()) + 1u);
    const float last = F32(static_cast<double>(reach));
    const float stretch = F32(D(decal.width) / D(decal.height));
    const float first = -last;
    // 0x5fb3f7..0x5fb408: with n < −n, nothing.
    if (last < first)
    {
        return {};
    }
    for (float j = first; j <= last; j = F32(D(j) + One))
    {
        // 0x5fb42b..0x5fb44e: the row's v values; the bottom one adds the 1 in the register, and the right u starts
        // from a stored float.
        const double v = D(j) + D(startV);
        const float top = F32(v * D(inverse));
        const float bottom = F32((v + One) * D(inverse));
        const float startURight = F32(D(startU) + One);
        for (float i = first; i <= last; i = F32(D(i) + One))
        {
            // 0x5fb451..0x5fb4a0: the cell's UVs in the decal, unturned.
            const float leftU = F32((D(i) + D(startU)) * D(inverse));
            const float rightU = F32((D(startURight) + D(i)) * D(inverse));
            const std::array<glm::vec2, 4> uvs{glm::vec2(leftU, top), glm::vec2(rightU, top), glm::vec2(rightU, bottom),
                                               glm::vec2(leftU, bottom)};
            // 0x5fb500..0x5fb529: 0x5faec0 with cell (cx + i, cy + j).
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
} // namespace Client::Terrain
