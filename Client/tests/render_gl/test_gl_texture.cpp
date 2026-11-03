#include "Render/Gl/GlTexture.h"

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Textures/OztTexture.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Platform/GlLoader.h"
#include "support/GlDrawTest.h"
#include "support/TextureFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// Client::Render::Gl::GlTexture: el buffer de lados potencia de 2 de un OZT o un OZJ (05 §
// Texturas) subido como lo sube el original (0x63bd00 y 0x63b870), con el filtro y el wrap de su
// tercer y cuarto argumento. Cada test lo dibuja con GlRenderer en el framebuffer y lee los téxeles
// por celdas: con GL_NEAREST y el rectángulo que cubre el framebuffer, cada celda es un téxel.
namespace
{
using Client::Data::Textures::Filter;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::Sampling;
using Client::Data::Textures::Texture;
using Client::Data::Textures::Wrap;
using Client::Render::Gl::GlTexture;
using Client::Render::Gl::Rgba8;
using Client::Tests::TestQuad;

using GlTextureTest = Client::Tests::GlDrawTest;
using CellColors = std::vector<std::optional<Rgba8>>;

constexpr Rgba8 Red{255, 0, 0, 255};
constexpr Rgba8 Green{0, 255, 0, 255};
constexpr Rgba8 Blue{0, 0, 255, 255};
constexpr Rgba8 White{255, 255, 255, 255};
constexpr Rgba8 Black{0, 0, 0, 255};

constexpr Sampling NearestClamp{Filter::Nearest, Wrap::Clamp};

// Una textura armada a mano: `pixels` es el buffer entero, de textureWidth × textureHeight.
Texture MakeTexture(PixelFormat format, int width, int height, std::vector<std::uint8_t> pixels)
{
    return Texture{.width = width,
                   .height = height,
                   .textureWidth = Client::Data::Textures::TextureSide(width),
                   .textureHeight = Client::Data::Textures::TextureSide(height),
                   .format = format,
                   .pixels = std::move(pixels)};
}

// El téxel (x, y) del buffer de `texture`, con y = 0 la primera fila que sube glTexImage2D. Uno
// RGB, como lo lee GL de una textura sin alfa, con el alfa en 255.
Rgba8 TexelOf(const Texture& texture, int x, int y)
{
    const std::size_t bytes = Client::Data::Textures::BytesPerPixel(texture.format);
    const std::size_t index =
        ((static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.textureWidth)) + static_cast<std::size_t>(x)) *
        bytes;
    const std::uint8_t alpha = texture.format == PixelFormat::Rgba ? texture.pixels[index + 3] : std::uint8_t{255};
    return Rgba8{texture.pixels[index], texture.pixels[index + 1], texture.pixels[index + 2], alpha};
}

// Si los píxeles no son el buffer entero, Upload no llama a GL: GL leería fuera del vector. Se
// prueba con una tabla vacía, sin contexto. Un buffer de cero de lado tampoco pasa.
TEST(GlTextureRulesTest, RefusesPixelsThatAreNotTheWholeBuffer)
{
    const Client::Platform::GlFunctions noGl{};
    const Texture shortRgb = MakeTexture(PixelFormat::Rgb, 2, 2, std::vector<std::uint8_t>(11));
    const Client::Core::Expected<GlTexture, std::string> refused = GlTexture::Upload(noGl, shortRgb, NearestClamp);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_NE(refused.Error().find("12"), std::string::npos) << refused.Error();
    EXPECT_NE(refused.Error().find("11"), std::string::npos) << refused.Error();

    Texture empty = MakeTexture(PixelFormat::Rgba, 1, 1, {});
    empty.textureWidth = 0;
    EXPECT_FALSE(GlTexture::Upload(noGl, empty, NearestClamp).HasValue());
}

// El OZT sintético de 2 × 2, leído como en la CL1 (las filas dadas vuelta, en RGBA): los cuatro
// téxeles salen como están en el buffer, con su alfa, y la fila 0 arriba, donde la pone el quad
// del original. El tamaño es el del buffer.
TEST_F(GlTextureTest, UploadsAnRgbaTexture)
{
    const auto ozt = Client::Data::Textures::LoadOztTexture(Client::Tests::PlainOzt(2, 2));
    ASSERT_TRUE(ozt.HasValue());
    const Texture& texture = ozt.Value();
    const Client::Core::Expected<GlTexture, std::string> uploaded = GlTexture::Upload(Gl(), texture, NearestClamp);
    ASSERT_TRUE(uploaded.HasValue()) << uploaded.Error();
    EXPECT_NE(uploaded.Value().Name(), 0u);
    EXPECT_EQ(uploaded.Value().Width(), 2);
    EXPECT_EQ(uploaded.Value().Height(), 2);

    Draw(uploaded.Value(), TestQuad{});

    EXPECT_EQ(Cells(2, 2), (CellColors{TexelOf(texture, 0, 0), TexelOf(texture, 1, 0), TexelOf(texture, 0, 1),
                                       TexelOf(texture, 1, 1)}));
}

// Las filas RGB de 1 y 2 píxeles miden 3 y 6 bytes, y con el GL_UNPACK_ALIGNMENT de 4 que GL trae
// por defecto la segunda empezaría en el byte 4 u 8 ("Lo que más puede morder", en el plan de la
// CL5): Upload lo pone en 1 y las filas no se corren. Las de 4 o más de ancho ya miden un múltiplo
// de 4: la de 3 × 2 en su buffer de 4 × 2 muestra además la columna que no es imagen, en cero.
TEST_F(GlTextureTest, UploadsAnRgbTextureOfOddWidth)
{
    const Texture twoWide = MakeTexture(PixelFormat::Rgb, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    const Texture oneWide = MakeTexture(PixelFormat::Rgb, 1, 2, {255, 0, 0, 0, 0, 255});
    const Texture threeWide = MakeTexture(PixelFormat::Rgb, 3, 2, {255, 0,   0,   0, 255, 0,   0, 0,   255, 0, 0, 0,
                                                                   255, 255, 255, 0, 0,   255, 0, 255, 0,   0, 0, 0});
    const CellColors expectedTwo{Red, Green, Blue, White};
    const CellColors expectedOne{Red, Blue};
    const CellColors expectedThree{Red, Green, Blue, Black, White, Blue, Green, Black};

    for (const auto& [texture, columns, expected] :
         {std::tuple{&twoWide, 2, &expectedTwo}, std::tuple{&oneWide, 1, &expectedOne},
          std::tuple{&threeWide, 4, &expectedThree}})
    {
        const Client::Core::Expected<GlTexture, std::string> uploaded = GlTexture::Upload(Gl(), *texture, NearestClamp);
        ASSERT_TRUE(uploaded.HasValue()) << uploaded.Error();
        Draw(uploaded.Value(), TestQuad{});
        EXPECT_EQ(Cells(columns, 2), *expected) << texture->textureWidth << " de ancho";
    }
}

// Upload deja GL_UNPACK_ALIGNMENT como estaba: lo que suba otro después (el texto de la CL5.3) no
// hereda el 1.
TEST_F(GlTextureTest, LeavesTheUnpackAlignmentAsItWas)
{
    constexpr GLint Before = 2;
    Gl().pixelStorei(GL_UNPACK_ALIGNMENT, Before);
    const Texture texture = MakeTexture(PixelFormat::Rgb, 2, 2, std::vector<std::uint8_t>(12));
    ASSERT_TRUE(GlTexture::Upload(Gl(), texture, NearestClamp).HasValue());
    GLint after = 0;
    Gl().getIntegerv(GL_UNPACK_ALIGNMENT, &after);
    EXPECT_EQ(after, Before);
}

// El tercer argumento del original va al filtro de cerca y al de lejos (06 § El login, al entrar).
// De cerca, dos téxeles negro y blanco estirados a 64 píxeles: con Nearest son dos mitades, y con
// Linear el medio es gris. De lejos, cuatro téxeles en un píxel, en el centro de la textura: con
// Nearest es uno de ellos, y con Linear el promedio de los cuatro. Cuál de los cuatro no se fija:
// el centro cae justo en el borde entre téxeles, y en una GPU de verdad la UV interpolada puede
// quedar un poco antes o después (Mesa da el (1, 1)). Sin mipmaps: con el filtro de lejos que GL
// trae por defecto, la textura no estaría completa y saldría negra.
TEST_F(GlTextureTest, NearestAndLinearSampleAsAsked)
{
    const Texture blackWhite = MakeTexture(PixelFormat::Rgb, 2, 1, {0, 0, 0, 255, 255, 255});
    const Texture fourColors = MakeTexture(PixelFormat::Rgb, 2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    const TestQuad onePixel{.right = 1.0f, .top = 1.0f};

    const auto nearest = GlTexture::Upload(Gl(), blackWhite, {Filter::Nearest, Wrap::Clamp});
    ASSERT_TRUE(nearest.HasValue()) << nearest.Error();
    Draw(nearest.Value(), TestQuad{});
    EXPECT_EQ(Cells(2, 1), (CellColors{Black, White}));

    const auto linear = GlTexture::Upload(Gl(), blackWhite, {Filter::Linear, Wrap::Clamp});
    ASSERT_TRUE(linear.HasValue()) << linear.Error();
    Draw(linear.Value(), TestQuad{});
    const std::vector<Rgba8> pixels = m_framebuffer->ReadPixels();
    const Rgba8 middle =
        pixels[Client::Tests::PixelIndex(Client::Tests::FramebufferSide / 2, 0, m_framebuffer->Width())];
    EXPECT_GT(middle.red, 32) << "el medio no es negro";
    EXPECT_LT(middle.red, 223) << "el medio no es blanco";

    const auto farNearest = GlTexture::Upload(Gl(), fourColors, {Filter::Nearest, Wrap::Clamp});
    ASSERT_TRUE(farNearest.HasValue()) << farNearest.Error();
    Draw(farNearest.Value(), onePixel);
    const Rgba8 nearestTexel = m_framebuffer->ReadPixels().front();
    EXPECT_TRUE(nearestTexel == Red || nearestTexel == Green || nearestTexel == Blue || nearestTexel == White)
        << "con Nearest no es uno de los cuatro téxeles";

    const auto farLinear = GlTexture::Upload(Gl(), fourColors, {Filter::Linear, Wrap::Clamp});
    ASSERT_TRUE(farLinear.HasValue()) << farLinear.Error();
    Draw(farLinear.Value(), onePixel);
    const Rgba8 average = m_framebuffer->ReadPixels().front();
    for (const std::uint8_t channel : {average.red, average.green, average.blue})
    {
        EXPECT_GE(channel, 127);
        EXPECT_LE(channel, 128);
    }
}

// El cuarto argumento, en los dos ejes: con la u (o la v) de 0 a 2, Repeat dibuja la textura dos
// veces y Clamp estira el último téxel. Clamp es GL_CLAMP_TO_EDGE, porque el GL_CLAMP del original
// no existe en el perfil core (Q-C73).
TEST_F(GlTextureTest, RepeatWrapsAndClampDoesNot)
{
    const Texture acrossS = MakeTexture(PixelFormat::Rgb, 2, 1, {255, 0, 0, 0, 0, 255});
    const Texture acrossT = MakeTexture(PixelFormat::Rgb, 1, 2, {255, 0, 0, 0, 0, 255});
    const TestQuad twiceS{.u1 = 2.0f};
    const TestQuad twiceT{.v1 = 2.0f};

    const auto repeatS = GlTexture::Upload(Gl(), acrossS, {Filter::Nearest, Wrap::Repeat});
    ASSERT_TRUE(repeatS.HasValue()) << repeatS.Error();
    Draw(repeatS.Value(), twiceS);
    EXPECT_EQ(Cells(4, 1), (CellColors{Red, Blue, Red, Blue}));

    const auto clampS = GlTexture::Upload(Gl(), acrossS, {Filter::Nearest, Wrap::Clamp});
    ASSERT_TRUE(clampS.HasValue()) << clampS.Error();
    Draw(clampS.Value(), twiceS);
    EXPECT_EQ(Cells(4, 1), (CellColors{Red, Blue, Blue, Blue}));

    const auto repeatT = GlTexture::Upload(Gl(), acrossT, {Filter::Nearest, Wrap::Repeat});
    ASSERT_TRUE(repeatT.HasValue()) << repeatT.Error();
    Draw(repeatT.Value(), twiceT);
    EXPECT_EQ(Cells(1, 4), (CellColors{Red, Blue, Red, Blue}));

    const auto clampT = GlTexture::Upload(Gl(), acrossT, {Filter::Nearest, Wrap::Clamp});
    ASSERT_TRUE(clampT.HasValue()) << clampT.Error();
    Draw(clampT.Value(), twiceT);
    EXPECT_EQ(Cells(1, 4), (CellColors{Red, Blue, Blue, Blue}));
}
} // namespace
