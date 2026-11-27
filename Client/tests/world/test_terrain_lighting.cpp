#include "Terrain/TerrainLighting.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainLight.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>

// Client::Terrain::ShadeTerrainLight and LightAt, with hand-built heights and lights: a cell's shade comes from the
// heights of its neighbors (0x5f8b10 and 0x5f8d90), and the light at a point from the four cells around it
// (0x5f99e0).
namespace
{
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::TerrainHeights;
using Client::Data::Terrain::TerrainLight;
using Client::Data::Terrain::TerrainLightColor;
using Client::Data::Terrain::TerrainSide;
using Client::Terrain::AddTerrainLight;
using Client::Terrain::BrightShadingMap;
using Client::Terrain::CopyTerrainLightWindow;
using Client::Terrain::LightAt;
using Client::Terrain::LoadTerrainLighting;
using Client::Terrain::ShadeTerrainLight;
using Client::Terrain::TerrainLighting;

constexpr std::uint8_t Lorencia = 0;
constexpr TerrainLightColor Base{0.8f, 0.6f, 0.4f};
constexpr float Rounding = 1e-5f;

std::unique_ptr<TerrainHeights> HeightsOf(const std::function<float(std::size_t, std::size_t)>& height)
{
    auto heights = std::make_unique<TerrainHeights>();
    for (std::size_t y = 0; y < TerrainSide; ++y)
    {
        for (std::size_t x = 0; x < TerrainSide; ++x)
        {
            heights->cells[(y * TerrainSide) + x] = height(x, y);
        }
    }
    return heights;
}

std::unique_ptr<TerrainLight> EvenLight()
{
    auto light = std::make_unique<TerrainLight>();
    light->cells.fill(Base);
    return light;
}

// The shade left on cell (x, y): the cell's light over the base one, in the red.
float ShadeAt(const TerrainLighting& lighting, std::uint8_t x, std::uint8_t y)
{
    return lighting.cells[CellIndex(x, y)][0] / Base[0];
}

// Without a slope, the normal is (0, 0, 1) and the shade 0.5 + 0.5: the light stays as it is.
TEST(TerrainLightingTest, AFlatTerrainKeepsItsLight)
{
    const auto lighting =
        ShadeTerrainLight(*EvenLight(), *HeightsOf([](std::size_t, std::size_t) { return 165.0f; }), Lorencia);
    for (const std::size_t cell : {CellIndex(0, 0), CellIndex(128, 77), CellIndex(254, 254)})
    {
        EXPECT_FLOAT_EQ(lighting->cells[cell][0], Base[0]);
        EXPECT_FLOAT_EQ(lighting->cells[cell][1], Base[1]);
        EXPECT_FLOAT_EQ(lighting->cells[cell][2], Base[2]);
    }
}

// Heights that rise 100 per cell in x: the normal is (−1, 0, 1) / √2, and the shade 0.5. Each channel is
// multiplied by it.
TEST(TerrainLightingTest, ASlopeInXShadesHalf)
{
    const auto lighting = ShadeTerrainLight(
        *EvenLight(), *HeightsOf([](std::size_t x, std::size_t) { return 100.0f * static_cast<float>(x); }), Lorencia);
    EXPECT_NEAR(ShadeAt(*lighting, 10, 20), 0.5f, Rounding);
    EXPECT_NEAR(lighting->cells[CellIndex(10, 20)][1], Base[1] * 0.5f, Rounding);
    EXPECT_NEAR(lighting->cells[CellIndex(10, 20)][2], Base[2] * 0.5f, Rounding);
}

// In y the arithmetic gives −0.5 · n.y: the slope rising toward +y gives 1.207 and stays at 1, and the falling one,
// 0.5.
TEST(TerrainLightingTest, TheShadeGoesFromZeroToOne)
{
    const auto up = ShadeTerrainLight(
        *EvenLight(), *HeightsOf([](std::size_t, std::size_t y) { return 100.0f * static_cast<float>(y); }), Lorencia);
    EXPECT_FLOAT_EQ(ShadeAt(*up, 30, 40), 1.0f);
    const auto down = ShadeTerrainLight(
        *EvenLight(), *HeightsOf([](std::size_t, std::size_t y) { return -100.0f * static_cast<float>(y); }), Lorencia);
    EXPECT_NEAR(ShadeAt(*down, 30, 40), 0.5f, Rounding);
    // With x − y times 1000, the normal goes to (−1, 1, 0.1) and the arithmetic gives −0.17: it stays at 0.
    const auto steep =
        ShadeTerrainLight(*EvenLight(),
                          *HeightsOf([](std::size_t x, std::size_t y)
                                     { return 1000.0f * (static_cast<float>(x) - static_cast<float>(y)); }),
                          Lorencia);
    EXPECT_FLOAT_EQ(ShadeAt(*steep, 30, 40), 0.0f);
}

// Map 30 shades with −1 and 1: the slope in x gives 0.5 · (−0.707) + 0.707 + 0.5 = 0.854.
TEST(TerrainLightingTest, MapThirtyShadesBrighter)
{
    const auto heights = HeightsOf([](std::size_t x, std::size_t) { return 100.0f * static_cast<float>(x); });
    const auto lighting = ShadeTerrainLight(*EvenLight(), *heights, BrightShadingMap);
    const float expected = (-0.5f * std::sqrt(0.5f)) + std::sqrt(0.5f) + 0.5f;
    EXPECT_NEAR(ShadeAt(*lighting, 10, 20), expected, Rounding);
}

// A terrain at 0 except the three heights cell (10, 20) uses: those of (11, 20), (11, 21) and (10, 21).
std::unique_ptr<TerrainHeights> CornersOfTenTwenty(float right, float topRight, float top)
{
    return HeightsOf(
        [right, topRight, top](std::size_t x, std::size_t y)
        {
            if (x == 11 && y == 20)
            {
                return right;
            }
            if (x == 11 && y == 21)
            {
                return topRight;
            }
            return x == 10 && y == 21 ? top : 0.0f;
        });
}

std::array<std::uint32_t, 3> BitsOf(const TerrainLightColor& light)
{
    return {std::bit_cast<std::uint32_t>(light[0]), std::bit_cast<std::uint32_t>(light[1]),
            std::bit_cast<std::uint32_t>(light[2])};
}

// The bits of the ASM profile (Client/docs/research/scripts/game/maps/terrain_light.py): the normal of 0x5fd000,
// the shade of 0x5f8d90 in its order and in 53 bits, and each product rounded to 53 bits and then to float. With the
// normal in float (glm::cross and glm::length), with the shade in float, or with the shade stored as float before
// the products, the three channels would give other bits; on map 0, 0x3ee01e7a, 0x3ea816dc and 0x3e601e7a.
TEST(TerrainLightingTest, AShadedCellKeepsTheBitsOfTheAsmProfile)
{
    const auto lighting = ShadeTerrainLight(*EvenLight(), *CornersOfTenTwenty(345.0f, 279.0f, 256.5f), Lorencia);
    EXPECT_EQ(BitsOf(lighting->cells[CellIndex(10, 20)]),
              (std::array<std::uint32_t, 3>{0x3ee01e79U, 0x3ea816dbU, 0x3e601e79U}));
    const auto bright = ShadeTerrainLight(*EvenLight(), *CornersOfTenTwenty(381.0f, 247.5f, 219.0f), BrightShadingMap);
    EXPECT_EQ(BitsOf(bright->cells[CellIndex(10, 20)]),
              (std::array<std::uint32_t, 3>{0x3e326fd9U, 0x3e05d3e3U, 0x3db26fd9U}));
}

// Column 255 looks up the height of x + 1 in a byte, column 0, but the position stays at 25,600: with heights that
// drop 100 per cell in x, the neighbor rises 25,500 in 100 units, the normal goes almost to −x and the shade to
// 0.002. With the height of column 255 the cell would be flat, and the shade 1. The same on row 255 with heights
// that rise in y.
TEST(TerrainLightingTest, TheLastColumnAndRowTakeTheHeightsOfTheFirst)
{
    const float wrapped = (-0.5f * 2550000.0f / std::sqrt((2550000.0f * 2550000.0f) + (10000.0f * 10000.0f))) +
                          (0.5f * 10000.0f / std::sqrt((2550000.0f * 2550000.0f) + (10000.0f * 10000.0f))) + 0.5f;
    const auto alongX = ShadeTerrainLight(
        *EvenLight(), *HeightsOf([](std::size_t x, std::size_t) { return -100.0f * static_cast<float>(x); }), Lorencia);
    EXPECT_FLOAT_EQ(ShadeAt(*alongX, 254, 20), 1.0f);
    EXPECT_NEAR(ShadeAt(*alongX, 255, 20), wrapped, Rounding);
    const auto alongY = ShadeTerrainLight(
        *EvenLight(), *HeightsOf([](std::size_t, std::size_t y) { return 100.0f * static_cast<float>(y); }), Lorencia);
    EXPECT_FLOAT_EQ(ShadeAt(*alongY, 20, 254), 1.0f);
    EXPECT_NEAR(ShadeAt(*alongY, 20, 255), wrapped, Rounding);
}

std::unique_ptr<TerrainLighting> LightingOf(const std::function<float(std::size_t, std::size_t)>& red)
{
    auto lighting = std::make_unique<TerrainLighting>();
    for (std::size_t y = 0; y < TerrainSide; ++y)
    {
        for (std::size_t x = 0; x < TerrainSide; ++x)
        {
            const float value = red(x, y);
            lighting->cells[(y * TerrainSide) + x] = {value, value * 0.5f, 0.0f};
        }
    }
    return lighting;
}

// Between the four cells: with the light x + 10 · y in the red, (150, 250) gives 1.5 + 25.
TEST(TerrainLightingTest, TheLightAtAPointIsBetweenItsFourCells)
{
    const auto lighting = LightingOf([](std::size_t x, std::size_t y)
                                     { return static_cast<float>(x) + (10.0f * static_cast<float>(y)); });
    const glm::vec3 light = LightAt(*lighting, 150.0f, 250.0f);
    EXPECT_NEAR(light.x, 26.5f, Rounding);
    EXPECT_NEAR(light.y, 13.25f, Rounding);
    EXPECT_FLOAT_EQ(light.z, 0.0f);
    // With a different value on each cell, the bilinear is not the plane: (1, 1) at 1 and the rest at 0.
    const auto peak = LightingOf([](std::size_t x, std::size_t y) { return x == 1 && y == 1 ? 1.0f : 0.0f; });
    EXPECT_NEAR(LightAt(*peak, 125.0f, 175.0f).x, 0.25f * 0.75f, Rounding);
}

// In the ordinary band, without the low32 reduction: exactly −100 is rejected; between −100 and 0 the cell is 0
// and it extrapolates.
TEST(TerrainLightingTest, OutsideTheGridThereIsNoLight)
{
    const auto lighting = LightingOf([](std::size_t x, std::size_t) { return 1.0f + static_cast<float>(x); });
    EXPECT_EQ(LightAt(*lighting, -150.0f, 500.0f), glm::vec3(0.0f));
    EXPECT_EQ(LightAt(*lighting, 500.0f, -150.0f), glm::vec3(0.0f));
    EXPECT_EQ(LightAt(*lighting, 25500.0f, 500.0f), glm::vec3(0.0f));
    EXPECT_EQ(LightAt(*lighting, 500.0f, 25500.0f), glm::vec3(0.0f));
    EXPECT_NEAR(LightAt(*lighting, 25499.0f, 500.0f).x, 255.99f, 1e-3f);
    EXPECT_NEAR(LightAt(*lighting, -50.0f, 500.0f).x, 0.5f, Rounding);
}
// Analytic vectors of the 0x5f99e0 contract under the masked portable PC53/RN profile, with a stable table: they
// are not new captures of the original and do not pin its effective CW.
TEST(TerrainLightingTest, LargeFiniteCoordinatesUseSigned64Low32OnBothAxes)
{
    const auto lighting = LightingOf([](std::size_t x, std::size_t y)
                                     { return 1.0f + static_cast<float>(x) + (2.0f * static_cast<float>(y)); });
    // After the F32 store, ±100·2^32 becomes ±2^32: a valid FTOL64, low32=0.
    constexpr float LargeCoordinate = 0x1.9p38f;
    // With V(x,y)=1+x+2·y, the weights stay ±2^32, not their fractional part.
    // In PC53 that leaves ±2^32+1 in X, ±2^33+1 in Y and ±3·2^32+1 on the diagonal;
    // the F32 store loses the +1 in all three.
    for (const float sign : {1.0f, -1.0f})
    {
        const float coordinate = sign * LargeCoordinate;
        const glm::vec3 alongX = LightAt(*lighting, coordinate, 0.0f);
        EXPECT_EQ(alongX, glm::vec3(sign * 0x1p32f, sign * 0x1p31f, 0.0f));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(alongX.z), 0U);
        const glm::vec3 alongY = LightAt(*lighting, 0.0f, coordinate);
        EXPECT_EQ(alongY, glm::vec3(sign * 0x1p33f, sign * 0x1p32f, 0.0f));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(alongY.z), 0U);
        const glm::vec3 diagonal = LightAt(*lighting, coordinate, coordinate);
        EXPECT_EQ(diagonal, glm::vec3(sign * 0x1.8p33f, sign * 0x1.8p32f, 0.0f));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(diagonal.z), 0U);
    }
}

TEST(TerrainLightingTest, Signed64BoundaryCoordinatesContinueWithLow32Zero)
{
    const auto lighting = LightingOf([](std::size_t x, std::size_t y)
                                     { return 1.0f + static_cast<float>(x) + (2.0f * static_cast<float>(y)); });
    // The store gives ±2^63: −2^63 is valid; +2^63 is a masked invalid. Both low32 are 0.
    constexpr float BoundaryCoordinate = 0x1.9p69f;
    // With V(x,y)=1+x+2·y, X alone gives ±2^63 and Y alone ±2^64 after rounding.
    // On the diagonal PC53 rounds L=±2^64+1 and R=±2^64+2 to the same ±2^64:
    // R−L=0, so the result is not ±3·2^63. Green is half of it.
    for (const float sign : {1.0f, -1.0f})
    {
        const float coordinate = sign * BoundaryCoordinate;
        const glm::vec3 alongX = LightAt(*lighting, coordinate, 0.0f);
        EXPECT_EQ(alongX, glm::vec3(sign * 0x1p63f, sign * 0x1p62f, 0.0f));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(alongX.z), 0U);
        const glm::vec3 alongY = LightAt(*lighting, 0.0f, coordinate);
        EXPECT_EQ(alongY, glm::vec3(sign * 0x1p64f, sign * 0x1p63f, 0.0f));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(alongY.z), 0U);
        const glm::vec3 diagonal = LightAt(*lighting, coordinate, coordinate);
        EXPECT_EQ(diagonal, glm::vec3(sign * 0x1p64f, sign * 0x1p63f, 0.0f));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(diagonal.z), 0U);
    }
}

TEST(TerrainLightingTest, SignedLow32ChecksAcceptZeroAnd254AndRejectMinusOneAnd255)
{
    const auto lighting = LightingOf([](std::size_t, std::size_t) { return 1.0f; });
    for (const float coordinate : {0.0f, 25400.0f})
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(LightAt(*lighting, coordinate, 0.0f).x), 0x3f800000U);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(LightAt(*lighting, 0.0f, coordinate).x), 0x3f800000U);
    }
    for (const float coordinate : {-100.0f, 25500.0f})
    {
        for (const glm::vec3 light : {LightAt(*lighting, coordinate, 0.0f), LightAt(*lighting, 0.0f, coordinate)})
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(light.x), 0U);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(light.y), 0U);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(light.z), 0U);
        }
    }
}

TEST(TerrainLightingTest, MaskedInvalidCoordinatesInterpolateInsteadOfReturningZero)
{
    const auto lighting = LightingOf([](std::size_t, std::size_t) { return 1.0f; });
    for (const float coordinate : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                                   -std::numeric_limits<float>::infinity()})
    {
        for (const glm::vec3 light : {LightAt(*lighting, coordinate, 0.0f), LightAt(*lighting, 0.0f, coordinate)})
        {
            // The NaN's sign and payload are not pinned: 0 times a non-finite weight carries on to the interpolation.
            EXPECT_TRUE(std::isnan(light.x));
            EXPECT_TRUE(std::isnan(light.y));
            EXPECT_TRUE(std::isnan(light.z));
        }
        // The other axis does reject: low32=0 does not skip the four spatial guards.
        EXPECT_EQ(std::bit_cast<std::uint32_t>(LightAt(*lighting, coordinate, -100.0f).x), 0U);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(LightAt(*lighting, 25500.0f, coordinate).x), 0U);
    }
}

TEST(TerrainLightingTest, BothScaledCoordinatesAreStoredAsFloatBeforeConversion)
{
    const auto lighting = LightingOf([](std::size_t x, std::size_t y) { return x == 1 && y == 1 ? 0.0f : 1.0f; });
    // 100·K in double is below 1; the F32 store gives exactly 1 on both axes.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(LightAt(*lighting, 100.0f, 100.0f).x), 0U);
}

TEST(TerrainLightingTest, Pc53ColumnsRetainTheBitUntilTheFinalFloatStore)
{
    auto lighting = std::make_unique<TerrainLighting>();
    lighting->cells.fill({0.0f, 0.0f, 0.0f});
    lighting->cells[CellIndex(0, 0)].fill(std::bit_cast<float>(0x3f000000U));
    lighting->cells[CellIndex(0, 1)].fill(std::bit_cast<float>(0x3f000001U));
    // X=1/4, Y=1/2; L=1/2+2^-25, R=0; store(3/8+3·2^-27) = 0x3ec00001.
    // Rounding L to F32 before interpolating X would lose the bit: 0x3ec00000.
    const glm::vec3 light = LightAt(*lighting, 25.0f, 50.0f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light.x), 0x3ec00001U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light.y), 0x3ec00001U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light.z), 0x3ec00001U);
}

// Without the world's folder, the error names the first missing file, in the map's folder (map 55 is World56).
TEST(TerrainLightingTest, LoadingAWorldWithoutItsFilesNamesWhatIsMissing)
{
    const Client::Tests::TemporaryFolder folder("terrain-lighting");
    const auto loaded = LoadTerrainLighting(folder.Path(), 55);
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("World56\\TerrainHeight.OZB"), std::string::npos) << loaded.Error();
}

// Catches losing the inclusive +3, clipping at the edge or resetting the whole table.
TEST(TerrainLightingTest, CopiesOnlyTheInclusiveWrappedWindow)
{
    const auto source = std::make_unique<TerrainLighting>();
    auto destination = std::make_unique<TerrainLighting>();
    source->cells.fill({1.0f, 2.0f, 3.0f});
    destination->cells.fill({7.0f, 8.0f, 9.0f});
    CopyTerrainLightWindow(*source, *destination, -1, 0, 254, 255);
    for (const auto x : {255, 0, 1, 2, 3})
    {
        for (const auto y : {254, 255, 0, 1, 2})
        {
            EXPECT_EQ(destination->cells[CellIndex(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y))],
                      (TerrainLightColor{1.0f, 2.0f, 3.0f}));
        }
    }
    EXPECT_EQ(destination->cells[CellIndex(4, 0)], (TerrainLightColor{7.0f, 8.0f, 9.0f}));
    EXPECT_EQ(destination->cells[CellIndex(0, 3)], (TerrainLightColor{7.0f, 8.0f, 9.0f}));
    EXPECT_EQ(destination->cells[CellIndex(100, 100)], (TerrainLightColor{7.0f, 8.0f, 9.0f}));
}

// Observes the wrap and the copied bits. Copying already visited indices again is idempotent, so the final state does
// not tell walking the whole window from limiting it to 256 contiguous columns.
TEST(TerrainLightingTest, CopiesRepeatedWrapsAndPreservesNanPayloadsAndSignedZero)
{
    const auto source = std::make_unique<TerrainLighting>();
    auto destination = std::make_unique<TerrainLighting>();
    source->cells.fill({2.0f, 3.0f, 4.0f});
    source->cells[CellIndex(0, 255)] = {std::bit_cast<float>(std::uint32_t{0x7f812345}), -0.0f, 0.0f};
    CopyTerrainLightWindow(*source, *destination, -257, 256, -257, -257);
    const TerrainLightColor& copied = destination->cells[CellIndex(0, 255)];
    EXPECT_EQ(std::bit_cast<std::uint32_t>(copied[0]), 0x7f812345u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(copied[1]), 0x80000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(copied[2]), 0u);
    EXPECT_EQ(destination->cells[CellIndex(3, 2)], (TerrainLightColor{2.0f, 3.0f, 4.0f}));
    EXPECT_EQ(destination->cells[CellIndex(3, 3)], TerrainLightColor{});
    CopyTerrainLightWindow(*destination, *destination, -257, 256, -257, -257);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(copied[0]), 0x7f812345u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(copied[1]), 0x80000000u);
}

// An empty window leaves the destination untouched (the scalar block of 0x5fbf60 is outside this API).
TEST(TerrainLightingTest, AnEmptyLightWindowPreservesTheDestination)
{
    const auto source = std::make_unique<TerrainLighting>();
    auto destination = std::make_unique<TerrainLighting>();
    destination->cells.fill({1.0f, 2.0f, 3.0f});
    CopyTerrainLightWindow(*source, *destination, 5, 1, 0, 0);
    CopyTerrainLightWindow(*source, *destination, 0, 0, 5, 1);
    // Normalizing the inverted endpoints would also leave (0,0) untouched: the whole table is observed.
    for (const TerrainLightColor& cell : destination->cells)
    {
        ASSERT_EQ(cell, (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    }
}

// first > last still copies when it reaches the +3 tail. Each cell has a different color; returning early on
// inverted endpoints, normalizing them or writing outside the window must fail.
TEST(TerrainLightingTest, CopiesTheRemainingTailWhenWindowEndpointsAreInverted)
{
    const auto source = std::make_unique<TerrainLighting>();
    auto destination = std::make_unique<TerrainLighting>();
    for (std::size_t y = 0; y < TerrainSide; ++y)
    {
        for (std::size_t x = 0; x < TerrainSide; ++x)
        {
            source->cells[(y * TerrainSide) + x] = {static_cast<float>(x), static_cast<float>(y), 0.25f};
        }
    }
    const TerrainLightColor untouched{-1.0f, -2.0f, -3.0f};
    for (std::int32_t gap = 1; gap <= 3; ++gap)
    {
        destination->cells.fill(untouched);
        CopyTerrainLightWindow(*source, *destination, 5, 5 - gap, 7, 7 - gap);
        const std::size_t lastX = 8 - static_cast<std::size_t>(gap);
        const std::size_t lastY = 10 - static_cast<std::size_t>(gap);
        for (std::size_t y = 0; y < TerrainSide; ++y)
        {
            for (std::size_t x = 0; x < TerrainSide; ++x)
            {
                const std::size_t index = (y * TerrainSide) + x;
                const bool copied = x >= 5 && x <= lastX && y >= 7 && y <= lastY;
                ASSERT_EQ(destination->cells[index], copied ? source->cells[index] : untouched)
                    << "gap=" << gap << ", x=" << x << ", y=" << y;
            }
        }
    }
}

// Exact weights: center 1, cardinal at distance 1 with radius 2 = 1/2, edge = 0.
// Catches another fixed radius, a ceiling at 1, a missing floor and a sum outside the circle.
TEST(TerrainLightingTest, AddsRadialLightWithoutAnUpperClamp)
{
    auto destination = std::make_unique<TerrainLighting>();
    destination->cells.fill({0.25f, 0.25f, 0.25f});
    AddTerrainLight(*destination, 1000.0f, 1000.0f, {2.0f, -2.0f, 0.5f}, 2);
    EXPECT_EQ(destination->cells[CellIndex(10, 10)], (TerrainLightColor{2.25f, 0.0f, 0.75f}));
    EXPECT_EQ(destination->cells[CellIndex(11, 10)], (TerrainLightColor{1.25f, 0.0f, 0.5f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 9)], (TerrainLightColor{1.25f, 0.0f, 0.5f}));
    // Diagonal: weight F32((2 - sqrt(2)) / 2) = 0x3e95f61a. With 2 and a base of 1/4: 0x3f55f61a.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(11, 11)][0]), 0x3f55f61au);
    EXPECT_EQ(destination->cells[CellIndex(12, 10)], (TerrainLightColor{0.25f, 0.25f, 0.25f}));
    EXPECT_EQ(destination->cells[CellIndex(12, 12)], (TerrainLightColor{0.25f, 0.25f, 0.25f}));
}

// On the exact edge the wide weight is zero and there is no store or floor: a negative destination survives.
// Processing that zero too would turn the red into +0, even though the light it adds is zero.
TEST(TerrainLightingTest, SkipsZeroWeightBoundaryCellsWithoutClampingTheirDestination)
{
    auto destination = std::make_unique<TerrainLighting>();
    destination->cells[CellIndex(12, 10)] = {-1.0f, -2.0f, -3.0f};
    AddTerrainLight(*destination, 1000.0f, 1000.0f, {1.0f, 2.0f, 3.0f}, 2);
    EXPECT_EQ(destination->cells[CellIndex(12, 10)], (TerrainLightColor{-1.0f, -2.0f, -3.0f}));
}

// 100 * float(0.01) lands exactly on 1 after the F32 store. The distance uses that stored value.
TEST(TerrainLightingTest, StoresScaledCoordinatesBeforeTheDistanceCalculation)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 100.0f, -100.0f, {1.0f, 2.0f, 3.0f}, 1);
    EXPECT_EQ(destination->cells[CellIndex(1, 255)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(0, 0)], TerrainLightColor{});
    AddTerrainLight(*destination, -50.0f, 0.0f, {4.0f, 5.0f, 6.0f}, 1);
    EXPECT_EQ(destination->cells[CellIndex(0, 0)], (TerrainLightColor{2.0f, 2.5f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(255, 0)], (TerrainLightColor{2.0f, 2.5f, 3.0f}));
}

// 0x5f97f6..0x5f97ff subtracts the cell coordinate from the scaled float32 X without losing its fraction.
// Center X=10.5, radius 2: distances 0.5 and 1.5 give exact weights 3/4 and 1/4.
TEST(TerrainLightingTest, MeasuresRadialDistanceFromTheFractionalX)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 1050.0f, 2000.0f, {4.0f, 8.0f, 12.0f}, 2);
    EXPECT_EQ(destination->cells[CellIndex(10, 20)], (TerrainLightColor{3.0f, 6.0f, 9.0f}));
    EXPECT_EQ(destination->cells[CellIndex(11, 20)], (TerrainLightColor{3.0f, 6.0f, 9.0f}));
    EXPECT_EQ(destination->cells[CellIndex(9, 20)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(12, 20)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(8, 20)], TerrainLightColor{});
}

// 0x5f97ed..0x5f97f4 keeps the Y fraction too. The wrap affects the index, not the distance.
TEST(TerrainLightingTest, MeasuresRadialDistanceFromTheFractionalNegativeY)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 1000.0f, -1050.0f, {4.0f, 8.0f, 12.0f}, 2);
    EXPECT_EQ(destination->cells[CellIndex(10, 245)], (TerrainLightColor{3.0f, 6.0f, 9.0f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 246)], (TerrainLightColor{3.0f, 6.0f, 9.0f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 244)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 247)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 248)], TerrainLightColor{});
}

// sx = 10401874 * 2^-25; toward x=-1, dx = 43956306 * 2^-25 needs more than 24 bits.
// Radius 2: the exact weight is 11576279 * 2^-25. An F32 subtraction would give 0x3eb0a3d8.
TEST(TerrainLightingTest, KeepsTheFractionalXDifferenceWide)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 31.0f, 0.0f, {1.0f, 0.0f, 0.0f}, 2);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(255, 0)][0]), 0x3eb0a3d7u);
}

// The same difference in Y observes the temporary kept for the whole row.
TEST(TerrainLightingTest, KeepsTheFractionalYDifferenceWide)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 0.0f, 31.0f, {1.0f, 0.0f, 0.0f}, 2);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(0, 255)][0]), 0x3eb0a3d7u);
}

// Radius 8 has weight 1/4 at distance 6 and wraps negative x and y.
TEST(TerrainLightingTest, UsesTheSuppliedRadiusAndWrapsBothAxes)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 0.0f, 0.0f, {4.0f, 8.0f, 12.0f}, 8);
    EXPECT_EQ(destination->cells[CellIndex(250, 0)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(0, 250)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(248, 0)], TerrainLightColor{});
}

// sqrt(10) is kept in double: (4 - sqrt(10))/4 rounds to 0x3e5674f9.
// Rounding the root to float first would give 0x3e5674f8 (one bit less).
TEST(TerrainLightingTest, KeepsTheDistanceWideUntilTheWeightStore)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 1000.0f, 1000.0f, {1.0f, 0.0f, 0.0f}, 4);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(13, 11)][0]), 0x3e5674f9u);
}

// PC53/RN: F32((5 - sqrt(2))/5) = 0x3f37979f. Storing the difference as F32 first or dividing in
// float gives 0x3f37979e: the radius is not a power of two and the distance is irrational.
TEST(TerrainLightingTest, KeepsTheDifferenceAndDivisionWideUntilTheWeightStore)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 1000.0f, 1000.0f, {1.0f, 0.0f, 0.0f}, 5);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(11, 11)][0]), 0x3f37979fu);
}

// The stored weight of the diagonal is the same literal as the initial red: -weightF32 + weightF32 = +0.
// Using the wide weight in the product leaves a positive value the floor does not clear.
TEST(TerrainLightingTest, MultipliesTheColorByTheStoredFloatWeight)
{
    auto destination = std::make_unique<TerrainLighting>();
    destination->cells[CellIndex(11, 11)][0] = std::bit_cast<float>(std::uint32_t{0x3e95f61a});
    AddTerrainLight(*destination, 1000.0f, 1000.0f, {-1.0f, 0.0f, 0.0f}, 2);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(11, 11)][0]), 0u);
}

// The sweep of radius 257 reaches (0,0) nine times; five visits have a positive weight:
// the center and four cardinals at 256, with F32 weight 1/257. The stored sum gives the bits 0x3f81fe01;
// it is not deduplicated and the radius is not capped at 256.
TEST(TerrainLightingTest, ALongRadiusAccumulatesRepeatedVisitsToTheSameCell)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 0.0f, 0.0f, {1.0f, 0.0f, 0.0f}, 257);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(0, 0)][0]), 0x3f81fe01u);
}

// The floor applies after each light: reversing them changes the result.
TEST(TerrainLightingTest, SerialLightsPreserveTheirOrderAcrossNegativeClamps)
{
    auto forward = std::make_unique<TerrainLighting>();
    auto reverse = std::make_unique<TerrainLighting>();
    AddTerrainLight(*forward, 0.0f, 0.0f, {-2.0f, 0.0f, 0.0f}, 1);
    AddTerrainLight(*forward, 0.0f, 0.0f, {1.0f, 0.0f, 0.0f}, 1);
    AddTerrainLight(*reverse, 0.0f, 0.0f, {1.0f, 0.0f, 0.0f}, 1);
    AddTerrainLight(*reverse, 0.0f, 0.0f, {-2.0f, 0.0f, 0.0f}, 1);
    EXPECT_EQ(forward->cells[CellIndex(0, 0)][0], 1.0f);
    EXPECT_EQ(reverse->cells[CellIndex(0, 0)][0], 0.0f);
}

// On row 9 the source color rises from 1 to 1.5; row 10 must read that 1.5, not an initial copy.
TEST(TerrainLightingTest, AnAliasedColorIsReadLiveInRowOrder)
{
    auto destination = std::make_unique<TerrainLighting>();
    TerrainLightColor& color = destination->cells[CellIndex(10, 9)];
    color = {1.0f, 1.0f, 1.0f};
    AddTerrainLight(*destination, 1000.0f, 1000.0f, color, 2);
    EXPECT_EQ(color, (TerrainLightColor{1.5f, 1.5f, 1.5f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 10)], (TerrainLightColor{1.5f, 1.5f, 1.5f}));
}

// The source on column 9 rises from 1 to 1.5 before the center on the same row: copying by row fails.
// Walking the columns in descending order would also leave the center at 1.
TEST(TerrainLightingTest, AnAliasedColorIsReadLiveWithinTheSameRow)
{
    auto destination = std::make_unique<TerrainLighting>();
    TerrainLightColor& color = destination->cells[CellIndex(9, 10)];
    color = {1.0f, 1.0f, 1.0f};
    AddTerrainLight(*destination, 1000.0f, 1000.0f, color, 2);
    EXPECT_EQ(color, (TerrainLightColor{1.5f, 1.5f, 1.5f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 10)], (TerrainLightColor{1.5f, 1.5f, 1.5f}));
}

// (13,6) is at distance 5 from the center and gets weight 1/2. By rows it changes before the center;
// by columns it changes after, and the center would get 1 instead of 1.5.
TEST(TerrainLightingTest, VisitsRowsBeforeColumnsWhenTheColorAliasesTheDestination)
{
    auto destination = std::make_unique<TerrainLighting>();
    TerrainLightColor& color = destination->cells[CellIndex(13, 6)];
    color = {1.0f, 1.0f, 1.0f};
    AddTerrainLight(*destination, 1000.0f, 1000.0f, color, 10);
    EXPECT_EQ(color, (TerrainLightColor{1.5f, 1.5f, 1.5f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 10)], (TerrainLightColor{1.5f, 1.5f, 1.5f}));
}

// -denorm_min * 1/4 + -0 is negative in double but stores -0 in float: comparing the store fails.
// Whereas -0 * 1 + -0 equals zero and must keep its sign, without a clamp to +0.
TEST(TerrainLightingTest, ClampsTheWideNegativeSumBeforeReloadingItsNegativeZeroStore)
{
    auto destination = std::make_unique<TerrainLighting>();
    destination->cells.fill({-0.0f, -0.0f, -0.0f});
    AddTerrainLight(*destination, 1000.0f, 1000.0f, {-std::numeric_limits<float>::denorm_min(), -0.0f, 0.0f}, 8);
    const TerrainLightColor& quarter = destination->cells[CellIndex(16, 10)];
    EXPECT_EQ(std::bit_cast<std::uint32_t>(quarter[0]), 0u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(quarter[1]), 0x80000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(10, 10)][1]), 0x80000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(18, 10)][0]), 0x80000000u);
}
} // namespace
