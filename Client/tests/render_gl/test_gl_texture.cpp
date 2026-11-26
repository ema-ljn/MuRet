#include "Render/Gl/GlTexture.h"

#include "Core/Expected.h"
#include "Data/Textures/OztTexture.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlFramebuffer.h"
#include "support/GlDrawTest.h"
#include "support/GlTestContext.h"
#include "support/TextureFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// Uploaded like the original (0x63bd00 and 0x63b870). Each test draws it with GlRenderer and reads the texels by cell:
// with GL_NEAREST and the rectangle covering the framebuffer, each cell is a texel.
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

// A hand-built texture: `pixels` is the whole buffer, textureWidth × textureHeight.
Texture MakeTexture(PixelFormat format, int width, int height, std::vector<std::uint8_t> pixels)
{
    return Texture{.width = width,
                   .height = height,
                   .textureWidth = Client::Data::Textures::TextureSide(width),
                   .textureHeight = Client::Data::Textures::TextureSide(height),
                   .format = format,
                   .pixels = std::move(pixels)};
}

// Texel (x, y) of `texture`'s buffer, y = 0 being the first row glTexImage2D uploads. An RGB one reads with alpha 255,
// as GL reads a texture without alpha.
Rgba8 TexelOf(const Texture& texture, int x, int y)
{
    const std::size_t bytes = Client::Data::Textures::BytesPerPixel(texture.format);
    const std::size_t index =
        ((static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.textureWidth)) + static_cast<std::size_t>(x)) *
        bytes;
    const std::uint8_t alpha = texture.format == PixelFormat::Rgba ? texture.pixels[index + 3] : std::uint8_t{255};
    return Rgba8{texture.pixels[index], texture.pixels[index + 1], texture.pixels[index + 2], alpha};
}

// If the pixels are not the whole buffer, Upload does not call GL, which would read past the vector; so an empty table
// without a context is enough.
TEST(GlTextureRulesTest, RefusesPixelsThatAreNotTheWholeBuffer)
{
    const Client::Platform::Sdl::GlFunctions noGl{};
    const Texture shortRgb = MakeTexture(PixelFormat::Rgb, 2, 2, std::vector<std::uint8_t>(11));
    const Client::Core::Expected<GlTexture, std::string> refused = GlTexture::Upload(noGl, shortRgb, NearestClamp);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_NE(refused.Error().find("12"), std::string::npos) << refused.Error();
    EXPECT_NE(refused.Error().find("11"), std::string::npos) << refused.Error();

    Texture empty = MakeTexture(PixelFormat::Rgba, 1, 1, {});
    empty.textureWidth = 0;
    EXPECT_FALSE(GlTexture::Upload(noGl, empty, NearestClamp).HasValue());
}

// The fake driver's name, its starting GL_UNPACK_ALIGNMENT (GL's 4) and how many errors the never-clearing one gives:
// then it gives GL_NO_ERROR, so an uncapped Upload ends and the test fails instead of hanging.
constexpr GLuint FakeTextureName = 7;
constexpr GLint FakeUnpackAlignment = 4;
constexpr int EndlessDriverCalls = 1000;

// A fake Upload driver without a context, like test_gl_loader.cpp's. It has several error flags and glGetError returns
// one per call: the glGetError reference says to call it until GL_NO_ERROR. Mesa has a single flag, so the test
// context cannot show this. glTexImage2D adds its errors to the pending ones, and the first glTexParameteri its own.
// Global because the table holds captureless function pointers; the fixture clears it before each test.
struct FakeErrorDriver
{
    std::deque<GLenum> pending;
    std::vector<GLenum> texImageErrors;
    std::vector<GLenum> texParameterErrors;
    bool neverClears = false;
    int getErrorCalls = 0;
    GLint unpackAlignment = FakeUnpackAlignment;
    std::vector<GLuint> generated;
    std::vector<GLuint> deleted;
};

FakeErrorDriver s_driver;

GLenum GLAPIENTRY FakeGetError()
{
    ++s_driver.getErrorCalls;
    if (s_driver.neverClears && s_driver.getErrorCalls <= EndlessDriverCalls)
    {
        return GL_OUT_OF_MEMORY;
    }
    if (s_driver.pending.empty())
    {
        return GL_NO_ERROR;
    }
    const GLenum error = s_driver.pending.front();
    s_driver.pending.pop_front();
    return error;
}

void GLAPIENTRY FakeGenTextures(GLsizei count, GLuint* names)
{
    for (GLsizei k = 0; k < count; ++k)
    {
        names[k] = FakeTextureName;
        s_driver.generated.push_back(FakeTextureName);
    }
}

void GLAPIENTRY FakeDeleteTextures(GLsizei count, const GLuint* names)
{
    s_driver.deleted.insert(s_driver.deleted.end(), names, names + count);
}

void GLAPIENTRY FakeBindTexture(GLenum /*target*/, GLuint /*name*/) {}

void GLAPIENTRY FakeGetIntegerv(GLenum name, GLint* value)
{
    if (name == GL_UNPACK_ALIGNMENT)
    {
        *value = s_driver.unpackAlignment;
    }
}

void GLAPIENTRY FakePixelStorei(GLenum name, GLint value)
{
    if (name == GL_UNPACK_ALIGNMENT)
    {
        s_driver.unpackAlignment = value;
    }
}

void GLAPIENTRY FakeTexImage2D(GLenum /*target*/, GLint /*level*/, GLint /*internalFormat*/, GLsizei /*width*/,
                               GLsizei /*height*/, GLint /*border*/, GLenum /*format*/, GLenum /*type*/,
                               const GLvoid* /*pixels*/)
{
    s_driver.pending.insert(s_driver.pending.end(), s_driver.texImageErrors.begin(), s_driver.texImageErrors.end());
}

void GLAPIENTRY FakeTexParameteri(GLenum /*target*/, GLenum /*name*/, GLint /*value*/)
{
    s_driver.pending.insert(s_driver.pending.end(), s_driver.texParameterErrors.begin(),
                            s_driver.texParameterErrors.end());
    s_driver.texParameterErrors.clear();
}

class GlTextureDriverTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        s_driver = FakeErrorDriver{};
    }

    // Only what Upload calls. It outlives the texture it gives.
    static Client::Platform::Sdl::GlFunctions FakeGl()
    {
        Client::Platform::Sdl::GlFunctions gl{};
        gl.getError = &FakeGetError;
        gl.genTextures = &FakeGenTextures;
        gl.deleteTextures = &FakeDeleteTextures;
        gl.bindTexture = &FakeBindTexture;
        gl.getIntegerv = &FakeGetIntegerv;
        gl.pixelStorei = &FakePixelStorei;
        gl.texImage2D = &FakeTexImage2D;
        gl.texParameteri = &FakeTexParameteri;
        return gl;
    }

    static Texture OneTexel()
    {
        return MakeTexture(PixelFormat::Rgba, 1, 1, {255, 0, 0, 255});
    }
};

// Errors pending before Upload belong to another call.
TEST_F(GlTextureDriverTest, TakesEveryPendingError)
{
    s_driver.pending = {GL_INVALID_ENUM, GL_INVALID_OPERATION};
    const Client::Platform::Sdl::GlFunctions gl = FakeGl();

    const Client::Core::Expected<GlTexture, std::string> refused = GlTexture::Upload(gl, OneTexel(), NearestClamp);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_NE(refused.Error().find("pendiente"), std::string::npos) << refused.Error();
    EXPECT_NE(refused.Error().find("GL_INVALID_ENUM"), std::string::npos) << refused.Error();
    EXPECT_TRUE(s_driver.pending.empty());
    EXPECT_TRUE(s_driver.generated.empty());
}

// Errors from glTexImage2D belong to the upload, not to another call.
TEST_F(GlTextureDriverTest, TakesEveryErrorOfTheUpload)
{
    s_driver.texImageErrors = {GL_OUT_OF_MEMORY, GL_INVALID_VALUE};
    const Client::Platform::Sdl::GlFunctions gl = FakeGl();

    const Client::Core::Expected<GlTexture, std::string> refused = GlTexture::Upload(gl, OneTexel(), NearestClamp);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_NE(refused.Error().find("GL_OUT_OF_MEMORY"), std::string::npos) << refused.Error();
    EXPECT_EQ(refused.Error().find("pendiente"), std::string::npos) << refused.Error();
    EXPECT_TRUE(s_driver.pending.empty());
    EXPECT_EQ(s_driver.generated, std::vector<GLuint>{FakeTextureName});
    EXPECT_EQ(s_driver.deleted, s_driver.generated);
    EXPECT_EQ(s_driver.unpackAlignment, FakeUnpackAlignment);
}

// glTexParameteri is part of the upload too. With these arguments it can only give GL_OUT_OF_MEMORY, which GL allows on
// any call: Upload neither returns the texture nor leaves the error pending, because the next call would be blamed.
TEST_F(GlTextureDriverTest, ATexParameterErrorIsTheUploads)
{
    s_driver.texParameterErrors = {GL_OUT_OF_MEMORY};
    const Client::Platform::Sdl::GlFunctions gl = FakeGl();

    const Client::Core::Expected<GlTexture, std::string> refused = GlTexture::Upload(gl, OneTexel(), NearestClamp);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_NE(refused.Error().find("GL_OUT_OF_MEMORY"), std::string::npos) << refused.Error();
    EXPECT_EQ(refused.Error().find("pendiente"), std::string::npos) << refused.Error();
    EXPECT_TRUE(s_driver.pending.empty());
    EXPECT_EQ(s_driver.deleted, s_driver.generated);
}

TEST_F(GlTextureDriverTest, StopsAskingADriverThatNeverClears)
{
    s_driver.neverClears = true;
    const Client::Platform::Sdl::GlFunctions gl = FakeGl();

    EXPECT_FALSE(GlTexture::Upload(gl, OneTexel(), NearestClamp).HasValue());
    EXPECT_LT(s_driver.getErrorCalls, EndlessDriverCalls);
}

// The synthetic 2 × 2 OZT, read as Data does (rows flipped, RGBA): the four texels come out as in the buffer, with
// their alpha, and row 0 at the top, where the original's quad puts it.
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

// 1- and 2-pixel RGB rows are 3 and 6 bytes, and with GL's default GL_UNPACK_ALIGNMENT of 4 the second would start at
// byte 4 or 8: Upload sets it to 1 so rows do not shift. Rows 4 or more wide are already a multiple of 4: the 3 × 2 in
// its 4 × 2 buffer also shows the non-image column, at zero.
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

// Whatever uploads later (the text) must not inherit the 1.
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

// What the driver rejects is not a texture (AUD-C03): a side over GL_MAX_TEXTURE_SIZE gives GL_INVALID_VALUE in
// glTexImage2D. The new name was bound when Upload deleted it, so GL_TEXTURE_2D ends at 0.
TEST_F(GlTextureTest, RejectsADriverFailure)
{
    constexpr GLint Before = 2;
    Gl().pixelStorei(GL_UNPACK_ALIGNMENT, Before);
    const Texture tooWide = Client::Tests::TextureOverTheGlLimit(Gl());

    const Client::Core::Expected<GlTexture, std::string> refused = GlTexture::Upload(Gl(), tooWide, NearestClamp);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_NE(refused.Error().find("GL_INVALID_VALUE"), std::string::npos) << refused.Error();
    GLint after = 0;
    Gl().getIntegerv(GL_UNPACK_ALIGNMENT, &after);
    EXPECT_EQ(after, Before);
    GLint bound = -1;
    Gl().getIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    EXPECT_EQ(bound, 0);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

// An error GL already had is not this upload's: glGetError does not say which call left it. An alignment of 3 (GL
// accepts 1, 2, 4 and 8) leaves GL_INVALID_VALUE pending.
TEST_F(GlTextureTest, ReportsAPreexistingGlError)
{
    const Texture texture = MakeTexture(PixelFormat::Rgba, 1, 1, {255, 0, 0, 255});
    Gl().pixelStorei(GL_UNPACK_ALIGNMENT, 3);

    const Client::Core::Expected<GlTexture, std::string> refused = GlTexture::Upload(Gl(), texture, NearestClamp);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_NE(refused.Error().find("pendiente"), std::string::npos) << refused.Error();
    EXPECT_NE(refused.Error().find("GL_INVALID_VALUE"), std::string::npos) << refused.Error();
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
    EXPECT_TRUE(GlTexture::Upload(Gl(), texture, NearestClamp).HasValue());
}

// The original's third argument goes to both mag and min filters (06 § El login, al entrar). Mag: black and white
// texels stretched to 64 pixels give two halves with Nearest and gray in the middle with Linear. Min: four texels in
// one pixel at the texture's center give one of them with Nearest and their average with Linear. Which one is not
// fixed: the center falls right on the texel border, and on a real GPU the interpolated UV can land slightly before or
// after (Mesa gives (1, 1)). No mipmaps: with GL's default min filter the texture would be incomplete and black.
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

// The fourth argument, on both axes: with u (or v) from 0 to 2, Repeat draws the texture twice and Clamp stretches the
// last texel. Clamp is GL_CLAMP_TO_EDGE, because the original's GL_CLAMP does not exist in the core profile (Q-C73).
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
