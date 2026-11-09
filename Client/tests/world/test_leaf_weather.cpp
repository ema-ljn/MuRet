#include "World/LeafWeather.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

// Oráculos literales de 530490-contract.md y leaf-4303c0-contract.md; sin expectativas del port.
namespace
{
using Client::World::AdvanceLeafWeatherPhase;
using Client::World::LeafWeather;
using Client::World::PrepareLeafWeather;

TEST(LeafWeatherTest, EverySupportedMapUsesItsOwnPrefix)
{
    constexpr std::array<std::size_t, 37> Expected{80, 80, 80, 80, 80, 80, 80, 80, 80, 200, 80, 80, 80,
                                                   80, 80, 80, 80, 80, 80, 80, 80, 80, 80,  80, 80, 80,
                                                   80, 80, 80, 80, 40, 80, 80, 80, 80, 80,  80};
    for (std::size_t map = 0; map < Expected.size(); ++map)
    {
        LeafWeather weather{.target = 17, .current = 17};
        EXPECT_EQ(PrepareLeafWeather(weather, static_cast<std::int32_t>(map), 0), Expected[map]) << map;
    }
    LeafWeather weather;
    EXPECT_EQ(PrepareLeafWeather(weather, 54, 0), 80u);
    EXPECT_EQ(PrepareLeafWeather(weather, 55, 0), 80u);
}

TEST(LeafWeatherTest, OnlyMapThirtyFourReadsTheSelectorForItsPrefix)
{
    for (const std::int32_t selector : {-1, 0, 1, 2, 3})
    {
        LeafWeather weather;
        EXPECT_EQ(PrepareLeafWeather(weather, 9, selector), 200u);
        EXPECT_EQ(PrepareLeafWeather(weather, 30, selector), 40u);
        EXPECT_EQ(PrepareLeafWeather(weather, 55, selector), 80u);
        EXPECT_EQ(PrepareLeafWeather(weather, 0, selector), 80u);
    }
    LeafWeather weather;
    EXPECT_EQ(PrepareLeafWeather(weather, 34, 1), 60u);
    EXPECT_EQ(PrepareLeafWeather(weather, 34, 2), 50u);
    EXPECT_EQ(PrepareLeafWeather(weather, 34, -1), 80u);
    EXPECT_EQ(PrepareLeafWeather(weather, 34, 3), 80u);
    EXPECT_EQ(PrepareLeafWeather(weather, 290, 1), 80u) << "la igualdad del mapa usa los 32 bits";
}

TEST(LeafWeatherTest, ForcedTargetPersistsAfterLeavingItsMaps)
{
    for (const std::int32_t map : {10, 18, 19, 20, 21, 22, 23})
    {
        LeafWeather weather{.target = -7, .current = 102, .firstWave = 19, .secondWave = -3, .phase = 55};
        EXPECT_EQ(PrepareLeafWeather(weather, map, 2), 80u);
        EXPECT_EQ(weather.target, 100) << map;
        EXPECT_EQ(weather.current, 101) << map;
        EXPECT_EQ(weather.firstWave, 19);
        EXPECT_EQ(weather.secondWave, -3);
        EXPECT_EQ(weather.phase, 55);
        EXPECT_EQ(PrepareLeafWeather(weather, 0, 0), 80u);
        EXPECT_EQ(weather.target, 100);
        EXPECT_EQ(weather.current, 100);
    }
}

TEST(LeafWeatherTest, OtherMapsKeepTheirTargetAndMoveOnlyOneStep)
{
    for (const std::int32_t map : {-1, 0, 9, 17, 24, 30, 34, 36, 54, 55, 256})
    {
        LeafWeather weather{.target = -9, .current = -11};
        static_cast<void>(PrepareLeafWeather(weather, map, 0));
        EXPECT_EQ(weather.target, -9) << map;
        EXPECT_EQ(weather.current, -10) << map;
        static_cast<void>(PrepareLeafWeather(weather, map, 0));
        EXPECT_EQ(weather.current, -9);
        static_cast<void>(PrepareLeafWeather(weather, map, 0));
        EXPECT_EQ(weather.current, -9) << "la igualdad no se pasa";
    }
}

TEST(LeafWeatherTest, CurrentUsesSignedComparisonWithoutOverflow)
{
    LeafWeather up{.target = std::numeric_limits<std::int32_t>::max(),
                   .current = std::numeric_limits<std::int32_t>::min()};
    static_cast<void>(PrepareLeafWeather(up, 0, 0));
    EXPECT_EQ(up.current, -2147483647);
    LeafWeather down{.target = std::numeric_limits<std::int32_t>::min(),
                     .current = std::numeric_limits<std::int32_t>::max()};
    static_cast<void>(PrepareLeafWeather(down, 0, 0));
    EXPECT_EQ(down.current, 2147483646);
}

TEST(LeafWeatherTest, PhaseWrapsBeforeTheSignedRemainder)
{
    struct Case
    {
        std::int32_t before;
        std::int32_t after;
    };
    constexpr std::array Cases{Case{-21, -1},
                               Case{-20, 0},
                               Case{-19, 1},
                               Case{1980, 0},
                               Case{1990, 10},
                               Case{2147483647, -1629},
                               Case{-2147483647 - 1, -1628}};
    for (const Case& sample : Cases)
    {
        LeafWeather weather{.target = 11, .current = 9, .firstWave = 7, .secondWave = -5, .phase = sample.before};
        AdvanceLeafWeatherPhase(weather);
        EXPECT_EQ(weather.phase, sample.after) << sample.before;
        EXPECT_EQ(weather.target, 11);
        EXPECT_EQ(weather.current, 9);
        EXPECT_EQ(weather.firstWave, 7);
        EXPECT_EQ(weather.secondWave, -5);
    }
}
} // namespace
