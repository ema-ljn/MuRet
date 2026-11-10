#include "Render/Models/OriginalRetainedMath.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <limits>

namespace
{
// Simulado matemático independiente: mpmath100dps → R64(sin/cos) → producto exacto → R53.
// Cada caso difiere de multiplicar dos resultados redondeados prematuramente a double.
TEST(FaunaRetainedMathTest, MultipliesBothRetainedResultsBeforeTheFirstBinary53Round)
{
    struct Case
    {
        double sine, cosine;
        std::uint64_t expected;
    };
    constexpr std::array<Case, 4> cases{{{0x1.028e8b3fe8000p+4, 0x1.842fd35549000p+6, 0x3fda51acd683f0faULL},
                                         {0x1.1b54618c2c000p+4, 0x1.b5b6e447ea000p+6, 0x3fe9255dbd237e28ULL},
                                         {0x1.341a37d870000p+4, 0x1.e73df53a8b000p+6, 0xbfd32df1ac5387c8ULL},
                                         {0x1.65a5e470f8000p+4, 0x1.25260b8fe6800p+7, 0x3fc555db8c919883ULL}}};
    for (const auto& sample : cases)
    {
        const auto actual = Client::Render::Models::Detail::SineCosineProductPC53(sample.sine, sample.cosine);
        ASSERT_TRUE(actual.has_value());
        EXPECT_EQ(std::bit_cast<std::uint64_t>(*actual), sample.expected);
    }
}
TEST(FaunaRetainedMathTest, KeepsSignedZeroAndRejectsAnUnsupportedArgumentProfile)
{
    const auto zero = Client::Render::Models::Detail::SineCosineProductPC53(-0.0, 0.0);
    ASSERT_TRUE(zero.has_value());
    EXPECT_EQ(std::bit_cast<std::uint64_t>(*zero), 0x8000000000000000ULL);
    EXPECT_FALSE(Client::Render::Models::Detail::SineCosineProductPC53(0x1p63, 0).has_value());
    EXPECT_FALSE(
        Client::Render::Models::Detail::SineCosineProductPC53(1, std::numeric_limits<double>::infinity()).has_value());
}
} // namespace

TEST(FaunaRetainedMathTest, ProducesTheThreeCounterDirectionsWithTheirNativeSpills)
{
    const auto direction = Client::Render::Models::Detail::FaunaDirectionPC53(0);
    ASSERT_TRUE(direction);
    EXPECT_EQ(std::bit_cast<std::uint32_t>((*direction)[0]), 0xbf42f393U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>((*direction)[1]), 0xbeb66851U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>((*direction)[2]), 0x3f0a9b4eU);
}
