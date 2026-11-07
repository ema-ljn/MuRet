#include "World/TerrainCulling.h"

#include "Core/Expected.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace
{
using Client::World::TerrainPolygon;
using Client::World::TerrainPolygonCameraInput;
using Client::World::TerrainPolygonError;
using Client::World::TerrainPolygonFrameOf;
using Client::World::TerrainPolygonInput;
using Client::World::TerrainPolygonOf;
using Client::World::TerrainPolygonState;

// Oráculo analítico: dimensiones locales literales del contrato, sin selector de perfiles.
// Giros identidad y ±45° (turn=0/+1/−1); seno/coseno de45 almacenados son 0x3f3504f3.
TerrainPolygon AnalyticalPolygon(float farWidth, float nearWidth, float farY, float nearY,
                                 const glm::vec3& translation = {}, int turn = 0)
{
    TerrainPolygon expected;
    const std::array<float, 4> x{-farWidth, farWidth, nearWidth, -nearWidth};
    const std::array<float, 4> y{farY, farY, nearY, nearY};
    const double c = std::bit_cast<float>(0x3f3504f3u);
    const double scale = std::bit_cast<float>(0x3c23d70au);
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const float ox = turn != 0 ? static_cast<float>((-turn * c * y[i]) + (x[i] * c)) : x[i];
        const float oy = turn != 0 ? static_cast<float>((turn * c * x[i]) + (c * y[i])) : y[i];
        const float translatedX = static_cast<float>(static_cast<double>(ox) + translation.x);
        const float translatedY = static_cast<float>(static_cast<double>(oy) + translation.y);
        expected.x[i] = static_cast<float>(translatedX * scale);
        expected.y[i] = static_cast<float>(translatedY * scale);
    }
    return expected;
}

void ExpectBits(const TerrainPolygon& actual, const TerrainPolygon& expected)
{
    EXPECT_EQ(actual.everything, expected.everything);
    for (std::size_t i = 0; i < actual.x.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.x[i]), std::bit_cast<std::uint32_t>(expected.x[i]));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.y[i]), std::bit_cast<std::uint32_t>(expected.y[i]));
    }
}

TerrainPolygonInput IdentityInput()
{
    TerrainPolygonInput input;
    input.map = 55;
    return input;
}

TEST(TerrainPolygonTest, TenProfilesKeepTheirDimensionsAndCornerOrder)
{
    struct Row
    {
        std::int32_t map;
        std::int32_t scene;
        std::int32_t level;
        float objectX;
        float farWidth;
        float nearWidth;
        float farY;
        float nearY;
    };
    // W640: a=1, q=1.1f. T/N son constantes, no F multiplicado por factores recompuestos.
    constexpr std::array<Row, 10> Rows{{
        {6, 2, -91, 0, 3000, 540, 4505, -3570},
        {30, 5, -91, 16500, 2250, 540, 2703, -1428},
        {30, 5, -91, 17700, 1300, 580, 1749, -924},
        {55, 2, 0, 0, 121380, 55080, 129744, -68544},
        {55, 4, 0, 0, 6854.3408203125f, 3110.373046875f, 7326.65673828125f, -3870.686767578125f},
        {55, 5, 0, 0, 1190, 540, 1272, -672},
        {55, 2, 1, 0, 1320, 594, 1431, -756},
        {55, 4, 2, 0, 1430, 594, 1590, -840},
        {55, 5, 3, 0, 1650, 638, 1749, -924},
        {55, 5, 4, 0, 1760, 726, 1802, -952},
    }};
    for (const auto& row : Rows)
    {
        SCOPED_TRACE(row.map);
        SCOPED_TRACE(row.scene);
        SCOPED_TRACE(row.level);
        auto input = IdentityInput();
        input.map = row.map;
        input.scene = row.scene;
        input.objectPosition = {row.objectX, 20000};
        input.translation = {128, -64, 333};
        input.webzenProfile = true;
        input.webzenYaw = true;
        const auto polygon = TerrainPolygonOf(input, row.level);
        ASSERT_TRUE(polygon.HasValue());
        ExpectBits(polygon.Value(), AnalyticalPolygon(row.farWidth, row.nearWidth, row.farY, row.nearY,
                                                      input.translation, row.map == 30));
    }
}

TEST(TerrainPolygonTest, LevelsOneThroughFourDoNotDependOnScene)
{
    for (std::int32_t level = 1; level <= 4; ++level)
    {
        auto input = IdentityInput();
        const auto game = TerrainPolygonOf(input, level);
        ASSERT_TRUE(game.HasValue());
        for (const std::int32_t scene : {2, 4, 5, 261})
        {
            input.scene = scene;
            const auto other = TerrainPolygonOf(input, level);
            ASSERT_TRUE(other.HasValue());
            TerrainPolygon expected = game.Value();
            expected.everything = scene == 261;
            ExpectBits(other.Value(), expected);
        }
    }
}

TEST(TerrainPolygonTest, WidthKeepsSignedIntegerAndProductBeforeThePointOneStore)
{
    struct Row
    {
        std::int32_t width;
        float farWidth;
        float nearWidth;
    };
    // Vectores analíticos binary32, obtenidos de las constantes exactas, no del código bajo prueba.
    constexpr std::array<Row, 10> Rows{{{640, 1650, 638},
                                        {641, 1652.34375f, 638.90625f},
                                        {853, 2149.21875f, 831.03131103515625f},
                                        {12, 178.125f, 68.875f},
                                        {0, 150, 58},
                                        {-640, -1350, -522},
                                        {16777217, 39321752, 15204411},
                                        {2147483647, 5033164800.0f, 1946157056.0f},
                                        {-2147483647 - 1, -5033164800.0f, -1946157056.0f},
                                        {1, 152.34375f, 58.90625f}}};
    for (const auto& row : Rows)
    {
        SCOPED_TRACE(row.width);
        auto input = IdentityInput();
        input.worldWidth = row.width;
        const auto polygon = TerrainPolygonOf(input, 3);
        ASSERT_TRUE(polygon.HasValue());
        ExpectBits(polygon.Value(), AnalyticalPolygon(row.farWidth, row.nearWidth, 1749, -924));
    }
}

TEST(TerrainPolygonTest, SpecialAndLevelZeroWidthsHaveTheirOwnIntermediateStore)
{
    struct Row
    {
        std::int32_t width;
        float specialFar;
        float normalFar;
        float near;
    };
    constexpr std::array<Row, 5> Rows{{{641, 3004.6875f, 1191.859375f, 540.84375f},
                                       {853, 3998.437744140625f, 1586.046875f, 719.71875f},
                                       {0, 0, 0, 0},
                                       {-640, -3000, -1190, -540},
                                       {16777217, 78643208, 31195138, 14155777}}};
    for (const auto& row : Rows)
    {
        auto input = IdentityInput();
        input.worldWidth = row.width;
        const auto normal = TerrainPolygonOf(input, 0);
        ASSERT_TRUE(normal.HasValue());
        ExpectBits(normal.Value(), AnalyticalPolygon(row.normalFar, row.near, 1272, -672));
        input.map = 6;
        input.webzenProfile = true;
        input.webzenYaw = true;
        const auto special = TerrainPolygonOf(input, 9);
        ASSERT_TRUE(special.HasValue());
        ExpectBits(special.Value(), AnalyticalPolygon(row.specialFar, row.near, 4505, -3570));
    }
}

TEST(TerrainPolygonTest, TranslationIsStoredBeforeCellScale)
{
    auto input = IdentityInput();
    input.worldWidth = 641;
    // CercaX=540,84375: guardar la suma con1,1f da0x40ad6c07; escalar sin store da0x40ad6c08.
    input.translation = {1.1f, -1.1f, 333};
    const auto polygon = TerrainPolygonOf(input, 0);
    ASSERT_TRUE(polygon.HasValue());
    ExpectBits(polygon.Value(), AnalyticalPolygon(1191.859375f, 540.84375f, 1272, -672, input.translation));
}

TEST(TerrainPolygonTest, NormalDecisionTruncatesBeforeComparisonAndRetainsEveryLevel)
{
    struct Row
    {
        float distance;
        float target;
        std::int32_t expected;
    };
    constexpr std::array<Row, 6> Rows{{{1001, 1000, 3},
                                       {1000, 1000, 1},
                                       {999, 1000, 1},
                                       {1000.9f, 1000.1f, 1},
                                       {-1000.9f, -1000.1f, 1},
                                       {-999.9f, -1000.1f, 3}}};
    const auto input = IdentityInput();
    for (const auto& row : Rows)
    {
        const auto frame = TerrainPolygonFrameOf({3}, input, {1, row.distance, row.target});
        ASSERT_TRUE(frame.HasValue());
        EXPECT_EQ(frame.Value().nextState.effectiveLevel, row.expected);
        const auto expected = TerrainPolygonOf(input, row.expected);
        ASSERT_TRUE(expected.HasValue());
        ExpectBits(frame.Value().polygon, expected.Value());
    }
    for (std::int32_t level = 1; level <= 4; ++level)
    {
        const auto frame = TerrainPolygonFrameOf({level}, input, {0, 1001, 1000});
        ASSERT_TRUE(frame.HasValue());
        EXPECT_EQ(frame.Value().nextState.effectiveLevel, level);
        const auto expected = TerrainPolygonOf(input, level);
        ASSERT_TRUE(expected.HasValue());
        ExpectBits(frame.Value().polygon, expected.Value());
    }
}

TEST(TerrainPolygonTest, ComparisonUsesSignedLowDwordsOfInt64)
{
    struct Row
    {
        float distance;
        float target;
        std::int32_t expected;
    };
    constexpr std::array<Row, 4> Rows{{{0x1p31f, 0, 1}, {0, 0x1p31f, 3}, {0x1p32f, 0, 1}, {-0x1p32f, -1, 3}}};
    for (const auto& row : Rows)
    {
        const auto frame = TerrainPolygonFrameOf({3}, IdentityInput(), {1, row.distance, row.target});
        ASSERT_TRUE(frame.HasValue());
        EXPECT_EQ(frame.Value().nextState.effectiveLevel, row.expected);
    }
}

TEST(TerrainPolygonTest, Int64BoundaryIsHalfOpenAndInvalidDistancesPrecedeLevelErrors)
{
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const float invalid : {std::nextafter(-0x1p63f, -infinity), 0x1p63f, nan, infinity, -infinity})
    {
        for (const bool invalidTarget : {false, true})
        {
            const TerrainPolygonState previous{-123};
            const auto frame = TerrainPolygonFrameOf(
                previous, IdentityInput(),
                {std::numeric_limits<std::int16_t>::min(), invalidTarget ? 0 : invalid, invalidTarget ? invalid : 0});
            ASSERT_FALSE(frame.HasValue());
            EXPECT_EQ(frame.Error(), TerrainPolygonError::InvalidCameraDistance);
            EXPECT_EQ(previous.effectiveLevel, -123);
        }
    }
    for (const float valid : {-0x1p63f, std::nextafter(0x1p63f, 0.0f)})
    {
        const auto frame = TerrainPolygonFrameOf({3}, IdentityInput(), {1, valid, valid});
        ASSERT_TRUE(frame.HasValue());
        EXPECT_EQ(frame.Value().nextState.effectiveLevel, 1);
    }
}

TEST(TerrainPolygonTest, OnlyTheChosenLevelIsValidatedAndAValidRequestCanHealState)
{
    const auto input = IdentityInput();
    for (const std::int16_t invalid : {std::numeric_limits<std::int16_t>::min(), std::int16_t{-1}, std::int16_t{5},
                                       std::numeric_limits<std::int16_t>::max()})
    {
        const auto retained = TerrainPolygonFrameOf({3}, input, {invalid, 1001, 1000});
        ASSERT_TRUE(retained.HasValue());
        EXPECT_EQ(retained.Value().nextState.effectiveLevel, 3);
        const auto adopted = TerrainPolygonFrameOf({3}, input, {invalid, 1000, 1000});
        ASSERT_FALSE(adopted.HasValue());
        EXPECT_EQ(adopted.Error(), TerrainPolygonError::InvalidEffectiveLevel);
    }
    for (const std::int32_t invalid :
         {std::numeric_limits<std::int32_t>::min(), -1, 5, std::numeric_limits<std::int32_t>::max()})
    {
        const TerrainPolygonState previous{invalid};
        const auto retained = TerrainPolygonFrameOf(previous, input, {2, 1001, 1000});
        ASSERT_FALSE(retained.HasValue());
        EXPECT_EQ(retained.Error(), TerrainPolygonError::InvalidEffectiveLevel);
        EXPECT_EQ(previous.effectiveLevel, invalid);
        const auto healed = TerrainPolygonFrameOf(previous, input, {2, 1000, 1000});
        ASSERT_TRUE(healed.HasValue());
        EXPECT_EQ(healed.Value().nextState.effectiveLevel, 2);
        const auto query = TerrainPolygonOf(input, invalid);
        ASSERT_FALSE(query.HasValue());
        EXPECT_EQ(query.Error(), TerrainPolygonError::InvalidEffectiveLevel);
    }
}

TEST(TerrainPolygonTest, InterleavedSpecialsPreserveStateWithoutConsumingInvalidCamera)
{
    auto input = IdentityInput();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const TerrainPolygonCameraInput invalid{std::numeric_limits<std::int16_t>::max(), nan,
                                            std::numeric_limits<float>::infinity()};
    for (const std::int32_t initial : {3, -1, std::numeric_limits<std::int32_t>::max()})
    {
        TerrainPolygonState state{initial};
        for (const std::int32_t map : {6, 30, 30})
        {
            input.map = map;
            input.webzenProfile = true;
            input.objectPosition = {input.objectPosition.x == 16500 ? 17700 : 16500, 20000};
            const auto frame = TerrainPolygonFrameOf(state, input, invalid);
            ASSERT_TRUE(frame.HasValue());
            EXPECT_EQ(frame.Value().nextState.effectiveLevel, initial);
            const auto query = TerrainPolygonOf(input, initial);
            ASSERT_TRUE(query.HasValue());
            ExpectBits(frame.Value().polygon, query.Value());
            state = frame.Value().nextState;
        }
        input.map = 55;
        const auto normal = TerrainPolygonFrameOf(state, input, {2, 0, 0});
        ASSERT_TRUE(normal.HasValue());
        EXPECT_EQ(normal.Value().nextState.effectiveLevel, 2);
    }
}

TEST(TerrainPolygonTest, CastleBandsObserveEachAdjacentFloatAtAllSixBounds)
{
    struct Boundary
    {
        bool x;
        float coordinate;
        std::array<bool, 3> large;
    };
    constexpr std::array<Boundary, 6> Bounds{{{true, 16100, {false, true, true}},
                                              {true, 17100, {true, false, false}},
                                              {true, 18300, {false, false, true}},
                                              {true, 19000, {true, true, false}},
                                              {false, 18900, {false, true, true}},
                                              {false, 21700, {true, true, false}}}};
    for (const auto& bound : Bounds)
    {
        const std::array<float, 3> values{std::nextafter(bound.coordinate, -INFINITY), bound.coordinate,
                                          std::nextafter(bound.coordinate, INFINITY)};
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            SCOPED_TRACE(bound.coordinate);
            SCOPED_TRACE(i);
            TerrainPolygonInput input;
            input.map = 30;
            input.objectPosition = bound.x ? glm::vec2{values[i], 20000} : glm::vec2{16500, values[i]};
            input.cameraYaw = std::numeric_limits<float>::quiet_NaN(); // No se consume en mapa30.
            const auto polygon = TerrainPolygonOf(input, -1);
            ASSERT_TRUE(polygon.HasValue());
            ExpectBits(polygon.Value(), bound.large[i] ? AnalyticalPolygon(2250, 540, 2703, -1428, {}, true)
                                                       : AnalyticalPolygon(1300, 580, 1749, -924, {}, true));
        }
    }
    TerrainPolygonInput input;
    input.map = 30;
    input.objectPosition = {18500, 20000};
    const auto rightBand = TerrainPolygonOf(input, -1);
    ASSERT_TRUE(rightBand.HasValue());
    ExpectBits(rightBand.Value(), AnalyticalPolygon(2250, 540, 2703, -1428, {}, true));
}

TEST(TerrainPolygonTest, CastleUnorderedAndInfiniteSelectorsUseSmallProfile)
{
    for (const float coordinate : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                                   -std::numeric_limits<float>::infinity()})
    {
        for (const bool x : {true, false})
        {
            TerrainPolygonInput input;
            input.map = 30;
            input.objectPosition = x ? glm::vec2{coordinate, 20000} : glm::vec2{16500, coordinate};
            const auto polygon = TerrainPolygonOf(input, 9);
            ASSERT_TRUE(polygon.HasValue());
            ExpectBits(polygon.Value(), AnalyticalPolygon(1300, 580, 1749, -924, {}, true));
        }
    }
}

TEST(TerrainPolygonTest, ObjectSelectorAndTranslationAreIndependentAndZDoesNotSelect)
{
    TerrainPolygonInput input;
    input.map = 30;
    input.objectPosition = {16500, 20000};
    input.translation = {-10, 20, 1};
    const auto original = TerrainPolygonOf(input, 0);
    ASSERT_TRUE(original.HasValue());
    ExpectBits(original.Value(), AnalyticalPolygon(2250, 540, 2703, -1428, input.translation, true));
    input.translation = {17700, 20000, -100000};
    const auto moved = TerrainPolygonOf(input, 0);
    ASSERT_TRUE(moved.HasValue());
    ExpectBits(moved.Value(), AnalyticalPolygon(2250, 540, 2703, -1428, input.translation, true));
    input.objectPosition.x = 17700;
    const auto small = TerrainPolygonOf(input, 0);
    ASSERT_TRUE(small.HasValue());
    ExpectBits(small.Value(), AnalyticalPolygon(1300, 580, 1749, -924, input.translation, true));
}

TEST(TerrainPolygonTest, WebzenObservationsIndependentlyChooseProfileAndYaw)
{
    for (const bool profile : {false, true})
    {
        for (const bool yaw : {false, true})
        {
            auto input = IdentityInput();
            input.map = 6;
            input.cameraYaw = 45;
            input.webzenProfile = profile;
            input.webzenYaw = yaw;
            const auto frame = TerrainPolygonFrameOf({3}, input, {0, 0, 0});
            ASSERT_TRUE(frame.HasValue());
            EXPECT_EQ(frame.Value().nextState.effectiveLevel, profile ? 3 : 0);
            ExpectBits(frame.Value().polygon, profile ? AnalyticalPolygon(3000, 540, 4505, -3570, {}, yaw ? -1 : 1)
                                                      : AnalyticalPolygon(1190, 540, 1272, -672, {}, yaw ? -1 : 1));
        }
    }
}

TEST(TerrainPolygonTest, WebzenIsIrrelevantOutsideMapSixAndCastleRequiresSceneFive)
{
    for (const std::int32_t map : {0, 30, 55})
    {
        auto input = IdentityInput();
        input.map = map;
        input.scene = 4;
        input.cameraYaw = 45;
        const auto plain = TerrainPolygonFrameOf({3}, input, {0, 0, 0});
        ASSERT_TRUE(plain.HasValue());
        input.webzenProfile = true;
        input.webzenYaw = true;
        const auto named = TerrainPolygonFrameOf({3}, input, {0, 0, 0});
        ASSERT_TRUE(named.HasValue());
        EXPECT_EQ(named.Value().nextState.effectiveLevel, 0);
        ExpectBits(named.Value().polygon, plain.Value().polygon);
        ExpectBits(plain.Value().polygon, AnalyticalPolygon(6854.3408203125f, 3110.373046875f, 7326.65673828125f,
                                                            -3870.686767578125f, {}, map == 55 ? -1 : 1));
    }
}

TEST(TerrainPolygonTest, FullWidthMapAndSceneIdentitiesDoNotAliasLowBytes)
{
    for (const std::int32_t map : {262, 286, 311})
    {
        TerrainPolygonInput input;
        input.map = map;
        input.webzenProfile = true;
        input.webzenYaw = true;
        input.objectPosition = {16500, 20000};
        const auto frame = TerrainPolygonFrameOf({3}, input, {0, 0, 0});
        ASSERT_TRUE(frame.HasValue());
        EXPECT_EQ(frame.Value().nextState.effectiveLevel, 0);
        ExpectBits(frame.Value().polygon, AnalyticalPolygon(1190, 540, 1272, -672, {}, true));
    }
    for (const std::int32_t scene : {258, 260, 261})
    {
        auto input = IdentityInput();
        input.scene = scene;
        input.map = 30;
        input.objectPosition = {16500, 20000};
        const auto frame = TerrainPolygonFrameOf({3}, input, {0, 0, 0});
        ASSERT_TRUE(frame.HasValue());
        EXPECT_EQ(frame.Value().nextState.effectiveLevel, 0);
        auto expected = AnalyticalPolygon(1190, 540, 1272, -672, {}, true);
        expected.everything = true;
        ExpectBits(frame.Value().polygon, expected);
    }
}

TEST(TerrainPolygonTest, OtherComputesAndUpdatesWhileLegacyKeepsPositiveZeroSentinel)
{
    auto input = IdentityInput();
    input.scene = 0;
    const auto frame = TerrainPolygonFrameOf({3}, input, {4, 0, 0});
    ASSERT_TRUE(frame.HasValue());
    EXPECT_EQ(frame.Value().nextState.effectiveLevel, 4);
    auto expected = AnalyticalPolygon(1760, 726, 1802, -952);
    expected.everything = true;
    ExpectBits(frame.Value().polygon, expected);
    EXPECT_TRUE(Client::World::PolygonContains(frame.Value().polygon, 1e12, 1e12, 0));
    input.scene = 5;
    const auto returned = TerrainPolygonFrameOf(frame.Value().nextState, input, {0, 1001, 1000});
    ASSERT_TRUE(returned.HasValue());
    EXPECT_EQ(returned.Value().nextState.effectiveLevel, 4);
    expected.everything = false;
    ExpectBits(returned.Value().polygon, expected);
    const auto legacy = TerrainPolygonOf(Client::World::CullingScene::Other, 55, {}, 0);
    TerrainPolygon sentinel;
    sentinel.everything = true;
    ExpectBits(legacy, sentinel);
}

TEST(TerrainPolygonTest, QueryFreezesLevelWhileGeometryChangesAndFrameFailureLeavesSnapshot)
{
    const TerrainPolygonState previous{3};
    auto input = IdentityInput();
    for (const float x : {0.0f, 123.0f, -456.0f})
    {
        input.translation.x = x;
        const auto query = TerrainPolygonOf(input, previous.effectiveLevel);
        ASSERT_TRUE(query.HasValue());
        ExpectBits(query.Value(), AnalyticalPolygon(1650, 638, 1749, -924, input.translation));
        const auto frame = TerrainPolygonFrameOf(previous, input, {0, 1001, 1000});
        ASSERT_TRUE(frame.HasValue());
        ExpectBits(query.Value(), frame.Value().polygon);
        EXPECT_EQ(previous.effectiveLevel, 3);
    }
    const auto adopted = TerrainPolygonFrameOf(previous, input, {2, 0, 0});
    ASSERT_TRUE(adopted.HasValue());
    const auto query = TerrainPolygonOf(input, adopted.Value().nextState.effectiveLevel);
    ASSERT_TRUE(query.HasValue());
    ExpectBits(query.Value(), adopted.Value().polygon);
    const auto failed = TerrainPolygonFrameOf(previous, input, {-1, 0, 0});
    ASSERT_FALSE(failed.HasValue());
    EXPECT_EQ(previous.effectiveLevel, 3);
}

TEST(TerrainPolygonTest, LegacySelection54KeepsHistoricalCornerBits)
{
    const auto polygon = TerrainPolygonOf(Client::World::CullingScene::CharacterSelect, 54, {12975, 15423, 333}, -45);
    constexpr std::array<std::uint32_t, 4> X{0x41ebcd38u, 0x42fcd209u, 0x43331d12u, 0x43072050u};
    constexpr std::array<std::uint32_t, 4> Y{0x431d91ddu, 0x437e813bu, 0x4314da90u, 0x42d1bba0u};
    EXPECT_FALSE(polygon.everything);
    for (std::size_t i = 0; i < X.size(); ++i)
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(polygon.x[i]), X[i]);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(polygon.y[i]), Y[i]);
    }
}
} // namespace
