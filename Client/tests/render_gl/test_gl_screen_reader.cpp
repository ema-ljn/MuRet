#include "Render/Gl/GlScreenReader.h"

#include "Core/PixelSize.h"
#include "Platform/Sdl/GlLoader.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// The screenshot glReadPixels (0x62031f) with a fake driver and no context. The driver writes the rows with its current
// GL_PACK_ALIGNMENT, like GL, and records how many bytes it was asked for.
namespace
{
using Client::Core::PixelSize;
using Client::Render::Gl::GlScreenReader;

// GL's initial GL_PACK_ALIGNMENT.
constexpr GLint GlPackAlignment = 4;

struct ReadCall
{
    GLint x = 0;
    GLint y = 0;
    GLsizei width = 0;
    GLsizei height = 0;
    GLenum format = 0;
    GLenum type = 0;
    // The GL_PACK_ALIGNMENT at that moment and the bytes GL wrote with it: every row but the last with its padding, the
    // last without.
    GLint alignment = 0;
    std::size_t written = 0;
};

// Global because the table holds captureless function pointers; the fixture clears it before each test.
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

// Row r's byte i is r + i: what the test reads must be that, row after row, without padding.
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

    Client::Platform::Sdl::GlFunctions m_gl{};
};

// 13 × 11: a 39-byte row, which GL's 4 would pad. The read is the whole screen from (0, 0), GL_RGB and
// GL_UNSIGNED_BYTE, with alignment 1: the 13 × 11 × 3 bytes, without overrunning the end (Q-C631).
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

// So whatever uploads or reads later is unaffected: the alignment goes back to what it was.
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

TEST_F(GlScreenReaderTest, AnEmptyScreenReadsNothing)
{
    GlScreenReader reader(m_gl);
    EXPECT_TRUE(reader.ReadTheScreen(PixelSize{.width = 0, .height = 480}).empty());
    EXPECT_TRUE(s_driver.reads.empty());
}
} // namespace
