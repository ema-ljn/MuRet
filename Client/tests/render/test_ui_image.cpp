#include "Render/UiImage.h"

#include "Core/PixelSize.h"
#include "Render/QuadCorner.h"
#include "Render/TextureSlot.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

// The new UI's image (0x647050, 0x6472e0 and 0x647650), with y from the top in its own space, each corner by fx and fy,
// and the UVs at half a texel (06 § La carga).
namespace
{
using Client::Core::PixelSize;
using Client::Render::DesignScale;
using Client::Render::QuadCorners;
using Client::Render::TextureSlot;
using Client::Render::UiImage;
using Client::Render::UiImageCorners;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};

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

// A 329 × 245 image in pixels, the size of the account window's background (0x13, in a 512 × 256 buffer), placed like
// that window at 640 × 480: ((W − w) / 2, (H − h) · 2 / 3), which is (155, 156) (06 § Las ventanas de la UI nueva).
constexpr UiImage AccountSizedImage{.slot = TextureSlot{0x13},
                                    .x = 155,
                                    .y = 156,
                                    .width = 329,
                                    .height = 245,
                                    .textureWidth = 512,
                                    .textureHeight = 256};

// With fx = fy = 1 the image is in pixels, and y comes from H: the top edge is 480 − 156 = 324 and the bottom one,
// 324 − 245 = 79. The UVs go half a texel from each edge: 0.5 / 512 and (329 − 0.5) / 512 in u, 0.5 / 256 and
// (245 − 0.5) / 256 in v, with row 0 at the top.
TEST(UiImageTest, InPixelsTheYGoesDownFromTheHeight)
{
    ExpectCorners(UiImageCorners(AccountSizedImage, Screen640), Expected{{{155, 324, 0.0009765625f, 0.001953125f},
                                                                          {155, 79, 0.0009765625f, 0.955078125f},
                                                                          {484, 79, 0.6416015625f, 0.955078125f},
                                                                          {484, 324, 0.6416015625f, 0.001953125f}}});
}

// On another screen, the same image in pixels does not change size: only y, which comes from H.
TEST(UiImageTest, InPixelsTheSizeDoesNotScale)
{
    const QuadCorners corners = UiImageCorners(AccountSizedImage, Screen1024);

    EXPECT_EQ(corners[0].y, 612.0f);
    EXPECT_EQ(corners[1].y, 367.0f);
    EXPECT_EQ(corners[2].x - corners[1].x, 329.0f);
}

// While loading, designed at 800 × 600, fx is 640 · (1/800 in float) = 0.79999995 and fy, exactly 0.8 plus a bit: the
// top edge is H / fy = 600 minus y, times fy. The background 0x71f, 288 × 478 at (512, 60), goes from x = 409.59998 to
// 639.99994, a bit before the screen edge.
TEST(UiImageTest, EachCornerGoesByItsScale)
{
    const UiImage right{.slot = TextureSlot{0x71f},
                        .x = 512,
                        .y = 60,
                        .width = 288,
                        .height = 478,
                        .textureWidth = 512,
                        .textureHeight = 512,
                        .scaleX = DesignScale(640, 800),
                        .scaleY = DesignScale(480, 600)};

    ExpectCorners(UiImageCorners(right, Screen640),
                  Expected{{{409.5999755859375f, 432, 0.0009765625f, 0.0009765625f},
                            {409.5999755859375f, 49.60000228881836f, 0.0009765625f, 0.9326171875f},
                            {639.9999389648438f, 49.60000228881836f, 0.5615234375f, 0.9326171875f},
                            {639.9999389648438f, 432, 0.5615234375f, 0.0009765625f}}});
}

// Without a slot (the original's -1) 0x647050 leaves the UVs at 0: the image is only its color.
TEST(UiImageTest, WithoutASlotTheUvIsZero)
{
    UiImage colorOnly = AccountSizedImage;
    colorOnly.slot.reset();

    for (const auto& corner : UiImageCorners(colorOnly, Screen640))
    {
        EXPECT_EQ(corner.u, 0.0f);
        EXPECT_EQ(corner.v, 0.0f);
    }
}

// The loading bar (0x6402d0 and 0x640580): 0x647370 changes its width and height after building it, and with them the
// right u (width / tw, 0x6473dd) and the bottom v (height / th, 0x647457), without the half texel; the left and top
// ones stay at half a texel. With a bar 328 wide (half of 656) and 15 high in a 4 × 16 buffer, u reaches 82: with
// GL_REPEAT, the texture repeats along it.
TEST(UiImageTest, AResizedImageEndsItsUvOnItsSize)
{
    const UiImage bar{.slot = TextureSlot{0x723},
                      .x = 72,
                      .y = 482,
                      .width = 328,
                      .height = 15,
                      .textureWidth = 4,
                      .textureHeight = 16,
                      .resizedWidth = true,
                      .resizedHeight = true};

    ExpectCorners(UiImageCorners(bar, Screen640), Expected{{{72, -2, 0.125f, 0.03125f},
                                                            {72, -17, 0.125f, 0.9375f},
                                                            {400, -17, 82.0f, 0.9375f},
                                                            {400, -2, 82.0f, 0.03125f}}});
}

// Each axis on its own: changing only the width leaves the bottom v at half a texel.
TEST(UiImageTest, OnlyTheResizedSideLosesTheHalfTexel)
{
    UiImage bar{.slot = TextureSlot{0x723},
                .x = 0,
                .y = 0,
                .width = 8,
                .height = 15,
                .textureWidth = 4,
                .textureHeight = 16,
                .resizedWidth = true};

    QuadCorners corners = UiImageCorners(bar, Screen640);
    EXPECT_EQ(corners[2].u, 2.0f);
    EXPECT_EQ(corners[2].v, 14.5f / 16.0f);

    bar.resizedWidth = false;
    bar.resizedHeight = true;
    corners = UiImageCorners(bar, Screen640);
    EXPECT_EQ(corners[2].u, 7.5f / 4.0f);
    EXPECT_EQ(corners[2].v, 15.0f / 16.0f);
}

// A button's frames (CL5.5): 0x63c330 stacks them downwards, each at k · h texels, and 0x647560(k) shifts the UVs: the
// top one to (k · h + 0.5) / th, and the bottom one, the top one plus what the image measured in v. A 193 × 26 server
// button (server_b2_all, in a 256 × 128 buffer), at frame 1: v goes from 26.5 / 128 to 51.5 / 128, and u does not
// change.
TEST(UiImageTest, AFrameMovesTheUvDown)
{
    const UiImage button{.slot = TextureSlot{0xd},
                         .x = 0,
                         .y = 0,
                         .width = 193,
                         .height = 26,
                         .textureWidth = 256,
                         .textureHeight = 128,
                         .frameTop = 26};

    ExpectCorners(UiImageCorners(button, Screen640), Expected{{{0, 480, 0.001953125f, 0.20703125f},
                                                               {0, 454, 0.001953125f, 0.40234375f},
                                                               {193, 454, 0.751953125f, 0.40234375f},
                                                               {193, 480, 0.751953125f, 0.20703125f}}});
}

// An image that starts elsewhere in its texture (0x647260, the list of 0x647050 with a single frame): the second piece
// of the list frame, 68 × 95 from (68, 0) of server_deco_all (159 × 95, in a 256 × 128 buffer). u goes from
// 68.5 / 256 to 135.5 / 256, and v from 0.5 / 128 to 94.5 / 128.
TEST(UiImageTest, AFrameCanStartToTheRight)
{
    const UiImage piece{.slot = TextureSlot{0xf},
                        .x = 0,
                        .y = 0,
                        .width = 68,
                        .height = 95,
                        .textureWidth = 256,
                        .textureHeight = 128,
                        .frameTop = 0,
                        .frameLeft = 68};

    ExpectCorners(UiImageCorners(piece, Screen640), Expected{{{0, 480, 0.267578125f, 0.00390625f},
                                                              {0, 385, 0.267578125f, 0.73828125f},
                                                              {68, 385, 0.529296875f, 0.73828125f},
                                                              {68, 480, 0.529296875f, 0.00390625f}}});
}

// The right side of the description box (0x66a1c0): 3 × 4 from (3, 0) of server_ex02 (an 8 × 4 buffer), with frames,
// and 0x66a4e0 sets its height to 40 without moving the UVs (+0x98 at 0). The corners go from 480 to 440 and v from
// 0.5 / 4 to 3.5 / 4: the texture stretches.
TEST(UiImageTest, AHeightChangeCanKeepTheUv)
{
    const UiImage side{.slot = TextureSlot{0x19},
                       .x = 0,
                       .y = 0,
                       .width = 3,
                       .height = 40,
                       .textureWidth = 8,
                       .textureHeight = 4,
                       .frameTop = 0,
                       .frameLeft = 3,
                       .uvHeight = 4};

    ExpectCorners(UiImageCorners(side, Screen640), Expected{{{0, 480, 0.4375f, 0.125f},
                                                             {0, 440, 0.4375f, 0.875f},
                                                             {3, 440, 0.6875f, 0.875f},
                                                             {3, 480, 0.6875f, 0.125f}}});
}

// Frame 0 is the image without frames: 0x647050 calls 0x647560(0) when building it.
TEST(UiImageTest, FrameZeroIsTheImage)
{
    UiImage framed = AccountSizedImage;
    framed.frameTop = 0;

    const QuadCorners plain = UiImageCorners(AccountSizedImage, Screen640);
    const QuadCorners first = UiImageCorners(framed, Screen640);
    for (std::size_t i = 0; i < plain.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(first[i].u, plain[i].u);
        EXPECT_EQ(first[i].v, plain[i].v);
    }
}

// 0x65eeb0: W · (1/800 in float) and H · (1/600 in float), stored as float. Neither inverse is exact: at 640 × 480,
// 0.79999995 and 0.80000001; at 800 × 600, exactly 1.
TEST(DesignScaleTest, MultipliesByTheInverseOfTheDesign)
{
    EXPECT_EQ(DesignScale(640, 800), 0.7999999523162842f);
    EXPECT_EQ(DesignScale(480, 600), 0.800000011920929f);
    EXPECT_EQ(DesignScale(1024, 800), 1.2799999713897705f);
    EXPECT_EQ(DesignScale(800, 800), 1.0f);
    EXPECT_EQ(DesignScale(600, 600), 1.0f);
}
} // namespace
