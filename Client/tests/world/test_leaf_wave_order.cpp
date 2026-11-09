#include "World/LeafWeather.h"

#include "Core/Expected.h"
#include "World/ILeafWaveEvaluator.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <system_error>
#include <vector>

namespace
{
using Client::Core::Expected;
using Client::Core::Unexpected;
using Client::World::ILeafWaveEvaluator;
using Client::World::LeafWave;
using Client::World::LeafWeather;
using Client::World::StepLeafWeatherWaves;

// Spy de wave-order-v1: observa lecturas/stores/orden en las fronteras de los callbacks.
// V y Q no tienen observación intermedia; su orden requiere inspección estática.
// No acredita FSIN ni el binding x87/69fd3c.
class WaveSpy final : public ILeafWaveEvaluator
{
public:
    WaveSpy(LeafWeather& weather, float& time) : m_weather(weather), m_time(time) {}

    Expected<void, std::error_code> Evaluate(LeafWave wave, float time) override
    {
        events.push_back(wave == LeafWave::First ? 1 : 3);
        EXPECT_EQ(time, wave == LeafWave::First ? 10 : 20);
        EXPECT_EQ(m_weather.firstWave, 71);
        ExpectUnchangedState();
        if (failAt == events.size())
            return Unexpected(error);
        return {};
    }

    Expected<std::int32_t, std::error_code> Truncate() override
    {
        const bool first = events.size() == 1;
        events.push_back(first ? 2 : 4);
        EXPECT_EQ(m_weather.firstWave, first ? 71 : expectedFirst);
        ExpectUnchangedState();
        if (failAt == events.size())
            return Unexpected(error);
        if (first)
            m_time = 20;
        else if (phaseAfterSecondTruncation.has_value())
            m_weather.phase = *phaseAfterSecondTruncation;
        return first ? firstResult : secondResult;
    }

    std::int32_t firstResult = -2;
    std::int32_t secondResult = -1;
    std::int32_t expectedFirst = 10;
    std::int32_t expectedPhase = 1990;
    std::optional<std::int32_t> phaseAfterSecondTruncation;
    std::size_t failAt = 0;
    std::error_code error = std::make_error_code(std::errc::io_error);
    std::vector<int> events;

private:
    void ExpectUnchangedState() const
    {
        EXPECT_EQ(m_weather.target, 101);
        EXPECT_EQ(m_weather.current, -5);
        EXPECT_EQ(m_weather.secondWave, 72);
        EXPECT_EQ(m_weather.phase, expectedPhase);
    }

    LeafWeather& m_weather;
    float& m_time;
};

TEST(LeafWaveOrderTest, RereadsTimeAndStoresFirstWaveBetweenSecondSineAndSecondTruncation)
{
    LeafWeather weather{101, -5, 71, 72, 1990};
    float time = 10;
    WaveSpy spy(weather, time);
    const auto result = StepLeafWeatherWaves(weather, time, &spy);
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(spy.events, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_EQ(weather.firstWave, 10);
    EXPECT_EQ(weather.secondWave, -20);
    EXPECT_EQ(weather.phase, 10);
    EXPECT_EQ(weather.target, 101);
    EXPECT_EQ(weather.current, -5);
}

TEST(LeafWaveOrderTest, RereadsPhaseChangedBySecondTruncation)
{
    LeafWeather weather{101, -5, 71, 72, 1990};
    float time = 10;
    WaveSpy spy(weather, time);
    // H2 cambia la fase viva: Q debe partir de -30, no de la fase de entrada 1990.
    spy.phaseAfterSecondTruncation = -30;
    const auto result = StepLeafWeatherWaves(weather, time, &spy);
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(spy.events, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_EQ(weather.firstWave, 10);
    EXPECT_EQ(weather.secondWave, -20);
    EXPECT_EQ(weather.phase, -10);
    EXPECT_EQ(weather.target, 101);
    EXPECT_EQ(weather.current, -5);
}

TEST(LeafWaveOrderTest, IntegerStoresWrapBeforePhaseAdvances)
{
    LeafWeather weather{101, -5, 71, 72, 1990};
    float time = 10;
    WaveSpy spy(weather, time);
    spy.firstResult = std::numeric_limits<std::int32_t>::max();
    spy.secondResult = std::numeric_limits<std::int32_t>::min();
    spy.expectedFirst = 20;
    ASSERT_TRUE(StepLeafWeatherWaves(weather, time, &spy).HasValue());
    EXPECT_EQ(weather.firstWave, 20);
    EXPECT_EQ(weather.secondWave, 0);
    EXPECT_EQ(weather.phase, 10);
    EXPECT_EQ(weather.target, 101);
    EXPECT_EQ(weather.current, -5);
}

TEST(LeafWaveOrderTest, NegativeFirstAndPositiveSecondResultsWrapIndependently)
{
    LeafWeather weather{101, -5, 71, 72, 1990};
    float time = 10;
    WaveSpy spy(weather, time);
    spy.firstResult = std::numeric_limits<std::int32_t>::min();
    spy.secondResult = std::numeric_limits<std::int32_t>::max();
    spy.expectedFirst = 30;
    ASSERT_TRUE(StepLeafWeatherWaves(weather, time, &spy).HasValue());
    EXPECT_EQ(weather.firstWave, 30);
    EXPECT_EQ(weather.secondWave, -20);
    EXPECT_EQ(weather.phase, 10);
    EXPECT_EQ(weather.target, 101);
    EXPECT_EQ(weather.current, -5);
}

TEST(LeafWaveOrderTest, FailureStopsAtEachBoundaryAndPreservesPriorStores)
{
    for (const std::size_t boundary : std::array<std::size_t, 4>{1, 2, 3, 4})
    {
        LeafWeather weather{101, -5, 71, 72, 1990};
        float time = 10;
        WaveSpy spy(weather, time);
        spy.failAt = boundary;
        // Valores y categorías distintos impiden fabricar un io_error genérico al propagar.
        spy.error = std::error_code(static_cast<int>(boundary) + 100,
                                    boundary % 2 == 0 ? std::system_category() : std::generic_category());
        const std::error_code expectedError = spy.error;
        const auto result = StepLeafWeatherWaves(weather, time, &spy);
        ASSERT_FALSE(result.HasValue()) << boundary;
        EXPECT_EQ(result.Error(), expectedError);
        EXPECT_EQ(spy.events.size(), boundary);
        EXPECT_EQ(weather.firstWave, boundary == 4 ? 10 : 71);
        EXPECT_EQ(weather.secondWave, 72);
        EXPECT_EQ(weather.phase, 1990);
        EXPECT_EQ(weather.target, 101);
        EXPECT_EQ(weather.current, -5);
    }
}

TEST(LeafWaveOrderTest, MissingNumericBindingReturnsAnErrorWithoutFabricatingWaves)
{
    LeafWeather weather{101, -5, 71, 72, 1990};
    const float time = 10;
    const auto result = StepLeafWeatherWaves(weather, time, nullptr);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::make_error_code(std::errc::function_not_supported));
    EXPECT_EQ(weather.firstWave, 71);
    EXPECT_EQ(weather.secondWave, 72);
    EXPECT_EQ(weather.phase, 1990);
    EXPECT_EQ(weather.target, 101);
    EXPECT_EQ(weather.current, -5);
}

TEST(LeafWaveOrderTest, PhaseAddsWithWrap32BeforeSignedRemainder)
{
    constexpr std::array<std::array<std::int32_t, 2>, 6> cases{
        {{2147483647, -1629}, {-2147483647 - 1, -1628}, {-30, -10}, {-20, 0}, {1980, 0}, {1990, 10}}};
    for (const auto& [initial, expected] : cases)
    {
        LeafWeather weather{101, -5, 71, 72, initial};
        float time = 10;
        WaveSpy spy(weather, time);
        spy.expectedPhase = initial;
        ASSERT_TRUE(StepLeafWeatherWaves(weather, time, &spy).HasValue()) << initial;
        EXPECT_EQ(weather.firstWave, 10);
        EXPECT_EQ(weather.secondWave, -20);
        EXPECT_EQ(weather.phase, expected) << initial;
        EXPECT_EQ(weather.target, 101);
        EXPECT_EQ(weather.current, -5);
    }
}
} // namespace
