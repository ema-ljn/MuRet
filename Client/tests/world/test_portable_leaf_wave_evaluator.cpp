#include "World/PortableLeafWaveEvaluator.h"

#include "Core/Expected.h"
#include "World/ILeafWaveEvaluator.h"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <system_error>

namespace
{
using Client::World::LeafWave;
using Client::World::PortableLeafWaveEvaluator;

TEST(PortableLeafWaveEvaluatorTest, MaskedInvalidNaNReturnsZeroAndConsumesPendingValue)
{
    for (const auto wave : std::array{LeafWave::First, LeafWave::Second})
    {
        PortableLeafWaveEvaluator evaluator;
        ASSERT_TRUE(evaluator.Evaluate(wave, std::numeric_limits<float>::quiet_NaN()).HasValue());
        const auto invalid = evaluator.Truncate();
        ASSERT_TRUE(invalid.HasValue());
        EXPECT_EQ(invalid.Value(), 0);

        const auto consumed = evaluator.Truncate();
        ASSERT_FALSE(consumed.HasValue());
        EXPECT_EQ(consumed.Error(), std::make_error_code(std::errc::invalid_argument));

        ASSERT_TRUE(evaluator.Evaluate(wave, 40.0f).HasValue());
        const auto recovered = evaluator.Truncate();
        ASSERT_TRUE(recovered.HasValue());
        EXPECT_EQ(recovered.Value(), 0);
    }
}

TEST(PortableLeafWaveEvaluatorTest, TruncatesBothNormalWavesTowardZero)
{
    PortableLeafWaveEvaluator evaluator;
    for (const auto wave : std::array{LeafWave::First, LeafWave::Second})
    {
        for (const float time : std::array{40.0f, -40.0f})
        {
            ASSERT_TRUE(evaluator.Evaluate(wave, time).HasValue());
            const auto result = evaluator.Truncate();
            ASSERT_TRUE(result.HasValue());
            // En estos tiempos los dos senos están estrictamente entre -1 y 1.
            EXPECT_EQ(result.Value(), 0);
        }
    }
}

TEST(PortableLeafWaveEvaluatorTest, TruncateInIdleReturnsInvalidArgumentAndAllowsRecovery)
{
    PortableLeafWaveEvaluator evaluator;
    const auto idle = evaluator.Truncate();
    ASSERT_FALSE(idle.HasValue());
    EXPECT_EQ(idle.Error(), std::make_error_code(std::errc::invalid_argument));

    ASSERT_TRUE(evaluator.Evaluate(LeafWave::First, 40.0f).HasValue());
    const auto recovered = evaluator.Truncate();
    ASSERT_TRUE(recovered.HasValue());
    EXPECT_EQ(recovered.Value(), 0);
}

TEST(PortableLeafWaveEvaluatorTest, SecondEvaluateDiscardsPendingValueAndAllowsRecovery)
{
    PortableLeafWaveEvaluator evaluator;
    ASSERT_TRUE(evaluator.Evaluate(LeafWave::First, 40.0f).HasValue());
    const auto duplicate = evaluator.Evaluate(LeafWave::Second, -40.0f);
    ASSERT_FALSE(duplicate.HasValue());
    EXPECT_EQ(duplicate.Error(), std::make_error_code(std::errc::invalid_argument));

    const auto discarded = evaluator.Truncate();
    ASSERT_FALSE(discarded.HasValue());
    EXPECT_EQ(discarded.Error(), std::make_error_code(std::errc::invalid_argument));

    ASSERT_TRUE(evaluator.Evaluate(LeafWave::Second, 40.0f).HasValue());
    const auto recovered = evaluator.Truncate();
    ASSERT_TRUE(recovered.HasValue());
    EXPECT_EQ(recovered.Value(), 0);
}

TEST(PortableLeafWaveEvaluatorTest, SecondTruncateReturnsInvalidArgumentAndAllowsRecovery)
{
    PortableLeafWaveEvaluator evaluator;
    ASSERT_TRUE(evaluator.Evaluate(LeafWave::First, 40.0f).HasValue());
    const auto first = evaluator.Truncate();
    ASSERT_TRUE(first.HasValue());
    EXPECT_EQ(first.Value(), 0);

    const auto duplicate = evaluator.Truncate();
    ASSERT_FALSE(duplicate.HasValue());
    EXPECT_EQ(duplicate.Error(), std::make_error_code(std::errc::invalid_argument));

    ASSERT_TRUE(evaluator.Evaluate(LeafWave::Second, 40.0f).HasValue());
    const auto recovered = evaluator.Truncate();
    ASSERT_TRUE(recovered.HasValue());
    EXPECT_EQ(recovered.Value(), 0);
}
} // namespace
