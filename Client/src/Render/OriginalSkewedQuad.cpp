#include "Render/OriginalSkewedQuad.h"

#include "App/WindowState.h"
#include "Render/OriginalQuad.h"
#include "Render/QuadCorner.h"

namespace Client::Render
{
namespace
{
// El cuarto de dv que se corre la v de la izquierda: el 0,25 de 0x6b89e0.
constexpr double SkewOfTheLeftSide = 0.25;
} // namespace

QuadCorners SkewedFanCorners(const OriginalSkewedQuad& quad, App::PixelSize screen)
{
    // Las posiciones son las de 0x621b60 con las dos banderas: 0x622000 escala los cuatro y hace la
    // misma resta (0x622050..0x62208c).
    const OriginalQuad scaled{.slot = quad.slot,
                              .x = quad.x,
                              .y = quad.y,
                              .width = quad.width,
                              .height = quad.height,
                              .scaleSize = true,
                              .scalePosition = true};
    QuadCorners corners = FanCorners(scaled, screen);

    // 0x622097..0x6220c2: dv · 0,25 queda en la pila; v + eso va arriba a la izquierda; v + dv se
    // guarda en float para abajo a la derecha, y a ese valor de la pila, sin redondear, se le resta
    // el cuarto para abajo a la izquierda.
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
