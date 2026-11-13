#include "Render/Gl/GlScreenReader.h"

#include "App/WindowState.h"
#include "Platform/GlLoader.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// Client::Render::Gl::GlScreenReader: el glReadPixels de la captura (0x62031f), con un driver falso sin contexto,
// como el de test_gl_texture.cpp. El driver escribe las filas con el GL_PACK_ALIGNMENT que tiene al leer, como GL, y
// anota cuántos bytes pidió.
namespace
{
using Client::App::PixelSize;
using Client::Render::Gl::GlScreenReader;

// El GL_PACK_ALIGNMENT con el que GL arranca.
constexpr GLint GlPackAlignment = 4;

struct ReadCall
{
    GLint x = 0;
    GLint y = 0;
    GLsizei width = 0;
    GLsizei height = 0;
    GLenum format = 0;
    GLenum type = 0;
    // El GL_PACK_ALIGNMENT de ese momento y los bytes que GL escribió con él: las filas menos la última con su
    // relleno, y la última sin él.
    GLint alignment = 0;
    std::size_t written = 0;
};

// Estado global porque la tabla es de punteros a función, sin captura; el fixture lo deja limpio antes de cada test.
struct FakeReadDriver
{
    GLint packAlignment = GlPackAlignment;
    std::vector<ReadCall> reads;
};

FakeReadDriver s_driver;

void GLAPIENTRY FakeGetIntegerv(GLenum name, GLint* value)
{
    if (name == GL_PACK_ALIGNMENT)
        *value = s_driver.packAlignment;
}

void GLAPIENTRY FakePixelStorei(GLenum name, GLint value)
{
    if (name == GL_PACK_ALIGNMENT)
        s_driver.packAlignment = value;
}

// Cada fila r, con su byte i, vale r + i: lo que lee el test tiene que ser eso, fila tras fila y sin relleno.
void GLAPIENTRY FakeReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                               GLvoid* pixels)
{
    const auto rowBytes = static_cast<std::size_t>(width) * 3;
    const auto alignment = static_cast<std::size_t>(s_driver.packAlignment);
    const std::size_t stride = (rowBytes + alignment - 1) / alignment * alignment;
    const auto rows = static_cast<std::size_t>(height);
    auto* bytes = static_cast<std::uint8_t*>(pixels);
    for (std::size_t row = 0; row < rows; ++row)
    {
        for (std::size_t i = 0; i < rowBytes; ++i)
            bytes[row * stride + i] = static_cast<std::uint8_t>(row + i);
    }
    s_driver.reads.push_back(ReadCall{.x = x,
                                      .y = y,
                                      .width = width,
                                      .height = height,
                                      .format = format,
                                      .type = type,
                                      .alignment = s_driver.packAlignment,
                                      .written = rows == 0 ? 0 : (rows - 1) * stride + rowBytes});
}

class GlScreenReaderTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        s_driver = FakeReadDriver{};
        m_gl.getIntegerv = &FakeGetIntegerv;
        m_gl.pixelStorei = &FakePixelStorei;
        m_gl.readPixels = &FakeReadPixels;
    }

    Client::Platform::GlFunctions m_gl{};
};

// 13 × 11: una fila de 39 bytes, que con el 4 de GL llevaría relleno. La lectura es de toda la pantalla desde (0, 0),
// en GL_RGB y GL_UNSIGNED_BYTE, con el alineado en 1: los 13 × 11 × 3 bytes, sin pasar el final (Q-C631).
TEST_F(GlScreenReaderTest, ReadsTheWholeScreenAsRgbRowsWithoutPadding)
{
    GlScreenReader reader(m_gl);
    const std::vector<std::uint8_t> pixels = reader.ReadTheScreen(PixelSize{.width = 13, .height = 11});
    ASSERT_EQ(s_driver.reads.size(), 1u);
    const ReadCall& read = s_driver.reads[0];
    EXPECT_EQ(read.x, 0);
    EXPECT_EQ(read.y, 0);
    EXPECT_EQ(read.width, 13);
    EXPECT_EQ(read.height, 11);
    EXPECT_EQ(read.format, static_cast<GLenum>(GL_RGB));
    EXPECT_EQ(read.type, static_cast<GLenum>(GL_UNSIGNED_BYTE));
    EXPECT_EQ(read.alignment, 1);
    ASSERT_EQ(pixels.size(), std::size_t{13 * 11 * 3});
    EXPECT_EQ(read.written, pixels.size());
    for (std::size_t row = 0; row < 11; ++row)
    {
        for (std::size_t i = 0; i < 39; ++i)
            ASSERT_EQ(pixels[row * 39 + i], static_cast<std::uint8_t>(row + i)) << "fila " << row << ", byte " << i;
    }
}

// Lo que suba o lea otro después no cambia: el alineado vuelve al que estaba.
TEST_F(GlScreenReaderTest, LeavesThePackAlignmentAsItWas)
{
    GlScreenReader reader(m_gl);
    static_cast<void>(reader.ReadTheScreen(PixelSize{.width = 640, .height = 480}));
    EXPECT_EQ(s_driver.packAlignment, GlPackAlignment);
    s_driver.packAlignment = 8;
    static_cast<void>(reader.ReadTheScreen(PixelSize{.width = 640, .height = 480}));
    EXPECT_EQ(s_driver.packAlignment, 8);
    ASSERT_EQ(s_driver.reads.size(), 2u);
    EXPECT_EQ(s_driver.reads[1].alignment, 1);
}

// Sin área, nada que leer.
TEST_F(GlScreenReaderTest, AnEmptyScreenReadsNothing)
{
    GlScreenReader reader(m_gl);
    EXPECT_TRUE(reader.ReadTheScreen(PixelSize{.width = 0, .height = 480}).empty());
    EXPECT_TRUE(s_driver.reads.empty());
}
} // namespace
