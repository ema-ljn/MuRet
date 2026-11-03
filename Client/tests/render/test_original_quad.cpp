#include "Render/OriginalQuad.h"

#include "App/WindowState.h"
#include "Render/OriginalSkewedQuad.h"
#include "Render/QuadCorner.h"
#include "Render/ScreenScale.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

// Client::Render::FanCorners y SkewedFanCorners: las cuatro esquinas de 0x621b60 y de 0x622000, con
// las cuentas de la x87 que el decompilado no muestra (06 § Primitivas 2D). Lo que hace el
// SpriteBatch con ellas, en test_sprite_batch.cpp.
namespace
{
using Client::App::PixelSize;
using Client::Render::FanCorners;
using Client::Render::OriginalQuad;
using Client::Render::OriginalSkewedQuad;
using Client::Render::QuadCorners;
using Client::Render::ScaleY;
using Client::Render::SkewedFanCorners;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};
constexpr PixelSize Screen1280{1280, 1024};

// El fondo 0x15 del login: 0x621b60(0x15, 0, 25, 320, 320, 0, 0, 1, 1, 1, 1, 0) en 0x6350b0.
constexpr OriginalQuad LoginLeftBackground{
    .slot = {0x15}, .x = 0, .y = 25, .width = 320, .height = 320, .scaleSize = true, .scalePosition = true};

// Las cuatro de (x, y, u, v), en el orden del fan.
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

// En 1280 × 1024 el borde de arriba es 1024 − 53,333336 = 970,66669, y el de abajo, ese menos
// 682,66669 sin redondear: 287,99997. Restando del float de arriba daría 288 justos.
TEST(OriginalQuadTest, TheBottomComesFromTheUnroundedTop)
{
    const QuadCorners corners = FanCorners(LoginLeftBackground, Screen1280);

    EXPECT_EQ(corners[0].y, 970.6666870117188f);
    EXPECT_EQ(corners[1].y, 287.99996948242188f);
    EXPECT_EQ(corners[0].y - ScaleY(LoginLeftBackground.height, Screen1280), 288.0f);
}

// La v crece hacia abajo: la esquina de arriba lleva v y la de abajo v + dv, así que la fila 0 del
// buffer queda arriba. Las UV no se escalan, y sin banderas todo va en píxeles.
TEST(OriginalQuadTest, TheUvGoesDownTheTexture)
{
    const OriginalQuad quad{
        .x = 10, .y = 20, .width = 30, .height = 40, .u = 0.25f, .v = 0.5f, .du = 0.5f, .dv = 0.25f};

    ExpectCorners(
        FanCorners(quad, Screen1024),
        Expected{{{10, 748, 0.25f, 0.5f}, {10, 708, 0.25f, 0.75f}, {40, 708, 0.75f, 0.75f}, {40, 748, 0.75f, 0.5f}}});
}

// La arena va siempre escalada: (0, 25, 640, 430) en 1024 × 768 es la pantalla entera de ancho y
// de 728 a 39,999935 de alto (el 430 escalado da 688,00006). La v de la izquierda se corre un
// cuarto de dv: (u, v + 0,25 dv) arriba y (u, v + 0,75 dv) abajo.
TEST(OriginalSkewedQuadTest, IsAlwaysScaledAndSkewsTheLeftSide)
{
    const OriginalSkewedQuad quad{
        .x = 0, .y = 25, .width = 640, .height = 430, .u = 0.25f, .v = 0.5f, .du = 0.5f, .dv = 0.25f};

    ExpectCorners(SkewedFanCorners(quad, Screen1024), Expected{{{0, 728, 0.25f, 0.5625f},
                                                                {0, 39.999935150146484f, 0.25f, 0.6875f},
                                                                {1024, 39.999935150146484f, 0.75f, 0.75f},
                                                                {1024, 728, 0.75f, 0.5f}}});
}

// 0x6220b7..0x6220c2: la x87 suma v + dv, lo guarda en float para la esquina de abajo a la derecha
// y le resta 0,25 dv al valor de la pila, sin redondear. Con v = 0,01 y dv = 0,2 la de abajo a la
// izquierda da 0,15999999; restando del float guardado, o sumando v + 0,75 dv, daría 0,16000001.
TEST(OriginalSkewedQuadTest, TheLowerLeftVComesFromTheUnroundedSum)
{
    const OriginalSkewedQuad quad{.width = 640, .height = 480, .v = 0.01f, .dv = 0.2f};

    const QuadCorners corners = SkewedFanCorners(quad, Screen640);

    EXPECT_EQ(corners[1].v, 0.1599999964237213f);
    EXPECT_EQ(corners[2].v, 0.21000000834465027f);
}
} // namespace
