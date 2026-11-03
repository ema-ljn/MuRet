#include "Fan.h"
#include "OriginalQuad.h"
#include "Ortho.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <vector>

// Jefe de la CL5.0 (docs/learning/ola-cl5.0-el-pipeline-grafico.md, actividad 9): el pipeline del
// original contra el de MuRet, sin GL.
//
// El original manda cada vértice con glVertex2f, con gluOrtho2D(0, W, 0, H) y el viewport entero
// (0x6219b0): el píxel donde cae es el vértice mismo. MuRet manda los mismos vértices en un VBO; el
// vertex shader los multiplica por la matriz de Ortho2D, y GL los pasa del NDC a la ventana con el
// viewport. Si los dos caminos dan el mismo píxel, el 2D del original se puede reproducir al
// vértice. Al final, el pase 3D: 0x620db0 pasa un rectángulo de 640 × 480 a un glViewport.
namespace
{
using Labs::FanToTriangles;
using Labs::FanVertices;
using Labs::Matrix4;
using Labs::Multiply;
using Labs::OriginalQuad;
using Labs::Ortho2D;
using Labs::QuadVertex;
using Labs::Screen;
using Labs::Triangle;
using Labs::Vec4;

constexpr Screen Screen640{640, 480};
constexpr Screen Screen800{800, 600};
constexpr Screen Screen1024{1024, 768};
constexpr Screen Screen1280{1280, 1024};
constexpr Screen OriginalSizes[] = {Screen640, Screen800, Screen1024, Screen1280};

// El fondo 0x15 del login (0x6350b0), como en test_original_quad.cpp.
constexpr OriginalQuad LoginLeftBackground{
    .x = 0, .y = 25, .width = 320, .height = 320, .scaleSize = true, .scalePosition = true};

// Un milésimo de píxel: lo que se pierde en float al ir de la ventana al NDC y volver.
constexpr float PixelTolerance = 1e-3f;

struct Viewport
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    friend bool operator==(const Viewport&, const Viewport&) = default;
};

void PrintTo(const Viewport& viewport, std::ostream* out)
{
    *out << "(" << viewport.x << ", " << viewport.y << ", " << viewport.width << ", " << viewport.height << ")";
}

struct WindowPoint
{
    float x = 0;
    float y = 0;
};

// El viewport entero, el de 0x6219b0.
Viewport FullViewport(Screen screen)
{
    return Viewport{0, 0, screen.width, screen.height};
}

// Lo que hace GL después del vertex shader: divide por w (el NDC, de −1 a 1) y pasa al viewport.
WindowPoint ToWindow(Vec4 clip, const Viewport& viewport)
{
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    return WindowPoint{static_cast<float>(viewport.x) + (ndcX + 1) * static_cast<float>(viewport.width) / 2,
                       static_cast<float>(viewport.y) + (ndcY + 1) * static_cast<float>(viewport.height) / 2};
}

Vec4 ToClip(const Matrix4& projection, float x, float y)
{
    return Multiply(projection, Vec4{x, y});
}

// 0x620db0 y 0x620d10: el rectángulo, en 640 × 480, pasa a la ventana en enteros sin signo (IMUL, y
// MUL por 0xcccccccd o 0x88888889 con su SHR, que es dividir por 640 o por 480, truncando), y la y
// se da vuelta: glViewport(x, H − y − h, w, h).
Viewport OriginalViewport(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, Screen screen)
{
    const auto screenWidth = static_cast<std::uint32_t>(screen.width);
    const auto screenHeight = static_cast<std::uint32_t>(screen.height);
    const std::uint32_t left = screenWidth * x / 640;
    const std::uint32_t top = screenHeight * y / 480;
    const std::uint32_t scaledWidth = screenWidth * width / 640;
    const std::uint32_t scaledHeight = screenHeight * height / 480;
    return Viewport{static_cast<int>(left), static_cast<int>(screenHeight - top - scaledHeight),
                    static_cast<int>(scaledWidth), static_cast<int>(scaledHeight)};
}

float Cross(float ax, float ay, float bx, float by)
{
    return ax * by - ay * bx;
}

// Los pesos del punto (u, v) en el triángulo (a, b, c) de la textura: sus coordenadas baricéntricas
// en el espacio de las UV. Si el punto cae afuera, no hay. Es lo que hace GL al interpolar: cada
// fragmento recibe la UV de sus tres vértices pesada así.
std::optional<std::array<float, 3>> UvWeights(const QuadVertex& a, const QuadVertex& b, const QuadVertex& c, float u,
                                              float v)
{
    const float area = Cross(b.u - a.u, b.v - a.v, c.u - a.u, c.v - a.v);
    const float weightB = Cross(u - a.u, v - a.v, c.u - a.u, c.v - a.v) / area;
    const float weightC = Cross(b.u - a.u, b.v - a.v, u - a.u, v - a.v) / area;
    const float weightA = 1 - weightB - weightC;
    if (weightA < 0 || weightB < 0 || weightC < 0)
    {
        return std::nullopt;
    }
    return std::array<float, 3>{weightA, weightB, weightC};
}

// El punto de la ventana con la UV (u, v): busca el triángulo del fan que la tiene y pesa las
// posiciones de sus vértices como las UV.
std::optional<WindowPoint> PointWithUv(const std::array<QuadVertex, 4>& vertices, const Triangle& triangle, float u,
                                       float v)
{
    const QuadVertex& a = vertices[triangle[0]];
    const QuadVertex& b = vertices[triangle[1]];
    const QuadVertex& c = vertices[triangle[2]];
    const std::optional<std::array<float, 3>> weights = UvWeights(a, b, c, u, v);
    if (!weights)
    {
        return std::nullopt;
    }
    const auto [weightA, weightB, weightC] = *weights;
    return WindowPoint{weightA * a.x + weightB * b.x + weightC * c.x, weightA * a.y + weightB * b.y + weightC * c.y};
}

// El área con signo de un triángulo ya en el NDC: positiva si gira en contra del reloj, el frente
// de GL con glFrontFace(GL_CCW), que es el que viene por defecto.
float SignedArea(const Matrix4& projection, const std::array<QuadVertex, 4>& vertices, const Triangle& triangle)
{
    const Vec4 a = ToClip(projection, vertices[triangle[0]].x, vertices[triangle[0]].y);
    const Vec4 b = ToClip(projection, vertices[triangle[1]].x, vertices[triangle[1]].y);
    const Vec4 c = ToClip(projection, vertices[triangle[2]].x, vertices[triangle[2]].y);
    return Cross(b.x - a.x, b.y - a.y, c.x - a.x, c.y - a.y) / 2;
}

// En 1024 × 768, el 0x15 va de (0, 728) a (512, 216): la esquina de arriba a la izquierda cae en
// (−1; 0,8958) del NDC y la de abajo a la derecha, en (0; −0,4375).
TEST(OriginalPipelineTest, TheLoginBackgroundLandsInNdc)
{
    const std::array<QuadVertex, 4> vertices = FanVertices(LoginLeftBackground, Screen1024);
    const Matrix4 projection = Ortho2D(0, 1024, 0, 768);

    const Vec4 topLeft = ToClip(projection, vertices[0].x, vertices[0].y);
    EXPECT_FLOAT_EQ(topLeft.x, -1);
    EXPECT_NEAR(topLeft.y, 43.0f / 48, 1e-6f);
    EXPECT_FLOAT_EQ(topLeft.w, 1);

    const Vec4 bottomRight = ToClip(projection, vertices[2].x, vertices[2].y);
    EXPECT_NEAR(bottomRight.x, 0, 1e-6f);
    EXPECT_FLOAT_EQ(bottomRight.y, -0.4375f);
}

// La UV (0,5; 0,5) está en la diagonal 0-2, que comparten los dos triángulos del fan: los dos dan
// el centro del quad, (256, 472) en la ventana, (−0,5; 0,2292) en el NDC.
TEST(OriginalPipelineTest, TheMiddleOfTheUvIsTheMiddleOfTheQuad)
{
    const std::array<QuadVertex, 4> vertices = FanVertices(LoginLeftBackground, Screen1024);
    const Matrix4 projection = Ortho2D(0, 1024, 0, 768);
    const std::vector<Triangle> triangles = FanToTriangles(static_cast<std::uint32_t>(vertices.size()));
    ASSERT_EQ(triangles.size(), 2u);

    for (const Triangle& triangle : triangles)
    {
        SCOPED_TRACE(triangle[2]);
        const std::optional<WindowPoint> middle = PointWithUv(vertices, triangle, 0.5f, 0.5f);
        ASSERT_TRUE(middle.has_value());
        EXPECT_FLOAT_EQ(middle->x, 256);
        EXPECT_FLOAT_EQ(middle->y, 472);

        const Vec4 clip = ToClip(projection, middle->x, middle->y);
        EXPECT_FLOAT_EQ(clip.x, -0.5f);
        EXPECT_NEAR(clip.y, 11.0f / 48, 1e-6f);
    }
}

// Lo que dibuja el original y lo que dibujaría MuRet: con la matriz y el viewport entero, cada
// vértice vuelve del NDC al mismo punto de la ventana, en las cuatro resoluciones del original.
TEST(OriginalPipelineTest, TheNdcGoesBackToThePixelOfTheOriginal)
{
    for (const Screen screen : OriginalSizes)
    {
        SCOPED_TRACE(screen.width);
        const Matrix4 projection = Ortho2D(0, static_cast<float>(screen.width), 0, static_cast<float>(screen.height));
        for (const QuadVertex& vertex : FanVertices(LoginLeftBackground, screen))
        {
            const WindowPoint pixel = ToWindow(ToClip(projection, vertex.x, vertex.y), FullViewport(screen));
            EXPECT_NEAR(pixel.x, vertex.x, PixelTolerance);
            EXPECT_NEAR(pixel.y, vertex.y, PixelTolerance);
        }
    }
}

// El orden del fan, (x, arriba), (x, abajo), (x + w, abajo), (x + w, arriba), gira en contra del
// reloj con la y para arriba: los dos triángulos dan el frente. Con la y para abajo
// (Ortho2D(0, W, H, 0)), los mismos vértices quedan de espaldas, y con el culling prendido no se
// verían: dar vuelta la y dos veces no es gratis.
TEST(OriginalPipelineTest, BothTrianglesFaceTheCamera)
{
    const std::array<QuadVertex, 4> vertices = FanVertices(LoginLeftBackground, Screen1024);
    const Matrix4 yUp = Ortho2D(0, 1024, 0, 768);
    const Matrix4 yDown = Ortho2D(0, 1024, 768, 0);
    for (const Triangle& triangle : FanToTriangles(4))
    {
        SCOPED_TRACE(triangle[2]);
        EXPECT_GT(SignedArea(yUp, vertices, triangle), 0);
        EXPECT_LT(SignedArea(yDown, vertices, triangle), 0);
    }
}

// El pase 3D del login, 0x620db0(0, 25, 640, 430) en 0x6350b0, es el de la selección (0x634680), y
// su traza dice glViewport(0, 40, 1024, 688) en 1024 × 768 (06, *verificado*). En 640 × 480 queda
// el rectángulo mismo, con la y dada vuelta: de 25 a 455 desde abajo.
TEST(OriginalPipelineTest, TheLoginThreeDViewportIsTheSelectionTraceRow)
{
    EXPECT_EQ(OriginalViewport(0, 25, 640, 430, Screen1024), (Viewport{0, 40, 1024, 688}));
    EXPECT_EQ(OriginalViewport(0, 25, 640, 430, Screen640), (Viewport{0, 25, 640, 430}));
}

// La cámara del juego, 0x620db0(0, 0, 640, 432) en 0x6371a0: 768 · 432 / 480 es 691,2, y la
// división entera da 691, el glViewport(0, 77, 1024, 691) de la traza (06, *verificado*).
TEST(OriginalPipelineTest, TheGameViewportTruncatesLikeTheTrace)
{
    EXPECT_EQ(OriginalViewport(0, 0, 640, 432, Screen1024), (Viewport{0, 77, 1024, 691}));
}

// En 800 × 600 el viewport trunca y los quads no (*estático*): el rectángulo del pase 3D, de y = 25
// a 455, como viewport es glViewport(0, 32, 800, 537) y va de 32 a 569 desde abajo; como quad
// escalado va de 31,249998 (la resta sin redondear de test_original_quad.cpp; restando del float de
// arriba sería 31,25 justo) a 568,75. Lo 3D queda corrido 0,75 píxeles arriba de lo 2D en un borde
// y 0,25 en el otro.
TEST(OriginalPipelineTest, At800x600TheViewportTruncatesAndTheQuadDoesNot)
{
    const Viewport viewport = OriginalViewport(0, 25, 640, 430, Screen800);
    EXPECT_EQ(viewport, (Viewport{0, 32, 800, 537}));

    const OriginalQuad sameArea{.x = 0, .y = 25, .width = 640, .height = 430, .scaleSize = true, .scalePosition = true};
    const std::array<QuadVertex, 4> vertices = FanVertices(sameArea, Screen800);
    EXPECT_EQ(vertices[0].y, 568.75f);
    EXPECT_EQ(vertices[1].y, 31.249998092651367f);
    EXPECT_NEAR(static_cast<float>(viewport.y) - vertices[1].y, 0.75f, 1e-5f);
    EXPECT_NEAR(static_cast<float>(viewport.y + viewport.height) - vertices[0].y, 0.25f, 1e-5f);
}
} // namespace
