#include "App/Resolution.h"

#include "support/AppPrinters.h"

#include <gtest/gtest.h>

#include <vector>

// Client::App::IsValidResolution y DescribeValidResolutions: las cinco del original o un modo del
// monitor (decidido el 2026-11-03).
namespace
{
using Client::App::DescribeValidResolutions;
using Client::App::IsValidResolution;
using Client::App::OriginalResolutions;
using Client::App::Resolution;

TEST(ResolutionTest, TheFiveOfTheOriginalAreTheRegistryValues)
{
    // El valor Resolution del registro, de 0 a 4 (02 § La resolución).
    const std::vector<Resolution> expected{{640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1600, 1200}};
    EXPECT_EQ(std::vector<Resolution>(OriginalResolutions.begin(), OriginalResolutions.end()), expected);
}

TEST(ResolutionTest, TheFiveOfTheOriginalAreValidWithoutAMonitor)
{
    for (const Resolution resolution : OriginalResolutions)
    {
        EXPECT_TRUE(IsValidResolution(resolution, {})) << ::testing::PrintToString(resolution);
    }
}

TEST(ResolutionTest, AModeOfTheMonitorIsValid)
{
    const std::vector<Resolution> monitor{{1920, 1080}, {1366, 768}};
    EXPECT_TRUE(IsValidResolution({1920, 1080}, monitor));
    EXPECT_TRUE(IsValidResolution({1366, 768}, monitor));
    EXPECT_TRUE(IsValidResolution({800, 600}, monitor));
}

TEST(ResolutionTest, ASizeThatIsNeitherIsNotValid)
{
    const std::vector<Resolution> monitor{{1920, 1080}};
    EXPECT_FALSE(IsValidResolution({7, 3}, monitor));
    EXPECT_FALSE(IsValidResolution({1000, 700}, monitor));
    // El ancho de una y el alto de otra no hacen una resolución.
    EXPECT_FALSE(IsValidResolution({1920, 480}, monitor));
    EXPECT_FALSE(IsValidResolution({640, 1080}, monitor));
    EXPECT_FALSE(IsValidResolution({1920, 1080}, {}));
}

TEST(ResolutionTest, TheListGoesFromTheSmallestWithoutRepeats)
{
    // SDL da un modo por frecuencia y por densidad, del más grande al más chico.
    const std::vector<Resolution> monitor{{1920, 1080}, {1920, 1080}, {1280, 1024}, {1280, 720}, {720, 400}};
    EXPECT_EQ(DescribeValidResolutions(monitor),
              "640 × 480, 720 × 400, 800 × 600, 1024 × 768, 1280 × 720, 1280 × 1024, 1600 × 1200, 1920 × 1080");
}

TEST(ResolutionTest, WithoutAMonitorTheListIsTheFiveOfTheOriginal)
{
    EXPECT_EQ(DescribeValidResolutions({}), "640 × 480, 800 × 600, 1024 × 768, 1280 × 1024, 1600 × 1200");
}
} // namespace
