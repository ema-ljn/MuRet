#include "OriginalQuad.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

// Labs::FanVertices: los cuatro vértices y las cuatro UV de 0x621b60 (CL5.0, paso 2), con la Y dada
// vuelta, la escala desde 640 × 480 y las dos cuentas del desensamblado (actividades 5 y 14).
namespace
{
using Labs::FanVertices;
using Labs::OriginalQuad;
using Labs::QuadVertex;
using Labs::ScaleX;
using Labs::ScaleY;
using Labs::Screen;

constexpr Screen Screen640{640, 480};
constexpr Screen Screen1024{1024, 768};
constexpr Screen Screen1280{1280, 1024};

// El fondo 0x15 del login: 0x621b60(0x15, 0, 25, 320, 320, 0, 0, 1, 1, 1, 1, 0) en 0x6350b0, con
// las dos banderas de escalar prendidas (la tabla del plan de la CL5).
constexpr OriginalQuad LoginLeftBackground{
    .x = 0, .y = 25, .width = 320, .height = 320, .scaleSize = true, .scalePosition = true};

// Las cuatro posiciones esperadas, en el orden del fan.
using Positions = std::array<std::array<float, 2>, 4>;

// EXPECT_FLOAT_EQ deja hasta 4 ULP de diferencia: el original no da enteros justos (actividad 5).
void ExpectPositions(const std::array<QuadVertex, 4>& vertices, const Positions& expected)
{
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_FLOAT_EQ(vertices[i].x, expected[i][0]);
        EXPECT_FLOAT_EQ(vertices[i].y, expected[i][1]);
    }
}

// En 640 × 480 la y de arriba es H − y = 455 y la de abajo, H − y − h = 135: el (0, 0) del original
// está arriba a la izquierda, y el de gluOrtho2D, abajo. Con las banderas prendidas, el original
// escala aunque la ventana sea de 640 × 480, y la de abajo sale 134,99997, dos ULP debajo de 135
// (TheBottomComesFromTheUnroundedTop).
TEST(OriginalQuadTest, FlipsYLikeTheOriginal)
{
    ExpectPositions(FanVertices(LoginLeftBackground, Screen640),
                    Positions{{{0, 455}, {0, 135}, {320, 135}, {320, 455}}});
}

// En 1024 × 768, 25 pasa a 40 y 320 pasa a 512 en x y en y, porque cada eje se escala por su lado.
TEST(OriginalQuadTest, ScalesFrom640x480)
{
    ExpectPositions(FanVertices(LoginLeftBackground, Screen1024),
                    Positions{{{0, 728}, {0, 216}, {512, 216}, {512, 728}}});
}

// El fondo 0x16 del login va en x = 320. Con solo la posición escalada, la esquina pasa a (512, 40)
// y el tamaño sigue en 320; con solo el tamaño, la esquina sigue en (320, 25) y el tamaño pasa a
// 512. Sin ninguna, todo queda en píxeles.
TEST(OriginalQuadTest, ScalesPositionAndSizeApart)
{
    OriginalQuad right{.x = 320, .y = 25, .width = 320, .height = 320};

    right.scalePosition = true;
    ExpectPositions(FanVertices(right, Screen1024), Positions{{{512, 728}, {512, 408}, {832, 408}, {832, 728}}});

    right.scalePosition = false;
    right.scaleSize = true;
    ExpectPositions(FanVertices(right, Screen1024), Positions{{{320, 743}, {320, 231}, {832, 231}, {832, 743}}});

    right.scaleSize = false;
    ExpectPositions(FanVertices(right, Screen1024), Positions{{{320, 743}, {320, 423}, {640, 423}, {640, 743}}});
}

// La v crece hacia abajo: el vértice de arriba lleva v y el de abajo v + dv, así que la fila 0 del
// buffer de la textura queda arriba. Las UV no se escalan.
TEST(OriginalQuadTest, TheUvGoesDownTheTexture)
{
    const OriginalQuad quad{
        .x = 10, .y = 20, .width = 30, .height = 40, .u = 0.25f, .v = 0.5f, .du = 0.5f, .dv = 0.25f};
    const std::array<QuadVertex, 4> vertices = FanVertices(quad, Screen1024);
    const std::array<std::array<float, 2>, 4> expected{{{0.25f, 0.5f}, {0.25f, 0.75f}, {0.75f, 0.75f}, {0.75f, 0.5f}}};
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(vertices[i].u, expected[i][0]);
        EXPECT_EQ(vertices[i].v, expected[i][1]);
    }
}

// Detective de números (actividad 5): 0x620d50 y 0x620d80 multiplican por 1/640 y 1/480 guardados
// en float (0x3acccccd y 0x3b088889 en 0x6ba070 y 0x6ba06c), que no son exactos. 25 en 480 da
// 25,000002 y no 25; en 768, 40,000004. 320 en 1024 sí da 512: el error no llega a medio ULP.
TEST(OriginalQuadTest, ScalesByTheInverseOfTheSize)
{
    EXPECT_EQ(ScaleY(25, Screen640), 25.000001907348633f);
    EXPECT_NE(ScaleY(25, Screen640), 25.0f);
    EXPECT_EQ(ScaleY(25, Screen1024), 40.000003814697266f);
    EXPECT_EQ(ScaleX(320, Screen1024), 512.0f);
    EXPECT_EQ(ScaleY(320, Screen1024), 512.0f);
    EXPECT_EQ(ScaleY(320, Screen640), 320.0000305175781f);
}

// Carril R (actividad 14): en 0x621bd7..0x621bf3 el x87 resta y de H, guarda el resultado como
// float (FST) y resta h del valor que quedó en la pila, sin redondear. El decompilado lo escribe
// como si restara del float guardado. En 1280 × 1024, con el fondo 0x15, la diferencia se ve: la de
// abajo es 287,99997, y restando del float de arriba daría 288 justos.
TEST(OriginalQuadTest, TheBottomComesFromTheUnroundedTop)
{
    const std::array<QuadVertex, 4> vertices = FanVertices(LoginLeftBackground, Screen1280);
    EXPECT_EQ(vertices[0].y, 970.6666870117188f);
    EXPECT_EQ(vertices[1].y, 287.99996948242188f);
    EXPECT_EQ(vertices[0].y - ScaleY(LoginLeftBackground.height, Screen1280), 288.0f);
}
} // namespace
