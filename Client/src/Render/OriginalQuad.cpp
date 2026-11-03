#include "Render/OriginalQuad.h"

#include "Render/ScreenScale.h"

namespace Client::Render
{
QuadCorners FanCorners(const OriginalQuad& quad, App::PixelSize screen)
{
    const float x = quad.scalePosition ? ScaleX(quad.x, screen) : quad.x;
    const float y = quad.scalePosition ? ScaleY(quad.y, screen) : quad.y;
    const float width = quad.scaleSize ? ScaleX(quad.width, screen) : quad.width;
    const float height = quad.scaleSize ? ScaleY(quad.height, screen) : quad.height;

    // 0x621bc9..0x621bf3: FILD H y FSUB y dejan H − y en la pila; FST la guarda como float (el
    // borde de arriba), y FLD ST0 y FSUB h restan del valor de la pila, sin redondear (el de abajo).
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
