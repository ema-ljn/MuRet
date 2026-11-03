#include "Render/UiImage.h"

#include "App/WindowState.h"
#include "Render/QuadCorner.h"
#include "Render/TextureSlot.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

// Client::Render::UiImageCorners y DesignScale: la imagen de la UI nueva (0x647050, 0x6472e0 y
// 0x647650), con la y desde arriba en su propio espacio, cada esquina por fx y fy, y las UV a medio
// téxel (06 § La carga). Lo que hace el SpriteBatch con ella, en test_sprite_batch.cpp.
namespace
{
using Client::App::PixelSize;
using Client::Render::DesignScale;
using Client::Render::QuadCorners;
using Client::Render::TextureSlot;
using Client::Render::UiImage;
using Client::Render::UiImageCorners;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};

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

// Una imagen de 329 × 245 en píxeles, del tamaño del fondo de la ventana de la cuenta (0x13, en un
// buffer de 512 × 256), ubicada como esa ventana en 640 × 480: ((W − w) / 2, (H − h) · 2 / 3), que
// es (155, 156) (06 § Las ventanas de la UI nueva).
constexpr UiImage AccountSizedImage{.slot = TextureSlot{0x13},
                                    .x = 155,
                                    .y = 156,
                                    .width = 329,
                                    .height = 245,
                                    .textureWidth = 512,
                                    .textureHeight = 256};

// Con fx = fy = 1 la imagen va en píxeles, y la y sale de H: el borde de arriba es 480 − 156 = 324
// y el de abajo, 324 − 245 = 79. Las UV van a medio téxel de cada borde: 0,5 / 512 y
// (329 − 0,5) / 512 en u, 0,5 / 256 y (245 − 0,5) / 256 en v, con la fila 0 arriba.
TEST(UiImageTest, InPixelsTheYGoesDownFromTheHeight)
{
    ExpectCorners(UiImageCorners(AccountSizedImage, Screen640), Expected{{{155, 324, 0.0009765625f, 0.001953125f},
                                                                          {155, 79, 0.0009765625f, 0.955078125f},
                                                                          {484, 79, 0.6416015625f, 0.955078125f},
                                                                          {484, 324, 0.6416015625f, 0.001953125f}}});
}

// En otra pantalla, la misma imagen en píxeles no cambia de tamaño: solo la y, que sale de H.
TEST(UiImageTest, InPixelsTheSizeDoesNotScale)
{
    const QuadCorners corners = UiImageCorners(AccountSizedImage, Screen1024);

    EXPECT_EQ(corners[0].y, 612.0f);
    EXPECT_EQ(corners[1].y, 367.0f);
    EXPECT_EQ(corners[2].x - corners[1].x, 329.0f);
}

// En la carga, diseñada en 800 × 600, fx es 640 · (1/800 en float) = 0,79999995 y fy, 0,8 justos
// más un poco: el borde de arriba es H / fy = 600 menos la y, por fy. El fondo 0x71f, de 288 × 478
// en (512, 60), va de x = 409,59998 a 639,99994, un poco antes del borde de la pantalla.
TEST(UiImageTest, EachCornerGoesByItsScale)
{
    const UiImage right{.slot = TextureSlot{0x71f},
                        .x = 512,
                        .y = 60,
                        .width = 288,
                        .height = 478,
                        .textureWidth = 512,
                        .textureHeight = 512,
                        .scaleX = DesignScale(640, 800),
                        .scaleY = DesignScale(480, 600)};

    ExpectCorners(UiImageCorners(right, Screen640),
                  Expected{{{409.5999755859375f, 432, 0.0009765625f, 0.0009765625f},
                            {409.5999755859375f, 49.60000228881836f, 0.0009765625f, 0.9326171875f},
                            {639.9999389648438f, 49.60000228881836f, 0.5615234375f, 0.9326171875f},
                            {639.9999389648438f, 432, 0.5615234375f, 0.0009765625f}}});
}

// Sin slot (el -1 del original) 0x647050 deja las UV en 0: la imagen es solo su color.
TEST(UiImageTest, WithoutASlotTheUvIsZero)
{
    UiImage colorOnly = AccountSizedImage;
    colorOnly.slot.reset();

    for (const auto& corner : UiImageCorners(colorOnly, Screen640))
    {
        EXPECT_EQ(corner.u, 0.0f);
        EXPECT_EQ(corner.v, 0.0f);
    }
}

// La barra de la carga (0x6402d0 y 0x640580): 0x647370 le cambia el ancho y el alto después de
// armarla, y con ellos la u de la derecha (ancho / tw, 0x6473dd) y la v de abajo (alto / th,
// 0x647457), sin medio téxel; la izquierda y la de arriba siguen a medio téxel. Con una barra de
// 328 de ancho (la mitad de 656) y 15 de alto en un buffer de 4 × 16, la u llega a 82: con
// GL_REPEAT, la textura se repite a lo largo.
TEST(UiImageTest, AResizedImageEndsItsUvOnItsSize)
{
    const UiImage bar{.slot = TextureSlot{0x723},
                      .x = 72,
                      .y = 482,
                      .width = 328,
                      .height = 15,
                      .textureWidth = 4,
                      .textureHeight = 16,
                      .resizedWidth = true,
                      .resizedHeight = true};

    ExpectCorners(UiImageCorners(bar, Screen640), Expected{{{72, -2, 0.125f, 0.03125f},
                                                            {72, -17, 0.125f, 0.9375f},
                                                            {400, -17, 82.0f, 0.9375f},
                                                            {400, -2, 82.0f, 0.03125f}}});
}

// Cada eje por su lado: solo el ancho cambiado deja la v de abajo a medio téxel.
TEST(UiImageTest, OnlyTheResizedSideLosesTheHalfTexel)
{
    UiImage bar{.slot = TextureSlot{0x723},
                .x = 0,
                .y = 0,
                .width = 8,
                .height = 15,
                .textureWidth = 4,
                .textureHeight = 16,
                .resizedWidth = true};

    QuadCorners corners = UiImageCorners(bar, Screen640);
    EXPECT_EQ(corners[2].u, 2.0f);
    EXPECT_EQ(corners[2].v, 14.5f / 16.0f);

    bar.resizedWidth = false;
    bar.resizedHeight = true;
    corners = UiImageCorners(bar, Screen640);
    EXPECT_EQ(corners[2].u, 7.5f / 4.0f);
    EXPECT_EQ(corners[2].v, 15.0f / 16.0f);
}

// 0x65eeb0: W · (1/800 en float) y H · (1/600 en float), guardados en float. Ninguna inversa es
// exacta: en 640 × 480, 0,79999995 y 0,80000001; en 800 × 600, 1 justo.
TEST(DesignScaleTest, MultipliesByTheInverseOfTheDesign)
{
    EXPECT_EQ(DesignScale(640, 800), 0.7999999523162842f);
    EXPECT_EQ(DesignScale(480, 600), 0.800000011920929f);
    EXPECT_EQ(DesignScale(1024, 800), 1.2799999713897705f);
    EXPECT_EQ(DesignScale(800, 800), 1.0f);
    EXPECT_EQ(DesignScale(600, 600), 1.0f);
}
} // namespace
