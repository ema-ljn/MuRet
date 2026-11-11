#include "World/KalimaSurfaceDecal.h"

#include "Core/Expected.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "World/KalimaWaves.h"
#include "World/KalimaWorld.h"
#include "World/OriginalFtol.h"
#include "World/TerrainDecal.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::World
{
namespace
{
constexpr float CellsPerUnit = 0.02f; // 41dd70/41dd7f, distinta de la proyección ordinaria.
constexpr float CellSide = 50;        // 41dfd9/41dfe2.
constexpr float SurfaceBase = 400;    // 41e00b/41e02e/41e058/41e06b.
constexpr double Half = 0.5;
constexpr std::int32_t FirstUndrawnCell = 255;
constexpr std::int32_t FirstNonProgressingReach = 1 << 24;

float F32(double value)
{
    return static_cast<float>(value);
}

struct DecalProjection
{
    float column;
    float row;
    float startU;
    float startV;
    float inverse;
    float stretch;
    std::int32_t reach;
    Render::Models::OriginalMatrix rotation;
};

Core::Expected<DecalProjection, std::string> Project(const TerrainDecal& decal)
{
    const float centerX = F32(static_cast<double>(decal.x) * CellsPerUnit);
    const float centerY = F32(static_cast<double>(decal.y) * CellsPerUnit);
    const auto column = OriginalFtolLow(centerX);
    const auto row = OriginalFtolLow(centerY);
    const float side = !(decal.width >= decal.height) ? decal.height : decal.width;
    const auto whole = OriginalFtolLow(side);
    if (!column || !row || !whole)
        return Core::Unexpected(std::string("0x41dd90/41dd96/41ddea: ftol del calco de Kalima fuera de int64"));
    const auto reach = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(*whole) + 1u);
    // Con2^24 el incrementofloat de1 ya no progresa: el caller nativo no termina.
    if (reach >= FirstNonProgressingReach)
        return Core::Unexpected(std::string("0x41df47: el alcance del calco no permite avanzar de a uno"));
    const float columnStart = F32(*column);
    const float rowStart = F32(*row);
    const double half = static_cast<double>(side) * Half;
    return DecalProjection{columnStart,
                           rowStart,
                           F32((static_cast<double>(columnStart) - centerX) + half),
                           F32((static_cast<double>(*row) - centerY) + half),
                           F32(1.0 / side),
                           F32(static_cast<double>(decal.width) / decal.height),
                           reach,
                           Render::Models::OriginalAngleMatrix({0, 0, decal.angle})};
}

std::array<glm::vec2, 4> Uvs(const DecalProjection& projection, std::int64_t column, std::int64_t row)
{
    // 41de3e..41deb3: v inferior retenida, u derecha desde startU+1 derramado.
    const double v = static_cast<double>(row) + projection.startV;
    const float top = F32(v * projection.inverse);
    const float bottom = F32((v + 1.0) * projection.inverse);
    const float left = F32((static_cast<double>(column) + projection.startU) * projection.inverse);
    const float startRight = F32(static_cast<double>(projection.startU) + 1.0);
    const float right = F32((static_cast<double>(startRight) + static_cast<double>(column)) * projection.inverse);
    const std::array<glm::vec2, 4> base{{{left, top}, {right, top}, {right, bottom}, {left, bottom}}};
    std::array<glm::vec2, 4> turned{};
    for (std::size_t index = 0; index < base.size(); ++index)
    {
        const glm::vec3 centered{F32(static_cast<double>(base[index].x) - Half),
                                 F32(static_cast<double>(base[index].y) - Half), 0};
        const auto rotated = Render::Models::OriginalVectorRotate(centered, projection.rotation);
        turned[index] = {F32(static_cast<double>(rotated.x) * projection.stretch + Half),
                         F32(static_cast<double>(rotated.y) + Half)};
    }
    return turned;
}

void Cell(Render::ModelBatch& batch, const KalimaWavePlane& heights, const TerrainDecal& decal,
          const DecalProjection& projection, std::int64_t column, std::int64_t row)
{
    const float x = F32(static_cast<double>(projection.column) + static_cast<double>(column));
    const float y = F32(static_cast<double>(projection.row) + static_cast<double>(row));
    const auto cell = static_cast<std::size_t>(y) * KalimaWaveSide + static_cast<std::size_t>(x);
    const std::array<std::size_t, 4> indices{cell, cell + 1, cell + KalimaWaveSide + 1, cell + KalimaWaveSide};
    const double left = static_cast<double>(x) * CellSide;
    const double top = static_cast<double>(y) * CellSide;
    const float right = F32(left + CellSide);
    const float bottom = F32(top + CellSide);
    const std::array<glm::vec2, 4> positions{
        {{F32(left), F32(top)}, {right, F32(top)}, {right, bottom}, {F32(left), bottom}}};
    const auto uvs = Uvs(projection, column, row);
    const Render::Color color{decal.color.x, decal.color.y, decal.color.z, decal.alpha};
    std::array<Render::ModelVertex, 4> vertices{};
    for (std::size_t corner = 0; corner < vertices.size(); ++corner)
    {
        const auto wave = std::bit_cast<std::int32_t>(heights[indices[corner]]);
        // 41dff9/41e027/41e046/41e064: siempre plano0, sin selector ni campos periódicos.
        const float z = F32((static_cast<double>(wave) + decal.lift) + SurfaceBase);
        vertices[corner] = {{positions[corner].x, positions[corner].y, z}, uvs[corner], color};
    }
    batch.Triangle({vertices[0], vertices[1], vertices[2]});
    batch.Triangle({vertices[0], vertices[2], vertices[3]});
}
} // namespace

Core::Expected<void, std::string> DrawKalimaSurfaceDecal(Render::ModelBatch& batch, const KalimaWorld& world,
                                                         const TerrainDecal& decal)
{
    if (decal.slot >= static_cast<std::int32_t>(Render::OriginalTextureSlotCount))
        return Core::Unexpected(std::string("0x41dd68: slot del calco fuera de la tabla de texturas"));
    Render::ModelDrawState state = batch.State();
    if (decal.slot < 0)
    {
        state.texture.reset();
        state.rawTextureName = std::uint32_t{0} - static_cast<std::uint32_t>(decal.slot);
    }
    else
    {
        state.texture = Render::TextureSlot{static_cast<std::uint16_t>(decal.slot)};
        state.rawTextureName.reset();
    }
    batch.ApplyOperation(state, Render::ModelStateOperation::Binding);
    const auto projected = Project(decal);
    if (!projected.HasValue())
        return Core::Unexpected(projected.Error());
    const auto& projection = projected.Value();
    if (projection.reach < 0)
        return {};
    // 41dfac..41dfd0 no emite GL fuera de0..254. Recortar omite sólo iteraciones sin efectos;
    // los offsets siguen siendo enterosfloat exactos en el dominio donde el bucle termina.
    const std::int64_t reach = projection.reach;
    const auto column = static_cast<std::int64_t>(projection.column);
    const auto row = static_cast<std::int64_t>(projection.row);
    const auto firstColumn = std::max(-reach, -column);
    const auto lastColumn = std::min(reach, FirstUndrawnCell - 1 - column);
    const auto firstRow = std::max(-reach, -row);
    const auto lastRow = std::min(reach, FirstUndrawnCell - 1 - row);
    const auto& plane = world.Waves().planes[0];
    for (auto y = firstRow; y <= lastRow; ++y)
        for (auto x = firstColumn; x <= lastColumn; ++x)
            Cell(batch, plane, decal, projection, x, y);
    return {};
}
} // namespace Client::World
