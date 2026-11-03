#include "Render/ScreenScale.h"

#include "App/WindowState.h"

#include <gtest/gtest.h>

// Client::Render::ScaleX y ScaleY: 0x620d50 y 0x620d80, de 640 × 480 a la pantalla, cada eje por
// su lado y multiplicando por la inversa en float (06 § Primitivas 2D).
namespace
{
using Client::App::PixelSize;
using Client::Render::ScaleX;
using Client::Render::ScaleY;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};
constexpr PixelSize Screen1920{1920, 1080};

// x va por W / 640 e y por H / 480: en 1024 × 768 los dos factores son 1,6, y en 1920 × 1080, 3 y
// 2,25, así que una pantalla ancha estira lo escalado (pregunta 10 del plan de la CL5). Los 25 dan
// un ULP arriba (MultipliesByTheInverseOfTheSize): EXPECT_FLOAT_EQ deja hasta 4.
TEST(ScreenScaleTest, ScalesEachAxisApart)
{
    EXPECT_FLOAT_EQ(ScaleX(320, Screen1024), 512.0f);
    EXPECT_FLOAT_EQ(ScaleY(25, Screen1024), 40.0f);
    EXPECT_FLOAT_EQ(ScaleX(320, Screen1920), 960.0f);
    EXPECT_FLOAT_EQ(ScaleY(25, Screen1920), 56.25f);
    EXPECT_FLOAT_EQ(ScaleY(320, Screen1920), 720.0f);
}

// Las inversas de 0x6ba070 y 0x6ba06c no son exactas: 25 en 480 da 25,000002, en 768 da 40,000004
// y en 1080, 56,250004. Dividiendo por 480 darían 25, 40 y 56,25 justos. 320 en 1024 sí da 512.
TEST(ScreenScaleTest, MultipliesByTheInverseOfTheSize)
{
    EXPECT_EQ(ScaleY(25, Screen640), 25.000001907348633f);
    EXPECT_EQ(ScaleY(25, Screen1024), 40.000003814697266f);
    EXPECT_EQ(ScaleY(25, Screen1920), 56.250003814697266f);
    EXPECT_EQ(ScaleY(320, Screen640), 320.0000305175781f);
    EXPECT_EQ(ScaleX(320, Screen1024), 512.0f);
}

// Con 640 × 480 no hay atajo: el original escala igual, y 0 sigue en 0.
TEST(ScreenScaleTest, In640x480ItStillScales)
{
    EXPECT_NE(ScaleY(25, Screen640), 25.0f);
    EXPECT_EQ(ScaleX(0, Screen640), 0.0f);
    EXPECT_EQ(ScaleY(0, Screen1920), 0.0f);
}
} // namespace
