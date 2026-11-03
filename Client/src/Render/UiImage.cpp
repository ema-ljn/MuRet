#include "Render/UiImage.h"

#include <bit>
#include <cstdint>

namespace Client::Render
{
namespace
{
// El medio téxel de las UV, el 0,5 de 0x6b882c.
constexpr float HalfTexel = 0.5f;

// Las inversas de 0x65eeb0 para su diseño de 800 × 600: 1/800 y 1/600 en float (0x6ba884 y
// 0x6ba880). DesignScale las arma con 1.0f / tamaño, que da estos mismos bits.
static_assert(std::bit_cast<std::uint32_t>(1.0f / 800) == 0x3aa3d70a, "la de 0x6ba884");
static_assert(std::bit_cast<std::uint32_t>(1.0f / 600) == 0x3ada740e, "la de 0x6ba880");

// Los cuatro bordes de un rectángulo, con la y para arriba: la posición de la imagen en su espacio,
// o sus UV (con la v de la fila 0 en `top`).
struct Edges
{
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

// Dónde queda la imagen en su espacio después de 0x647050 y de 0x6472e0(x, y, 3), con el ancla y
// el corrimiento en 0.
Edges PlacedEdges(const UiImage& image, App::PixelSize screen)
{
    // 0x647080..0x6470bb: el alto de la pantalla en el espacio de la imagen (H / fy, en float) es
    // el borde de arriba antes de ubicarla, y el de abajo, ese menos el alto, en float.
    const auto spaceHeight = static_cast<float>(static_cast<double>(screen.height) / static_cast<double>(image.scaleY));
    const auto firstBottom = static_cast<float>(static_cast<double>(spaceHeight) - image.height);

    // 0x6472e0: el ancho y el alto salen de las esquinas que dejó 0x647050, y cada borde se
    // guarda en float; el de la derecha y el de abajo salen del otro sin redondear (FADDP y FSUBRP
    // con el valor de la pila).
    const double spanX = image.width;
    const double spanY = static_cast<double>(spaceHeight) - static_cast<double>(firstBottom);
    const double topInTheStack = static_cast<double>(spaceHeight) - image.y;
    return Edges{.left = static_cast<float>(image.x),
                 .top = static_cast<float>(topInTheStack),
                 .right = static_cast<float>(image.x + spanX),
                 .bottom = static_cast<float>(topInTheStack - spanY)};
}

// 0x6470c9..0x647151, con un slot: las UV a medio téxel de cada borde. Sin slot quedan en 0
// (0x6471e4).
Edges HalfTexelUv(const UiImage& image)
{
    if (!image.slot.has_value())
    {
        return Edges{};
    }
    return Edges{.left = HalfTexel / image.textureWidth,
                 .top = HalfTexel / image.textureHeight,
                 .right = (static_cast<float>(image.width) - HalfTexel) / image.textureWidth,
                 .bottom = (static_cast<float>(image.height) - HalfTexel) / image.textureHeight};
}

// Una esquina como la manda 0x647650: glVertex2f(x · fx, y · fy), cada producto en la x87 y
// pasado a float al apilarlo.
float ToScreen(float value, float scale)
{
    return static_cast<float>(static_cast<double>(value) * static_cast<double>(scale));
}
} // namespace

QuadCorners UiImageCorners(const UiImage& image, App::PixelSize screen)
{
    const Edges placed = PlacedEdges(image, screen);
    const Edges uv = HalfTexelUv(image);
    const float left = ToScreen(placed.left, image.scaleX);
    const float right = ToScreen(placed.right, image.scaleX);
    const float top = ToScreen(placed.top, image.scaleY);
    const float bottom = ToScreen(placed.bottom, image.scaleY);
    return {QuadCorner{left, top, uv.left, uv.top}, QuadCorner{left, bottom, uv.left, uv.bottom},
            QuadCorner{right, bottom, uv.right, uv.bottom}, QuadCorner{right, top, uv.right, uv.top}};
}

float DesignScale(int screenSize, int designSize)
{
    // FILD del tamaño y FMUL por la inversa en float; 0x65eeb0 lo guarda en float.
    const float inverse = 1.0f / static_cast<float>(designSize);
    return static_cast<float>(static_cast<double>(screenSize) * static_cast<double>(inverse));
}
} // namespace Client::Render
