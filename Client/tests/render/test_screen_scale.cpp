#include "Render/ScreenScale.h"

#include "Core/PixelSize.h"

#include <gtest/gtest.h>

// 0x620d50 and 0x620d80, from 640 × 480 to the screen, each axis on its own and multiplying by the inverse in float
// (06 § Primitivas 2D).
namespace
{
using Client::Core::PixelSize;
using Client::Render::ScaleX;
using Client::Render::ScaleY;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};
constexpr PixelSize Screen1920{1920, 1080};

// x goes by W / 640 and y by H / 480: at 1024 × 768 both factors are 1.6, and at 1920 × 1080, 3 and 2.25, so a wide
// screen stretches what is scaled (question 10 of the CL5 plan). The 25s come out one ULP up
// (MultipliesByTheInverseOfTheSize): EXPECT_FLOAT_EQ allows up to 4.
TEST(ScreenScaleTest, ScalesEachAxisApart)
{
    EXPECT_FLOAT_EQ(ScaleX(320, Screen1024), 512.0f);
    EXPECT_FLOAT_EQ(ScaleY(25, Screen1024), 40.0f);
    EXPECT_FLOAT_EQ(ScaleX(320, Screen1920), 960.0f);
    EXPECT_FLOAT_EQ(ScaleY(25, Screen1920), 56.25f);
    EXPECT_FLOAT_EQ(ScaleY(320, Screen1920), 720.0f);
}

// The inverses at 0x6ba070 and 0x6ba06c are not exact: 25 at 480 gives 25.000002, at 768 40.000004 and at 1080
// 56.250004. Dividing by 480 would give exactly 25, 40 and 56.25. 320 at 1024 does give 512.
TEST(ScreenScaleTest, MultipliesByTheInverseOfTheSize)
{
    EXPECT_EQ(ScaleY(25, Screen640), 25.000001907348633f);
    EXPECT_EQ(ScaleY(25, Screen1024), 40.000003814697266f);
    EXPECT_EQ(ScaleY(25, Screen1920), 56.250003814697266f);
    EXPECT_EQ(ScaleY(320, Screen640), 320.0000305175781f);
    EXPECT_EQ(ScaleX(320, Screen1024), 512.0f);
}

// At 640 × 480 there is no shortcut: the original scales all the same, and 0 stays at 0.
TEST(ScreenScaleTest, In640x480ItStillScales)
{
    EXPECT_NE(ScaleY(25, Screen640), 25.0f);
    EXPECT_EQ(ScaleX(0, Screen640), 0.0f);
    EXPECT_EQ(ScaleY(0, Screen1920), 0.0f);
}
} // namespace
