#include "Render/OriginalSkewedQuad.h"

#include "Core/PixelSize.h"
#include "Render/OriginalQuad.h"
#include "Render/QuadCorner.h"

namespace Client::Render
{
namespace
{
// The quarter of dv the left v is shifted by: 0x6b89e0's 0.25.
constexpr double SkewOfTheLeftSide = 0.25;
} // namespace

QuadCorners SkewedFanCorners(const OriginalSkewedQuad& quad, Core::PixelSize screen)
{
    // The positions are 0x621b60's with both flags: 0x622000 scales all four and does the same subtraction
    // (0x622050..0x62208c).
    const OriginalQuad scaled{.slot = quad.slot,
                              .x = quad.x,
                              .y = quad.y,
                              .width = quad.width,
                              .height = quad.height,
                              .scaleSize = true,
                              .scalePosition = true};
    QuadCorners corners = FanCorners(scaled, screen);

    // 0x622097..0x6220c2: dv · 0.25 stays on the x87 stack; v plus that goes top left; v + dv is stored as float for
    // bottom right, and the quarter is subtracted from that unrounded stack value for bottom left.
    const double quarter = static_cast<double>(quad.dv) * SkewOfTheLeftSide;
    const double bottomInTheStack = static_cast<double>(quad.v) + static_cast<double>(quad.dv);
    const float rightU = quad.u + quad.du;
    corners[0].u = quad.u;
    corners[0].v = static_cast<float>(static_cast<double>(quad.v) + quarter);
    corners[1].u = quad.u;
    corners[1].v = static_cast<float>(bottomInTheStack - quarter);
    corners[2].u = rightU;
    corners[2].v = static_cast<float>(bottomInTheStack);
    corners[3].u = rightU;
    corners[3].v = quad.v;
    return corners;
}
} // namespace Client::Render
