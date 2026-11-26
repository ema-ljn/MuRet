#include "Math/RoundedMath.h"

#include "support/FloatBits.h"
#include "support/Fnv1a.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <type_traits>
#include <vector>

// The expected bits are MPFR's over the same inputs: Client/tests/vectors/rounded-math/README.md.
namespace
{
using Client::Math::RoundedAcos;
using Client::Math::RoundedCos;
using Client::Math::RoundedLog10;
using Client::Math::RoundedSin;
using Client::Math::RoundedTan;
using Client::Tests::DoubleFromBits;
using Client::Tests::ExponentRange;
using Client::Tests::FloatFromBits;
using Client::Tests::Fnv1a64;
using Client::Tests::WithoutSign;

constexpr std::size_t Count = 100000;
constexpr std::uint64_t Seed = 20261121;

// Each function runs over the sizes the game uses and then over every exponent of its type.
constexpr ExponentRange Angles{-8, 8};
constexpr ExponentRange Cosines{-8, -1};
constexpr ExponentRange EveryFloat{-126, 127};
constexpr ExponentRange EveryFloatBelowOne{-126, -1};
constexpr ExponentRange EveryDouble{-1022, 1023};
constexpr ExponentRange EveryDoubleBelowOne{-1022, -1};

template <typename Function> std::uint64_t FloatHash(Function function, ExponentRange common, ExponentRange every)
{
    std::mt19937_64 engine(Seed);
    std::vector<std::uint32_t> results;
    results.reserve(2 * Count);
    for (const ExponentRange range : {common, every})
    {
        for (std::size_t i = 0; i < Count; ++i)
        {
            results.push_back(std::bit_cast<std::uint32_t>(function(FloatFromBits(engine, range))));
        }
    }
    return Fnv1a64(std::as_bytes(std::span(results)));
}

template <typename Function> std::uint64_t DoubleHash(Function function, ExponentRange common, ExponentRange every)
{
    std::mt19937_64 engine(Seed);
    std::vector<std::uint64_t> results;
    results.reserve(2 * Count);
    for (const ExponentRange range : {common, every})
    {
        for (std::size_t i = 0; i < Count; ++i)
        {
            results.push_back(std::bit_cast<std::uint64_t>(function(DoubleFromBits(engine, range))));
        }
    }
    return Fnv1a64(std::as_bytes(std::span(results)));
}

TEST(RoundedMathTest, AFloatGivesAFloatAndADoubleADouble)
{
    static_assert(std::is_same_v<decltype(RoundedSin(1.0f)), float>);
    static_assert(std::is_same_v<decltype(RoundedCos(1.0f)), float>);
    static_assert(std::is_same_v<decltype(RoundedTan(1.0f)), float>);
    static_assert(std::is_same_v<decltype(RoundedAcos(1.0f)), float>);
    static_assert(std::is_same_v<decltype(RoundedSin(1.0)), double>);
    static_assert(std::is_same_v<decltype(RoundedCos(1.0)), double>);
    static_assert(std::is_same_v<decltype(RoundedTan(1.0)), double>);
    static_assert(std::is_same_v<decltype(RoundedAcos(1.0)), double>);
    static_assert(std::is_same_v<decltype(RoundedLog10(1.0)), double>);
}

TEST(RoundedMathTest, KnownValues)
{
    EXPECT_EQ(std::bit_cast<std::uint32_t>(RoundedSin(1.0f)), 0x3f576aa4u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(RoundedCos(1.0f)), 0x3f0a5140u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(RoundedTan(1.0f)), 0x3fc75923u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(RoundedAcos(0.5f)), 0x3f860a92u);
    EXPECT_EQ(std::bit_cast<std::uint64_t>(RoundedSin(1.0)), 0x3feaed548f090ceeu);
    EXPECT_EQ(std::bit_cast<std::uint64_t>(RoundedCos(1.0)), 0x3fe14a280fb5068cu);
    EXPECT_EQ(std::bit_cast<std::uint64_t>(RoundedTan(1.0)), 0x3ff8eb245cbee3a6u);
    EXPECT_EQ(std::bit_cast<std::uint64_t>(RoundedAcos(0.5)), 0x3ff0c152382d7366u);
    EXPECT_EQ(std::bit_cast<std::uint64_t>(RoundedLog10(2.0)), 0x3fd34413509f79ffu);
    EXPECT_EQ(RoundedSin(0.0), 0.0);
    EXPECT_EQ(RoundedCos(0.0), 1.0);
    EXPECT_EQ(RoundedAcos(1.0), 0.0);
    EXPECT_EQ(RoundedLog10(1000.0), 3.0);
}

TEST(RoundedMathTest, FloatsAreTheNearestToTheExactValue)
{
    EXPECT_EQ(FloatHash([](float x) { return RoundedSin(x); }, Angles, EveryFloat), 0xc1d51bb67356e490u);
    EXPECT_EQ(FloatHash([](float x) { return RoundedCos(x); }, Angles, EveryFloat), 0xb461a2e1923ed20fu);
    EXPECT_EQ(FloatHash([](float x) { return RoundedTan(x); }, Angles, EveryFloat), 0xff5b9223181d53c9u);
    EXPECT_EQ(FloatHash([](float x) { return RoundedAcos(x); }, Cosines, EveryFloatBelowOne), 0x8cbd2f0f4d5ae299u);
}

TEST(RoundedMathTest, DoublesAreTheNearestToTheExactValue)
{
    EXPECT_EQ(DoubleHash([](double x) { return RoundedSin(x); }, Angles, EveryDouble), 0x437149241e2d9310u);
    EXPECT_EQ(DoubleHash([](double x) { return RoundedCos(x); }, Angles, EveryDouble), 0xe0c4241d235ab65eu);
    EXPECT_EQ(DoubleHash([](double x) { return RoundedTan(x); }, Angles, EveryDouble), 0x32c3200dc8d22a17u);
    EXPECT_EQ(DoubleHash([](double x) { return RoundedAcos(x); }, Cosines, EveryDoubleBelowOne), 0x70b6b290d450f67du);
    EXPECT_EQ(DoubleHash([](double x) { return RoundedLog10(WithoutSign(x)); }, Angles, EveryDouble),
              0x7e8a47e44860acf3u);
}
} // namespace
