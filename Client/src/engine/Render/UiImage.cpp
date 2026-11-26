#include "Render/UiImage.h"

#include "Core/PixelSize.h"
#include "Render/QuadCorner.h"

#include <bit>
#include <cstdint>

namespace Client::Render
{
namespace
{
// The UV half texel, 0x6b882c's 0.5.
constexpr float HalfTexel = 0.5f;

// The inverses of 0x65eeb0 for its 800 × 600 design: 1/800 and 1/600 as float (0x6ba884 and 0x6ba880). DesignScale
// builds them with 1.0f / size, which gives these same bits.
static_assert(std::bit_cast<std::uint32_t>(1.0f / 800) == 0x3aa3d70a, "la de 0x6ba884");
static_assert(std::bit_cast<std::uint32_t>(1.0f / 600) == 0x3ada740e, "la de 0x6ba880");

// A rectangle's four edges, y up: the image's position in its space, or its UVs (with row 0's v in `top`).
struct Edges
{
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

// Where the image lands in its space after 0x647050 and 0x6472e0(x, y, 3), with the anchor and the offset at 0.
Edges PlacedEdges(const UiImage& image, Core::PixelSize screen)
{
    // 0x647080..0x6470bb: the screen height in the image's space (H / fy, as float) is the top edge before placing it,
    // and the bottom, that minus the height, as float.
    const auto spaceHeight = static_cast<float>(static_cast<double>(screen.height) / static_cast<double>(image.scaleY));
    const auto firstBottom = static_cast<float>(static_cast<double>(spaceHeight) - image.height);

    // 0x6472e0: the width and height come from the corners 0x647050 left, and each edge is stored as float; the right
    // and bottom edges come from the other one unrounded (FADDP and FSUBRP with the stack value).
    const double spanX = image.width;
    const double spanY = static_cast<double>(spaceHeight) - static_cast<double>(firstBottom);
    const double topInTheStack = static_cast<double>(spaceHeight) - image.y;
    return Edges{.left = static_cast<float>(image.x),
                 .top = static_cast<float>(topInTheStack),
                 .right = static_cast<float>(image.x + spanX),
                 .bottom = static_cast<float>(topInTheStack - spanY)};
}

// 0x6470c9..0x647151, with a slot: UVs half a texel from each edge, except the side 0x647370 changed, which reaches the
// size (fild of the size, fdiv by the texture's and fst to float). Without a slot they stay 0 (0x6471e4).
Edges HalfTexelUv(const UiImage& image)
{
    if (!image.slot.has_value())
    {
        return Edges{};
    }
    const float rightTexel = image.resizedWidth ? 0.0f : HalfTexel;
    const float bottomTexel = image.resizedHeight ? 0.0f : HalfTexel;
    const int height = image.uvHeight.value_or(image.height);
    return Edges{.left = HalfTexel / image.textureWidth,
                 .top = HalfTexel / image.textureHeight,
                 .right = (static_cast<float>(image.width) - rightTexel) / image.textureWidth,
                 .bottom = (static_cast<float>(height) - bottomTexel) / image.textureHeight};
}

// 0x647560(k) with 0x647050's frame (0x647185..0x6471c5): the frame starts at ((frameLeft + 0.5) / tw,
// (frameTop + 0.5) / th), each integer converted to float and the sum and division on the x87, stored as float. The
// right u is the unrounded u width (FSUB on the stack) plus the new left; the v height is stored as float first (FSTP
// to [EBP + 8]) and the bottom v is that plus the new top. Shifted from 0x647050's UVs; the original shifts them from
// the previous frame, which can differ by an ulp and is not reproduced.
Edges FrameUv(const Edges& base, const UiImage& image, int frameTop)
{
    const auto left = static_cast<float>((static_cast<double>(static_cast<float>(image.frameLeft)) + HalfTexel) /
                                         static_cast<double>(image.textureWidth));
    const auto top = static_cast<float>((static_cast<double>(static_cast<float>(frameTop)) + HalfTexel) /
                                        static_cast<double>(image.textureHeight));
    const double spanU = static_cast<double>(base.right) - static_cast<double>(base.left);
    const auto spanV = static_cast<float>(static_cast<double>(base.bottom) - static_cast<double>(base.top));
    return Edges{.left = left,
                 .top = top,
                 .right = static_cast<float>(spanU + static_cast<double>(left)),
                 .bottom = static_cast<float>(static_cast<double>(spanV) + static_cast<double>(top))};
}

// A corner as 0x647650 sends it: glVertex2f(x · fx, y · fy), each product on the x87 and converted to float when
// pushed.
float ToScreen(float value, float scale)
{
    return static_cast<float>(static_cast<double>(value) * static_cast<double>(scale));
}
} // namespace

QuadCorners UiImageCorners(const UiImage& image, Core::PixelSize screen)
{
    const Edges placed = PlacedEdges(image, screen);
    Edges uv = HalfTexelUv(image);
    if (image.slot.has_value() && image.frameTop.has_value())
    {
        uv = FrameUv(uv, image, *image.frameTop);
    }
    const float left = ToScreen(placed.left, image.scaleX);
    const float right = ToScreen(placed.right, image.scaleX);
    const float top = ToScreen(placed.top, image.scaleY);
    const float bottom = ToScreen(placed.bottom, image.scaleY);
    return {QuadCorner{left, top, uv.left, uv.top}, QuadCorner{left, bottom, uv.left, uv.bottom},
            QuadCorner{right, bottom, uv.right, uv.bottom}, QuadCorner{right, top, uv.right, uv.top}};
}

float DesignScale(int screenSize, int designSize)
{
    // FILD of the size and FMUL by the float inverse; 0x65eeb0 stores it as float.
    const float inverse = 1.0f / static_cast<float>(designSize);
    return static_cast<float>(static_cast<double>(screenSize) * static_cast<double>(inverse));
}
} // namespace Client::Render
