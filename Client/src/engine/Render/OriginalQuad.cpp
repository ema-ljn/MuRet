#include "Render/OriginalQuad.h"

#include "Core/PixelSize.h"
#include "Render/QuadCorner.h"
#include "Render/ScreenScale.h"

namespace Client::Render
{
QuadCorners FanCorners(const OriginalQuad& quad, Core::PixelSize screen)
{
    const float x = quad.scalePosition ? ScaleX(quad.x, screen) : quad.x;
    const float y = quad.scalePosition ? ScaleY(quad.y, screen) : quad.y;
    const float width = quad.scaleSize ? ScaleX(quad.width, screen) : quad.width;
    const float height = quad.scaleSize ? ScaleY(quad.height, screen) : quad.height;

    // 0x621bc9..0x621bf3: FILD H and FSUB y leave H − y on the x87 stack; FST stores it as float (the top edge), and
    // FLD ST0 and FSUB h subtract from the stack value, unrounded (the bottom edge).
    const double topInTheStack = static_cast<double>(screen.height) - static_cast<double>(y);
    const auto top = static_cast<float>(topInTheStack);
    const auto bottom = static_cast<float>(topInTheStack - static_cast<double>(height));
    const float right = x + width;
    const float rightU = quad.u + quad.du;
    const float bottomV = quad.v + quad.dv;

    return {QuadCorner{x, top, quad.u, quad.v}, QuadCorner{x, bottom, quad.u, bottomV},
            QuadCorner{right, bottom, rightU, bottomV}, QuadCorner{right, top, rightU, quad.v}};
}
} // namespace Client::Render
