#include "Terrain/TerrainDecal.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// Client::Terrain::DrawTerrainDecal with a hand-built terrain: the decal of 0x5fb2f0 over the cells of 0x5faec0
// (06 § Las marcas del suelo).
namespace
{
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::TerrainHeights;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawCommand;
using Client::Render::ModelDrawState;
using Client::Render::ModelVertex;
using Client::Render::TextureSlot;
using Client::Terrain::DrawTerrainDecal;
using Client::Terrain::TerrainDecal;

constexpr std::int32_t BloodSlot = 0x5e3;
// 0x549175: how much a mark's decal is lifted.
constexpr float Lift = 5.0f;
// The six vertices of a cell, from its four corners: the fan (0, 1, 2) and (0, 2, 3).
constexpr std::array<std::size_t, 6> FanCorners{0, 1, 2, 0, 2, 3};

// Different heights on neighboring cells, all exact in float.
std::unique_ptr<TerrainHeights> Heights()
{
    auto heights = std::make_unique<TerrainHeights>();
    for (std::size_t cell = 0; cell < heights->cells.size(); ++cell)
    {
        heights->cells[cell] = static_cast<float>(cell % 7) * 1.5f;
    }
    return heights;
}

// In the world (1000, 2000): the center falls right on the corner of cell (10, 20).
TerrainDecal Decal()
{
    return TerrainDecal{.slot = BloodSlot,
                        .x = 1000.0f,
                        .y = 2000.0f,
                        .width = 1.0f,
                        .height = 1.0f,
                        .color = glm::vec3(0.25f, 0.5f, 0.75f),
                        .angle = 0.0f,
                        .alpha = 0.5f,
                        .lift = Lift};
}

struct Drawn
{
    ModelBatch batch;
    std::optional<std::string> error;
};

// With the normal blend and no depth test, as 0x549130 leaves it.
Drawn Draw(const TerrainHeights& heights, const TerrainDecal& decal)
{
    Drawn drawn;
    drawn.batch.Begin(Client::Render::Pass3D{});
    drawn.batch.SetState(ModelDrawState{.texture = std::nullopt, .blend = ModelBlend::Alpha, .depthTest = false});
    if (auto decaled = DrawTerrainDecal(drawn.batch, heights, decal); !decaled.HasValue())
    {
        drawn.error = decaled.Error();
    }
    return drawn;
}

// Corner `corner` (0 to 3) of cell `cell` of the batch, in drawing order.
const ModelVertex& Corner(const ModelBatch& batch, std::size_t cell, std::size_t corner)
{
    // Corners 0, 1 and 2 open the first triangle; 3 closes the second.
    const std::size_t offset = corner == 3 ? 5 : corner;
    return batch.Vertices()[(cell * FanCorners.size()) + offset];
}

// Corner `corner` of cell (x, y) in the world, with its height plus the decal's lift.
glm::vec3 CornerPosition(const TerrainHeights& heights, std::uint8_t x, std::uint8_t y, std::size_t corner)
{
    // (x, y), (x + 1, y), (x + 1, y + 1) and (x, y + 1).
    constexpr std::array<int, 4> right{0, 1, 1, 0};
    constexpr std::array<int, 4> down{0, 0, 1, 1};
    const auto cornerX = static_cast<std::uint8_t>(x + right[corner]);
    const auto cornerY = static_cast<std::uint8_t>(y + down[corner]);
    return glm::vec3(static_cast<float>(cornerX) * 100.0f, static_cast<float>(cornerY) * 100.0f,
                     Lift + heights.cells[CellIndex(cornerX, cornerY)]);
}
} // namespace

// Unturned and with side 1: n = 2, the 25 cells from (8, 18) to (12, 22), row by row. The center falls on the
// corner of (10, 20), so a cell's u goes from i + 0.5 to i + 1.5 and its v from j + 0.5 to j + 1.5: the whole decal
// is the square of one cell around the center and the rest fall outside [0, 1]. Each vertex has the height of its
// corner plus 5 and the color with the alpha.
TEST(TerrainDecalTest, AnUnturnedDecalCoversTheCellsAroundItsCenter)
{
    const auto heights = Heights();
    const Drawn drawn = Draw(*heights, Decal());

    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    ASSERT_EQ(drawn.batch.Commands().size(), 1u);
    const ModelDrawCommand& command = drawn.batch.Commands()[0];
    EXPECT_EQ(command.state.texture, TextureSlot{static_cast<std::uint16_t>(BloodSlot)});
    EXPECT_EQ(command.state.blend, ModelBlend::Alpha);
    EXPECT_FALSE(command.state.depthTest);
    ASSERT_EQ(drawn.batch.Vertices().size(), 25u * FanCorners.size());
    std::size_t cell = 0;
    for (int j = -2; j <= 2; ++j)
    {
        for (int i = -2; i <= 2; ++i, ++cell)
        {
            SCOPED_TRACE(testing::Message() << i << ", " << j);
            const auto x = static_cast<std::uint8_t>(10 + i);
            const auto y = static_cast<std::uint8_t>(20 + j);
            const float left = static_cast<float>(i) + 0.5f;
            const float top = static_cast<float>(j) + 0.5f;
            const std::array<glm::vec2, 4> uvs{glm::vec2(left, top), glm::vec2(left + 1.0f, top),
                                               glm::vec2(left + 1.0f, top + 1.0f), glm::vec2(left, top + 1.0f)};
            for (std::size_t corner = 0; corner < 4; ++corner)
            {
                const ModelVertex& vertex = Corner(drawn.batch, cell, corner);
                EXPECT_EQ(vertex.position, CornerPosition(*heights, x, y, corner)) << corner;
                EXPECT_EQ(vertex.uv, uvs[corner]) << corner;
                EXPECT_EQ(vertex.color, (Client::Render::Color{0.25f, 0.5f, 0.75f, 0.5f})) << corner;
            }
        }
    }
}

// 0x5fb2f0 changes neither the blend nor the depth: it binds the texture over what the caller left.
TEST(TerrainDecalTest, KeepsTheBlendItWasGiven)
{
    const auto heights = Heights();
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    batch.SetState(ModelDrawState{.texture = TextureSlot{0x32}, .blend = ModelBlend::Additive, .depthTest = false});

    ASSERT_TRUE(DrawTerrainDecal(batch, *heights, Decal()).HasValue());

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state, (ModelDrawState{.texture = TextureSlot{static_cast<std::uint16_t>(BloodSlot)},
                                                         .blend = ModelBlend::Additive,
                                                         .depthTest = false}));
}

// 0x5faedb..0x5faefa: cells with x or y outside 0..254 are not drawn. With the center at (0.5, 254.5) columns 0 to
// 2 and rows 252 to 254 remain: 255 does not either, even though it is on the map.
TEST(TerrainDecalTest, TheCellsOutsideZeroTo254AreNotDrawn)
{
    const auto heights = Heights();
    TerrainDecal decal = Decal();
    decal.x = 50.0f;
    decal.y = 25450.0f;

    const Drawn drawn = Draw(*heights, decal);

    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    ASSERT_EQ(drawn.batch.Vertices().size(), 9u * FanCorners.size());
    std::size_t cell = 0;
    for (std::uint8_t y = 252; y <= 254; ++y)
    {
        for (std::uint8_t x = 0; x <= 2; ++x, ++cell)
        {
            EXPECT_EQ(Corner(drawn.batch, cell, 0).position, CornerPosition(*heights, x, y, 0));
            EXPECT_EQ(Corner(drawn.batch, cell, 2).position, CornerPosition(*heights, x, y, 2));
        }
    }
}

// The larger side sets the cells, n = ftol(2) + 1 = 3, and 1 / 2 the UV step; the quotient, 2, stretches the u
// around 0.5 after the turn: u = 2 · ((i + 1) · 0.5 − 0.5) + 0.5 = i + 0.5 on the left and i + 1.5 on the right,
// and the v stays (j + 1) · 0.5 on top and (j + 2) · 0.5 at the bottom.
TEST(TerrainDecalTest, AWiderDecalCoversMoreCellsAndStretchesTheU)
{
    const auto heights = Heights();
    TerrainDecal decal = Decal();
    decal.width = 2.0f;

    const Drawn drawn = Draw(*heights, decal);

    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    ASSERT_EQ(drawn.batch.Vertices().size(), 49u * FanCorners.size());
    std::size_t cell = 0;
    for (int j = -3; j <= 3; ++j)
    {
        for (int i = -3; i <= 3; ++i, ++cell)
        {
            const ModelVertex& first = Corner(drawn.batch, cell, 0);
            EXPECT_EQ(first.uv, glm::vec2(static_cast<float>(i) + 0.5f, static_cast<float>(j + 1) * 0.5f))
                << i << ", " << j;
            EXPECT_EQ(Corner(drawn.batch, cell, 2).uv,
                      glm::vec2(static_cast<float>(i) + 1.5f, static_cast<float>(j + 2) * 0.5f));
        }
    }
}

// 0x5fb388..0x5fb39d: with the width as NaN, the side is the height (fcomp and C0): 25 cells, as with side 1.
TEST(TerrainDecalTest, WithANotANumberWidthTheHeightDecides)
{
    const auto heights = Heights();
    TerrainDecal decal = Decal();
    decal.width = std::numeric_limits<float>::quiet_NaN();

    const Drawn drawn = Draw(*heights, decal);

    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    EXPECT_EQ(drawn.batch.Vertices().size(), 25u * FanCorners.size());
}

// The turn of 0x5fc8f0 with (0, 0, 90): each UV turns around (0.5, 0.5), u' = 1 − v and v' = u, with the x87 sine
// and cosine (within 1e-6).
TEST(TerrainDecalTest, ATurnedDecalTurnsItsUvsAroundTheMiddle)
{
    const auto heights = Heights();
    TerrainDecal decal = Decal();
    decal.angle = 90.0f;

    const Drawn drawn = Draw(*heights, decal);

    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    ASSERT_EQ(drawn.batch.Vertices().size(), 25u * FanCorners.size());
    // The center cell, 12: u from 0.5 to 1.5 and v from 0.5 to 1.5 unturned.
    const std::array<glm::vec2, 4> unturned{glm::vec2(0.5f, 0.5f), glm::vec2(1.5f, 0.5f), glm::vec2(1.5f, 1.5f),
                                            glm::vec2(0.5f, 1.5f)};
    for (std::size_t corner = 0; corner < 4; ++corner)
    {
        const glm::vec2 uv = Corner(drawn.batch, 12, corner).uv;
        EXPECT_NEAR(uv.x, 1.0f - unturned[corner].y, 1e-6f) << corner;
        EXPECT_NEAR(uv.y, unturned[corner].x, 1e-6f) << corner;
    }
}

// 0x620780 with a slot outside the table's 1,845, and a center the ftol of 0x5fb378 cannot bring to a 64-bit
// integer: error, without a vertex.
TEST(TerrainDecalTest, ASlotOutsideTheTableOrAnUnreachableCenterStops)
{
    struct Case
    {
        TerrainDecal decal;
        std::string_view address;
    };
    const auto heights = Heights();
    Case outside{Decal(), "0x5fb355"};
    outside.decal.slot = static_cast<std::int32_t>(Client::Render::OriginalTextureSlotCount);
    Case negative{Decal(), "0x5fb355"};
    negative.decal.slot = -1;
    Case far{Decal(), "0x5fb378"};
    far.decal.x = 1e30f;

    for (const Case& probe : {outside, negative, far})
    {
        const Drawn drawn = Draw(*heights, probe.decal);
        ASSERT_TRUE(drawn.error.has_value());
        EXPECT_NE(drawn.error->find(probe.address), std::string::npos) << *drawn.error;
        EXPECT_TRUE(drawn.batch.Vertices().empty());
    }
}
