#include "World/Ftol.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

// Vectores analíticos del contrato FTOL64→low32, con sus dos políticas: la de invalid enmascarada
// (FtolLow) y la verificada (FtolLowChecked). Protegen la extracción de TerrainHeight (double sin
// store), Particles (float promovido exactamente) y los callers que cortan fuera del dominio, sin
// oracle nativo.
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
        EXPECT_EQ(Client::World::FtolLow(test.value), test.expected) << test.value;
    }
}

TEST(FtolTest, IncludesNegativeSigned64LimitAndExcludesPositiveLimit)
{
    constexpr double Limit = 0x1p63;
    EXPECT_EQ(Client::World::FtolLow(-Limit), 0);
    EXPECT_EQ(Client::World::FtolLow(std::nextafter(-Limit, 0.0)), 1024);
    EXPECT_EQ(Client::World::FtolLow(std::nextafter(Limit, 0.0)), -1024);
    EXPECT_EQ(Client::World::FtolLow(Limit), 0);
    EXPECT_EQ(Client::World::FtolLow(std::nextafter(-Limit, -std::numeric_limits<double>::infinity())), 0);
    EXPECT_EQ(Client::World::FtolLow(std::nextafter(Limit, std::numeric_limits<double>::infinity())), 0);
}

TEST(FtolTest, MaskedNonFiniteConversionProjectsToZero)
{
    EXPECT_EQ(Client::World::FtolLow(std::numeric_limits<double>::quiet_NaN()), 0);
    EXPECT_EQ(Client::World::FtolLow(std::numeric_limits<double>::infinity()), 0);
    EXPECT_EQ(Client::World::FtolLow(-std::numeric_limits<double>::infinity()), 0);
    EXPECT_EQ(Client::World::FtolLow(std::numeric_limits<float>::quiet_NaN()), 0);
    EXPECT_EQ(Client::World::FtolLow(std::numeric_limits<float>::infinity()), 0);
    EXPECT_EQ(Client::World::FtolLow(-std::numeric_limits<float>::infinity()), 0);
}

TEST(FtolTest, CheckedPolicyAgreesWithTheMaskedOneInsideTheDomain)
{
    constexpr double Limit = 0x1p63;
    const double values[] = {-1.75,
                             -0.75,
                             0.0,
                             254.75,
                             4294967295.0,
                             4294967296.0,
                             4294967551.0,
                             -4294967297.0,
                             -4294967042.0,
                             -Limit,
                             std::nextafter(-Limit, 0.0),
                             std::nextafter(Limit, 0.0)};
    for (const double value : values)
    {
        EXPECT_EQ(Client::World::FtolLowChecked(value), std::optional<std::int32_t>{Client::World::FtolLow(value)})
            << value;
    }
}

TEST(FtolTest, CheckedPolicyRejectsNonFiniteAndOutOfDomainValues)
{
    constexpr double Limit = 0x1p63;
    constexpr double Infinity = std::numeric_limits<double>::infinity();
    const double values[] = {Limit,
                             std::nextafter(Limit, Infinity),
                             std::nextafter(-Limit, -Infinity),
                             Infinity,
                             -Infinity,
                             std::numeric_limits<double>::quiet_NaN(),
                             1.0e300,
                             -1.0e300};
    for (const double value : values)
    {
        EXPECT_FALSE(Client::World::FtolLowChecked(value).has_value()) << value;
    }
}
} // namespace
