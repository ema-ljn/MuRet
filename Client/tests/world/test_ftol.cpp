#include "Math/Ftol.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

// Analytic vectors for the FTOL64→low32 contract with invalid masked. They guard the extraction of TerrainHeight
// (double without a store) and Particles (float promoted exactly), with no native oracle.
namespace
{
TEST(FtolTest, TruncatesBeforeProjectingSignedLow32)
{
    struct Case
    {
        double value;
        std::int32_t expected;
    };
    constexpr Case Cases[] = {{-1.75, -1},         {-0.75, 0},          {0.0, 0},           {254.75, 254},
                              {255.75, 255},       {4294967295.0, -1},  {4294967296.0, 0},  {4294967550.0, 254},
                              {4294967551.0, 255}, {-4294967297.0, -1}, {-4294967296.0, 0}, {-4294967042.0, 254}};
    for (const Case& test : Cases)
    {
        EXPECT_EQ(Client::Math::FtolLow(test.value), test.expected) << test.value;
    }
}

TEST(FtolTest, IncludesNegativeSigned64LimitAndExcludesPositiveLimit)
{
    constexpr double Limit = 0x1p63;
    EXPECT_EQ(Client::Math::FtolLow(-Limit), 0);
    EXPECT_EQ(Client::Math::FtolLow(std::nextafter(-Limit, 0.0)), 1024);
    EXPECT_EQ(Client::Math::FtolLow(std::nextafter(Limit, 0.0)), -1024);
    EXPECT_EQ(Client::Math::FtolLow(Limit), 0);
    EXPECT_EQ(Client::Math::FtolLow(std::nextafter(-Limit, -std::numeric_limits<double>::infinity())), 0);
    EXPECT_EQ(Client::Math::FtolLow(std::nextafter(Limit, std::numeric_limits<double>::infinity())), 0);
}

TEST(FtolTest, MaskedNonFiniteConversionProjectsToZero)
{
    EXPECT_EQ(Client::Math::FtolLow(std::numeric_limits<double>::quiet_NaN()), 0);
    EXPECT_EQ(Client::Math::FtolLow(std::numeric_limits<double>::infinity()), 0);
    EXPECT_EQ(Client::Math::FtolLow(-std::numeric_limits<double>::infinity()), 0);
    EXPECT_EQ(Client::Math::FtolLow(std::numeric_limits<float>::quiet_NaN()), 0);
    EXPECT_EQ(Client::Math::FtolLow(std::numeric_limits<float>::infinity()), 0);
    EXPECT_EQ(Client::Math::FtolLow(-std::numeric_limits<float>::infinity()), 0);
}

TEST(FtolTest, FtolLowIsOriginalFtolLowOrZero)
{
    struct Case
    {
        double value;
        std::int32_t low;
        std::optional<std::int32_t> original;
    };
    constexpr double Infinity = std::numeric_limits<double>::infinity();
    const Case cases[] = {{std::numeric_limits<double>::quiet_NaN(), 0, 0},
                          {Infinity, 0, std::nullopt},
                          {-Infinity, 0, std::nullopt},
                          {0x1p63, 0, std::nullopt},
                          {0x1p40 + 5, 5, 5},
                          {-1.5, -1, -1}};
    for (const Case& test : cases)
    {
        EXPECT_EQ(Client::Math::FtolLow(test.value), test.low) << test.value;
        EXPECT_EQ(Client::Math::OriginalFtolLow(test.value), test.original) << test.value;
    }
}
} // namespace
