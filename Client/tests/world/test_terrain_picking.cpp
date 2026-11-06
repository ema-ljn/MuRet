#include "World/TerrainPicking.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/TerrainPickingSegment.h"
#include "World/TerrainPickingState.h"
#include "World/TerrainPickingTarget.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

// Expectativas analíticas/estáticas de 06 § El rayo contra el terreno; ninguna proviene de una
// nueva ejecución o emulación del original. Los puntos asimétricos evitan la diagonal excluida.
namespace
{
using Client::World::PickTerrain;
using Client::World::PickTerrainTarget;
using Client::World::TerrainPickingSegment;
using Client::World::TerrainPickingState;
using Client::World::TerrainPickingTarget;
using Client::World::TerrainPolygon;
using Client::World::TerrainRange;

class TerrainPickingTest : public ::testing::Test
{
protected:
    std::unique_ptr<Client::Data::Terrain::TerrainHeights> m_heights =
        std::make_unique<Client::Data::Terrain::TerrainHeights>();
    std::unique_ptr<Client::Data::Terrain::TerrainAttributes> m_attributes =
        std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    Client::World::TerrainView m_view{.heights = m_heights.get(), .attributes = m_attributes.get()};
    TerrainPickingState m_state{
        .selected = true, .selectedX = 5.0f, .selectedY = 6.0f, .maxT = 0.25f, .hit = {11.0f, 22.0f, 33.0f}};
    TerrainPickingTarget m_target{.x = 91, .y = 92, .dontMove = true};
    TerrainPolygon m_polygon{.everything = true};

    void SetUp() override
    {
        m_heights->cells.fill(0.0f);
        m_attributes->cells.fill(0);
    }

    static TerrainPickingSegment Vertical(float x, float y, float top = 300.0f, float bottom = -300.0f)
    {
        return {.origin = {x, y, top}, .end = {x, y, bottom}};
    }

    void ExpectRetainedMiss() const
    {
        EXPECT_FALSE(m_state.selected);
        EXPECT_EQ(m_state.selectedX, 5.0f);
        EXPECT_EQ(m_state.selectedY, 6.0f);
        EXPECT_EQ(m_state.hit, glm::vec3(11.0f, 22.0f, 33.0f));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(m_state.maxT), 0x4B18967Fu);
    }
};

// Rechaza la mutación que omite 023, usa un quad o conserva maxT viejo.
TEST_F(TerrainPickingTest, BothTrianglesPublishThePointAndCell)
{
    for (const glm::vec3& point : std::array<glm::vec3, 2>{{{75.0f, 25.0f, 0.0f}, {25.0f, 75.0f, 0.0f}}})
    {
        const auto result = PickTerrain(m_state, m_view, {}, m_polygon, Vertical(point.x, point.y));
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(m_state.selected);
        EXPECT_EQ(m_state.selectedX, 0.0f);
        EXPECT_EQ(m_state.selectedY, 0.0f);
        EXPECT_EQ(m_state.maxT, 0.5f);
        EXPECT_EQ(m_state.hit, point);
    }
}

TEST_F(TerrainPickingTest, DiagonalEdgesAndVerticesAreStrictlyExcluded)
{
    for (const glm::vec3& point : std::array<glm::vec3, 5>{{{50.0f, 50.0f, 0.0f},
                                                            {0.0f, 25.0f, 0.0f},
                                                            {100.0f, 25.0f, 0.0f},
                                                            {25.0f, 0.0f, 0.0f},
                                                            {0.0f, 0.0f, 0.0f}}})
    {
        const auto result = PickTerrain(m_state, m_view, {}, m_polygon, Vertical(point.x, point.y));
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ExpectRetainedMiss();
    }
}

TEST_F(TerrainPickingTest, BackfacesParallelAndOutsideSegmentsKeepThePreviousHit)
{
    constexpr std::array<TerrainPickingSegment, 4> Rays{{
        {{75.0f, 25.0f, -1.0f}, {75.0f, 25.0f, 1.0f}},
        {{75.0f, 25.0f, 1.0f}, {85.0f, 25.0f, 1.0f}},
        {{75.0f, 25.0f, 2.0f}, {75.0f, 25.0f, 1.0f}},
        {{75.0f, 25.0f, -1.0f}, {75.0f, 25.0f, -2.0f}},
    }};
    for (const auto& ray : Rays)
    {
        const auto result = PickTerrain(m_state, m_view, {}, m_polygon, ray);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ExpectRetainedMiss();
    }
}

TEST_F(TerrainPickingTest, SegmentEndpointsAreIncluded)
{
    const auto start = PickTerrain(m_state, m_view, {}, m_polygon, Vertical(75.0f, 25.0f, 0.0f, -100.0f));
    ASSERT_TRUE(start.HasValue()) << start.Error();
    EXPECT_TRUE(m_state.selected);
    EXPECT_EQ(m_state.maxT, 0.0f);
    const auto end = PickTerrain(m_state, m_view, {}, m_polygon, Vertical(75.0f, 25.0f, 100.0f, 0.0f));
    ASSERT_TRUE(end.HasValue()) << end.Error();
    EXPECT_TRUE(m_state.selected);
    EXPECT_EQ(m_state.maxT, 1.0f);
}

TEST_F(TerrainPickingTest, HiddenCellsArePickable)
{
    m_attributes->cells[0] = 0x08;
    const auto result = PickTerrainTarget(m_state, m_target, m_view, {}, m_polygon, Vertical(75.0f, 25.0f));
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(result.Value());
    EXPECT_EQ(m_target.x, 0u);
    EXPECT_EQ(m_target.y, 0u);
    EXPECT_TRUE(m_target.dontMove);
    EXPECT_EQ(m_state.hit, glm::vec3(75.0f, 25.0f, 0.0f));
}

TEST_F(TerrainPickingTest, APositiveYNormalUsesTheZXProjectionWithItsWinding)
{
    m_heights->cells[0] = 150.0f;
    m_heights->cells[1] = 150.0f;
    const auto result = PickTerrain(m_state, m_view, {}, m_polygon, Vertical(75.0f, 25.0f));
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_TRUE(m_state.selected);
    EXPECT_EQ(m_state.hit, glm::vec3(75.0f, 25.0f, 112.5f));
    EXPECT_EQ(m_state.maxT, 0.3125f);
}

// Cada vértice pesa 1/4 o 1/2 en estos puntos: distingue override por esquina de altura bilineal.
TEST_F(TerrainPickingTest, FixedHeightIsAppliedIndependentlyToEachCorner)
{
    constexpr std::array<std::size_t, 4> Corners{0, 1, 257, 256};
    constexpr std::array<float, 4> Expected{300.0f, 600.0f, 300.0f, 600.0f};
    for (std::size_t corner = 0; corner < Corners.size(); ++corner)
    {
        m_attributes->cells.fill(0);
        m_attributes->cells[Corners[corner]] = 0x40;
        const auto ray = corner == 3 ? Vertical(25.0f, 75.0f, 1500.0f) : Vertical(75.0f, 25.0f, 1500.0f);
        const auto result = PickTerrain(m_state, m_view, {}, m_polygon, ray);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_TRUE(m_state.selected);
        EXPECT_NEAR(m_state.hit.z, Expected[corner], 0.0001f) << corner;
    }
    m_attributes->cells.fill(0x40);
    m_view.fixedHeight = 1201.0f;
    const auto result = PickTerrain(m_state, m_view, {}, m_polygon, Vertical(75.0f, 25.0f, 1500.0f));
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_state.hit.z, 1201.0f);
}

TEST_F(TerrainPickingTest, TheNearestAttemptedFaceWinsEvenWhenItIsVisitedLater)
{
    constexpr std::array<float, 5> Heights{0.0f, 150.0f, 0.0f, 150.0f, 0.0f};
    for (std::size_t x = 0; x < Heights.size(); ++x)
    {
        m_heights->cells[x] = Heights[x];
        m_heights->cells[256 + x] = Heights[x];
    }
    const auto result = PickTerrain(m_state, m_view, {}, m_polygon, {{450.0f, 25.0f, 75.0f}, {-50.0f, 25.0f, 75.0f}});
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_TRUE(m_state.selected);
    EXPECT_EQ(m_state.selectedX, 3.0f);
    EXPECT_EQ(m_state.maxT, 0.2f);
    EXPECT_EQ(m_state.hit, glm::vec3(350.0f, 25.0f, 75.0f));
}

TEST_F(TerrainPickingTest, AMoreDistantLaterHitDoesNotReplaceTheFirst)
{
    constexpr std::array<float, 5> Heights{0.0f, 150.0f, 0.0f, 150.0f, 0.0f};
    for (std::size_t x = 0; x < Heights.size(); ++x)
    {
        m_heights->cells[x] = Heights[x];
        m_heights->cells[256 + x] = Heights[x];
    }
    const auto result = PickTerrain(m_state, m_view, {}, m_polygon, {{-50.0f, 25.0f, 75.0f}, {450.0f, 25.0f, 75.0f}});
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_TRUE(m_state.selected);
    EXPECT_EQ(m_state.selectedX, 0.0f);
    EXPECT_EQ(m_state.maxT, 0.2f);
    EXPECT_EQ(m_state.hit, glm::vec3(50.0f, 25.0f, 75.0f));
}

TEST_F(TerrainPickingTest, PointsImmediatelyInsideTheDiagonalAreAccepted)
{
    const float below = std::nextafter(50.0f, 0.0f);
    const float above = std::nextafter(50.0f, 100.0f);
    for (const float y : {below, above})
    {
        const auto result = PickTerrainTarget(m_state, m_target, m_view, {}, m_polygon, Vertical(50.0f, y));
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(result.Value());
        EXPECT_EQ(m_state.hit, glm::vec3(50.0f, y, 0.0f));
    }
}

TEST_F(TerrainPickingTest, BlockMarginAndCellFrustumAreBothApplied)
{
    // El centro del bloque (2,2) queda fuera, pero pasa con margen -40; sólo pasa el centro (.5,.5).
    const TerrainPolygon thin{.x = {0.0f, 0.75f, 0.75f, 0.0f}, .y = {1.0f, 1.0f, 0.0f, 0.0f}};
    const auto inside = PickTerrain(m_state, m_view, {}, thin, Vertical(75.0f, 25.0f));
    ASSERT_TRUE(inside.HasValue()) << inside.Error();
    EXPECT_TRUE(m_state.selected);
    EXPECT_EQ(m_state.selectedX, 0.0f);
    const auto clipped = PickTerrain(m_state, m_view, {}, thin, Vertical(175.0f, 25.0f));
    ASSERT_TRUE(clipped.HasValue()) << clipped.Error();
    EXPECT_FALSE(m_state.selected);
    EXPECT_EQ(m_state.hit, glm::vec3(75.0f, 25.0f, 0.0f));
}

TEST_F(TerrainPickingTest, ExplicitOverrideAcceptsRejectedBlocksAndCells)
{
    const TerrainPolygon far{.x = {100.0f, 101.0f, 101.0f, 100.0f}, .y = {101.0f, 101.0f, 100.0f, 100.0f}};
    const auto clipped = PickTerrain(m_state, m_view, {}, far, Vertical(175.0f, 25.0f));
    ASSERT_TRUE(clipped.HasValue()) << clipped.Error();
    ExpectRetainedMiss();
    const auto visible = PickTerrain(m_state, m_view, {}, far, Vertical(175.0f, 25.0f), true);
    ASSERT_TRUE(visible.HasValue()) << visible.Error();
    EXPECT_TRUE(m_state.selected);
    EXPECT_EQ(m_state.selectedX, 1.0f);
    EXPECT_EQ(m_state.hit, glm::vec3(175.0f, 25.0f, 0.0f));
}

TEST_F(TerrainPickingTest, NonAlignedBlockStartsAreNotRoundedOrClamped)
{
    const auto result = PickTerrain(m_state, m_view, {1, 5, 1, 1}, m_polygon, Vertical(875.0f, 125.0f));
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_state.selected);
    EXPECT_EQ(m_state.selectedX, 8.0f);
    EXPECT_EQ(m_state.selectedY, 1.0f);
}

TEST_F(TerrainPickingTest, ReversedRangesVisitNoBlocks)
{
    const auto result = PickTerrain(m_state, m_view, {4, 0, 4, 0}, m_polygon, Vertical(75.0f, 25.0f));
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ExpectRetainedMiss();
}

TEST_F(TerrainPickingTest, ConsumerAcceptsTheEqualDistanceOfItsRetry)
{
    const auto result = PickTerrainTarget(m_state, m_target, m_view, {}, m_polygon, Vertical(175.0f, 25.0f));
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_TRUE(result.Value());
    EXPECT_EQ(m_state.maxT, 0.5f);
    EXPECT_EQ(m_state.selectedX, 1.0f);
    EXPECT_EQ(m_target.x, 1u);
    EXPECT_FALSE(m_target.dontMove);
}

TEST_F(TerrainPickingTest, CursorUsesTheFullUnsignedAttributeAndBothExceptions)
{
    struct Case
    {
        std::uint16_t attribute;
        bool dontMove;
    };
    constexpr std::array<Case, 7> Cases{
        {{7, false}, {8, true}, {0x10, true}, {0x100, true}, {0x20, false}, {0x40, false}, {0x60, false}}};
    for (const auto& test : Cases)
    {
        m_attributes->cells[0] = test.attribute;
        const auto result =
            PickTerrainTarget(m_state, m_target, m_view, {}, m_polygon, Vertical(75.0f, 25.0f, 1500.0f));
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_TRUE(result.Value()) << test.attribute;
        EXPECT_EQ(m_target.dontMove, test.dontMove) << test.attribute;
        EXPECT_EQ(m_target.x, 0u);
        EXPECT_EQ(m_target.y, 0u);
    }
}

// Estado retenido fraccional explícito, fuera del stock entero pero dentro del dominio geométrico
// admitido. Permite distinguir el producto retenido 100*0.01f<1 de redondearlo a float o dividir100.
TEST_F(TerrainPickingTest, TargetTruncatesTheUnroundedProductRatherThanTheSelectedCoordinate)
{
    m_state.selectedX = 0.5f;
    m_state.selectedY = 1.5f;
    const auto result = PickTerrainTarget(m_state, m_target, m_view, {}, {}, Vertical(100.0f, 175.0f));
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_TRUE(result.Value());
    EXPECT_EQ(m_state.selectedX, 0.5f);
    EXPECT_EQ(m_state.selectedY, 1.5f);
    EXPECT_EQ(m_state.hit, glm::vec3(100.0f, 175.0f, 0.0f));
    EXPECT_EQ(m_target.x, 0u);
    EXPECT_EQ(m_target.y, 1u);
    m_state.selectedX = 1.5f;
    m_state.selectedY = 0.5f;
    const auto otherAxis = PickTerrainTarget(m_state, m_target, m_view, {}, {}, Vertical(175.0f, 100.0f));
    ASSERT_TRUE(otherAxis.HasValue()) << otherAxis.Error();
    ASSERT_TRUE(otherAxis.Value());
    EXPECT_EQ(m_target.x, 1u);
    EXPECT_EQ(m_target.y, 0u);
}

TEST_F(TerrainPickingTest, ConsumerRetriesTheRetainedCellAfterACulledPass)
{
    const TerrainPolygon outside{};
    const auto ray = Vertical(575.0f, 625.0f);
    const auto pass = PickTerrain(m_state, m_view, {}, outside, ray);
    ASSERT_TRUE(pass.HasValue()) << pass.Error();
    ExpectRetainedMiss();
    const auto target = PickTerrainTarget(m_state, m_target, m_view, {}, outside, ray);
    ASSERT_TRUE(target.HasValue()) << target.Error();
    EXPECT_TRUE(target.Value());
    EXPECT_TRUE(m_state.selected);
    EXPECT_EQ(m_state.maxT, 0.5f);
    EXPECT_EQ(m_state.hit, glm::vec3(575.0f, 625.0f, 0.0f));
    EXPECT_EQ(m_target.x, 5u);
    EXPECT_EQ(m_target.y, 6u);
}

TEST_F(TerrainPickingTest, FailedConsumerRetryDoesNotPublishAnOldTarget)
{
    const auto result = PickTerrainTarget(m_state, m_target, m_view, {}, {}, Vertical(75.0f, 25.0f));
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FALSE(result.Value());
    ExpectRetainedMiss();
    EXPECT_EQ(m_target.x, 91u);
    EXPECT_EQ(m_target.y, 92u);
    EXPECT_TRUE(m_target.dontMove);
}

TEST_F(TerrainPickingTest, TheLastBlockUsesCorner255WithoutWrapping)
{
    m_heights->cells[65535] = 120.0f;
    const auto result = PickTerrain(m_state, m_view, {251, 251, 251, 251}, m_polygon, Vertical(25475.0f, 25425.0f));
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_TRUE(m_state.selected);
    EXPECT_EQ(m_state.selectedX, 254.0f);
    EXPECT_EQ(m_state.selectedY, 254.0f);
    EXPECT_NEAR(m_state.hit.z, 30.0f, 0.0001f);
}

TEST_F(TerrainPickingTest, InvalidInputsAreExplicitErrors)
{
    m_view.heights = nullptr;
    auto result = PickTerrainTarget(m_state, m_target, m_view, {}, m_polygon, Vertical(75.0f, 25.0f));
    EXPECT_FALSE(result.HasValue());
    EXPECT_EQ(m_target.x, 91u);
    m_view.heights = m_heights.get();
    result = PickTerrainTarget(m_state, m_target, m_view, {252, 252, 0, 0}, m_polygon, Vertical(75.0f, 25.0f));
    EXPECT_FALSE(result.HasValue());
    m_heights->cells[0] = std::numeric_limits<float>::quiet_NaN();
    result = PickTerrainTarget(m_state, m_target, m_view, {}, m_polygon, Vertical(75.0f, 25.0f));
    EXPECT_FALSE(result.HasValue());
    EXPECT_EQ(m_target.x, 91u);
}

TEST_F(TerrainPickingTest, AnErrorAfterAnEarlierHitDoesNotPartiallyPublish)
{
    m_heights->cells[3] = std::numeric_limits<float>::infinity();
    const auto result = PickTerrainTarget(m_state, m_target, m_view, {}, m_polygon, Vertical(75.0f, 25.0f));
    ASSERT_FALSE(result.HasValue());
    EXPECT_TRUE(m_state.selected);
    EXPECT_EQ(m_state.selectedX, 5.0f);
    EXPECT_EQ(m_state.maxT, 0.25f);
    EXPECT_EQ(m_state.hit, glm::vec3(11.0f, 22.0f, 33.0f));
    EXPECT_EQ(m_target.x, 91u);
    EXPECT_EQ(m_target.y, 92u);
    EXPECT_TRUE(m_target.dontMove);
}

TEST_F(TerrainPickingTest, ClippedHeightsAreNotReadForValidation)
{
    m_heights->cells.fill(std::numeric_limits<float>::quiet_NaN());
    const auto result = PickTerrain(m_state, m_view, {}, {}, Vertical(75.0f, 25.0f));
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ExpectRetainedMiss();
}

TEST_F(TerrainPickingTest, AnExtremeRayCannotCauseAnUndefinedIntegerConversion)
{
    // La proyección yz puede admitir el punto redondeado de un rayo enorme fuera del dominio de
    // cámara stock. El componente lo informa como error si no admite ftol signed64 ordinario.
    m_heights->cells[1] = 150.0f;
    m_heights->cells[257] = 150.0f;
    const auto result = PickTerrainTarget(m_state, m_target, m_view, {}, m_polygon,
                                          {{-1.0e35f, 25.0f, 75.0f}, {2.0e35f, 25.0f, 75.0f}});
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(m_state.hit, glm::vec3(11.0f, 22.0f, 33.0f));
    EXPECT_EQ(m_target.x, 91u);
    EXPECT_EQ(m_target.y, 92u);
}
} // namespace
