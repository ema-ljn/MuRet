#include "Render/OriginalQuad.h"

#include "Core/PixelSize.h"
#include "Render/OriginalSkewedQuad.h"
#include "Render/QuadCorner.h"
#include "Render/ScreenScale.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

// The four corners of 0x621b60 and 0x622000, with the x87 math the decompilation does not show
// (06 § Primitivas 2D).
namespace
{
using Client::Core::PixelSize;
using Client::Render::FanCorners;
using Client::Render::OriginalQuad;
using Client::Render::OriginalSkewedQuad;
using Client::Render::QuadCorners;
using Client::Render::ScaleY;
using Client::Render::SkewedFanCorners;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};
constexpr PixelSize Screen1280{1280, 1024};

// The login's 0x15 background: 0x621b60(0x15, 0, 25, 320, 320, 0, 0, 1, 1, 1, 1, 0) in 0x6350b0.
constexpr OriginalQuad LoginLeftBackground{
    .slot = {0x15}, .x = 0, .y = 25, .width = 320, .height = 320, .scaleSize = true, .scalePosition = true};

// The four (x, y, u, v), in fan order.
using Expected = std::array<std::array<float, 4>, 4>;

void ExpectCorners(const QuadCorners& corners, const Expected& expected)
{
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(corners[i].x, expected[i][0]);
        EXPECT_EQ(corners[i].y, expected[i][1]);
        EXPECT_EQ(corners[i].u, expected[i][2]);
        EXPECT_EQ(corners[i].v, expected[i][3]);
    }
}

// At 1280 × 1024 the top edge is 1024 − 53.333336 = 970.66669, and the bottom one, that minus 682.66669 unrounded:
// 287.99997. Subtracting from the top float would give exactly 288.
TEST(OriginalQuadTest, TheBottomComesFromTheUnroundedTop)
{
    const QuadCorners corners = FanCorners(LoginLeftBackground, Screen1280);

    EXPECT_EQ(corners[0].y, 970.6666870117188f);
    EXPECT_EQ(corners[1].y, 287.99996948242188f);
    EXPECT_EQ(corners[0].y - ScaleY(LoginLeftBackground.height, Screen1280), 288.0f);
}

// v grows downwards: the top corner has v and the bottom one v + dv, so buffer row 0 ends up at the top. The UVs are
// not scaled, and without flags everything is in pixels.
TEST(OriginalQuadTest, TheUvGoesDownTheTexture)
{
    const OriginalQuad quad{
        .x = 10, .y = 20, .width = 30, .height = 40, .u = 0.25f, .v = 0.5f, .du = 0.5f, .dv = 0.25f};

    ExpectCorners(
        FanCorners(quad, Screen1024),
        Expected{{{10, 748, 0.25f, 0.5f}, {10, 708, 0.25f, 0.75f}, {40, 708, 0.75f, 0.75f}, {40, 748, 0.75f, 0.5f}}});
}

// The sand is always scaled: (0, 25, 640, 430) at 1024 × 768 is the whole screen width and from 728 to 39.999935 in
// height (the scaled 430 gives 688.00006). The left v shifts by a quarter of dv: (u, v + 0.25 dv) at the top and
// (u, v + 0.75 dv) at the bottom.
TEST(OriginalSkewedQuadTest, IsAlwaysScaledAndSkewsTheLeftSide)
{
    const OriginalSkewedQuad quad{
        .x = 0, .y = 25, .width = 640, .height = 430, .u = 0.25f, .v = 0.5f, .du = 0.5f, .dv = 0.25f};

    ExpectCorners(SkewedFanCorners(quad, Screen1024), Expected{{{0, 728, 0.25f, 0.5625f},
                                                                {0, 39.999935150146484f, 0.25f, 0.6875f},
                                                                {1024, 39.999935150146484f, 0.75f, 0.75f},
                                                                {1024, 728, 0.75f, 0.5f}}});
}

// 0x6220b7..0x6220c2: the x87 adds v + dv, stores it as a float for the lower-right corner and subtracts 0.25 dv from
// the stack value, unrounded. With v = 0.01 and dv = 0.2 the lower-left gives 0.15999999; subtracting from the stored
// float, or adding v + 0.75 dv, would give 0.16000001.
TEST(OriginalSkewedQuadTest, TheLowerLeftVComesFromTheUnroundedSum)
{
    const OriginalSkewedQuad quad{.width = 640, .height = 480, .v = 0.01f, .dv = 0.2f};

    const QuadCorners corners = SkewedFanCorners(quad, Screen640);

    EXPECT_EQ(corners[1].v, 0.1599999964237213f);
    EXPECT_EQ(corners[2].v, 0.21000000834465027f);
}
} // namespace
