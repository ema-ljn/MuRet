#include "LabGlTest.h"

#include "GlName.h"
#include "LabFramebuffer.h"
#include "LabQuadRenderer.h"
#include "LabTexture.h"

#include "Core/Expected.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>

// Labs::RequiredUnpackBytes y UploadTexture: GL_UNPACK_ALIGNMENT, lo que más puede morder al subir
// un OZJ (actividades 10 y 13 de la guía). Los dos primeros no abren GL.
namespace
{
using Labs::GlName;
using Labs::LabFramebuffer;
using Labs::LabQuadRenderer;
using Labs::Quadrants;
using Labs::RequiredUnpackBytes;
using Labs::Rgba;
using Labs::TextureImage;

using LabTextureTest = Labs::LabGlTest;

// Los cuatro colores de test_lab_framebuffer.cpp, juntos como los deja un OZJ de 2 × 2 (12 bytes):
// la fila 0, rojo y verde; la fila 1, azul y blanco. Detrás, cuatro bytes de marca, 0xaa, que no
// son de la imagen: si GL los lee, se ven. Los dos renglones, de 8 bytes, empiezan donde GL lee
// cada fila con el 4.
constexpr std::uint8_t Mark = 0xaa;
constexpr std::array<std::uint8_t, 16> FourColorsAndMarks{255, 0,   0,   0,   255,  0,    0,    0,
                                                          255, 255, 255, 255, Mark, Mark, Mark, Mark};

// Una fila de 6 bytes con el 4 ocupa 8; la última no lleva relleno. Con 1, todo junto. Una fila
// RGBA, de 4 bytes por píxel, nunca necesita relleno.
TEST(LabTextureRulesTest, RequiredBytesFollowTheUnpackAlignment)
{
    EXPECT_EQ(RequiredUnpackBytes(2, 2, 3, 1), 12u);
    EXPECT_EQ(RequiredUnpackBytes(2, 2, 3, 4), 14u);
    EXPECT_EQ(RequiredUnpackBytes(1, 1, 3, 4), 3u);
    EXPECT_EQ(RequiredUnpackBytes(3, 2, 3, 4), 21u);
    EXPECT_EQ(RequiredUnpackBytes(2, 2, 4, 4), 16u);
    EXPECT_EQ(RequiredUnpackBytes(2, 0, 3, 4), 0u);
}

// Los 12 bytes de un OZJ de 2 × 2 no alcanzan con el 4: GL leería 14. UploadTexture lo dice antes
// de llamar a GL, así que se prueba con una tabla vacía, sin contexto.
TEST(LabTextureRulesTest, RefusesABufferShorterThanWhatGlReads)
{
    const Labs::LabGl noGl{};
    const TextureImage tight{.width = 2, .height = 2, .channels = 3, .pixels = std::span(FourColorsAndMarks).first(12)};
    const Client::Core::Expected<GlName, std::string> refused = Labs::UploadTexture(noGl, tight, 4);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_NE(refused.Error().find("14"), std::string::npos) << refused.Error();
}

// Rompelo (actividad 13): los mismos 12 bytes juntos, con GL_UNPACK_ALIGNMENT en 4, el que GL tiene
// por defecto. La fila 0 sale bien, pero la 1 empieza en el byte 8 y no en el 6: abajo a la
// izquierda va (255, 255, 255), los bytes 8 a 10, y abajo a la derecha (255, 0xaa, 0xaa), el último
// byte del blanco y dos de las marcas. GL leyó 2 bytes que no eran de la imagen.
TEST_F(LabTextureTest, TheDefaultAlignmentReadsTheSecondRowFromByteEight)
{
    Client::Core::Expected<LabFramebuffer, std::string> framebuffer =
        LabFramebuffer::Create(Gl(), Labs::FramebufferSide, Labs::FramebufferSide);
    ASSERT_TRUE(framebuffer.HasValue()) << framebuffer.Error();
    Client::Core::Expected<LabQuadRenderer, std::string> renderer = LabQuadRenderer::Create(Gl());
    ASSERT_TRUE(renderer.HasValue()) << renderer.Error();
    const TextureImage shifted{.width = 2, .height = 2, .channels = 3, .pixels = FourColorsAndMarks};
    Client::Core::Expected<GlName, std::string> texture = Labs::UploadTexture(Gl(), shifted, 4);
    ASSERT_TRUE(texture.HasValue()) << texture.Error();

    renderer.Value().Draw(Labs::FullFramebufferQuad(), texture.Value().Get(), Labs::FramebufferProjection());
    ASSERT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));

    const Quadrants quadrants = Labs::ReadQuadrants(framebuffer.Value());
    EXPECT_EQ(quadrants.topLeft, (Rgba{255, 0, 0, 255}));
    EXPECT_EQ(quadrants.topRight, (Rgba{0, 255, 0, 255}));
    EXPECT_EQ(quadrants.bottomLeft, (Rgba{255, 255, 255, 255}));
    EXPECT_EQ(quadrants.bottomRight, (Rgba{255, Mark, Mark, 255}));
}
} // namespace
