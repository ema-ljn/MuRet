#include "Data/Textures/JpegWriter.h"

#include "Data/Textures/JpegImage.h"
#include "support/FileBytes.h"
#include "support/JpegVectors.h"
#include "support/SameBytes.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <vector>

// WriteJpegFile (0x63b150) contra los vectores de la captura, que escribió la IJG libjpeg 6b con las mismas llamadas
// (Client/tests/vectors/jpeg/README.md, shot6b.c). Los archivos van a una carpeta temporal.
namespace
{
using Client::Data::Textures::JpegWrite;
using Client::Data::Textures::WriteJpegFile;
using Client::Tests::ReadFileBytes;
using Client::Tests::ReadJpegVector;
using Client::Tests::SameBytes;
using Client::Tests::TemporaryFolder;

// La calidad que pide 0x6202e0.
constexpr int ScreenshotQuality = 100;

// La imagen `detailed` de shot6b.c como la deja glReadPixels: la fila de abajo primero. y cuenta desde arriba.
std::vector<std::uint8_t> DetailedBottomUp(int width, int height)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
    for (int y = 0; y < height; ++y)
    {
        const std::size_t line = static_cast<std::size_t>(height - 1 - y) * static_cast<std::size_t>(width) * 3;
        for (int x = 0; x < width; ++x)
        {
            const std::size_t at = line + static_cast<std::size_t>(x) * 3;
            pixels[at] = static_cast<std::uint8_t>((47 * x + 13 * y) & 255);
            pixels[at + 1] = static_cast<std::uint8_t>((x * x + 3 * y * y) & 255);
            pixels[at + 2] = static_cast<std::uint8_t>((7 * x * y + 91) & 255);
        }
    }
    return pixels;
}

// Los bytes de `path`, como std::byte.
std::vector<std::byte> FileBytesOf(const std::filesystem::path& path)
{
    const auto read = ReadFileBytes(path);
    if (!read)
    {
        ADD_FAILURE() << "no se pudo leer " << path.string();
        return {};
    }
    const auto bytes = std::as_bytes(std::span(*read));
    return {bytes.begin(), bytes.end()};
}

// Con la calidad 100, los parámetros por defecto y las filas dadas vuelta, los mismos bytes que la 6b: un tamaño que
// no es múltiplo del bloque de 16 del 4:2:0 y uno que lo es en alto.
TEST(JpegWriterTest, WritesTheBytesOfThe6b)
{
    const TemporaryFolder folder("jpeg-writer");
    struct Case
    {
        int width;
        int height;
        std::string vector;
    };
    for (const Case& shot : {Case{13, 11, "screenshot-13x11.6b.jpg"}, Case{40, 24, "screenshot-40x24.6b.jpg"}})
    {
        SCOPED_TRACE(shot.vector);
        const std::filesystem::path file = folder.Path() / "Screen(01_02-03_04)-0000.jpg";
        const auto written =
            WriteJpegFile(file, shot.width, shot.height, DetailedBottomUp(shot.width, shot.height), ScreenshotQuality);
        ASSERT_TRUE(written.HasValue()) << written.Error();
        EXPECT_EQ(written.Value(), JpegWrite::Written);
        EXPECT_TRUE(SameBytes(FileBytesOf(file), ReadJpegVector(shot.vector)));
    }
}

// 0x63b220..0x63b23a: la fila de arriba del JPEG es la última del buffer. Arriba blanco y abajo negro, en el buffer de
// abajo hacia arriba: primero el negro.
TEST(JpegWriterTest, TheTopRowIsTheLastOfTheBuffer)
{
    const TemporaryFolder folder("jpeg-writer");
    constexpr int Side = 16;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(Side) * Side * 3, 0);
    const std::size_t half = pixels.size() / 2;
    for (std::size_t i = half; i < pixels.size(); ++i)
        pixels[i] = 255;
    const std::filesystem::path file = folder.Path() / "shot.jpg";
    ASSERT_TRUE(WriteJpegFile(file, Side, Side, pixels, ScreenshotQuality).HasValue());

    const auto decoded = Client::Data::Textures::DecodeJpeg(FileBytesOf(file), Side);
    ASSERT_TRUE(decoded.HasValue());
    const std::vector<std::uint8_t>& image = decoded.Value().pixels;
    ASSERT_EQ(image.size(), pixels.size());
    EXPECT_GE(image.front(), 250);
    EXPECT_LE(image.back(), 5);
}

// fopen("wb") vacía el archivo que había: no queda nada del de antes.
TEST(JpegWriterTest, TruncatesTheFileThatWasThere)
{
    const TemporaryFolder folder("jpeg-writer");
    const std::filesystem::path file = folder.Path() / "shot.jpg";
    {
        std::ofstream old(file, std::ios::binary);
        old << std::string(10000, 'x');
    }
    ASSERT_TRUE(WriteJpegFile(file, 13, 11, DetailedBottomUp(13, 11), ScreenshotQuality).HasValue());
    EXPECT_TRUE(SameBytes(FileBytesOf(file), ReadJpegVector("screenshot-13x11.6b.jpg")));
}

// 0x63b197: sin el archivo, 0 sin escribir nada; no es un error.
TEST(JpegWriterTest, AFileThatDoesNotOpenIsNotWritten)
{
    const TemporaryFolder folder("jpeg-writer");
    const std::filesystem::path file = folder.Path() / "no-existe" / "shot.jpg";
    const auto written = WriteJpegFile(file, 13, 11, DetailedBottomUp(13, 11), ScreenshotQuality);
    ASSERT_TRUE(written.HasValue()) << written.Error();
    EXPECT_EQ(written.Value(), JpegWrite::NotOpened);
    EXPECT_FALSE(std::filesystem::exists(file));
}

// Un buffer que no es el de la pantalla es el error, sin abrir el archivo.
TEST(JpegWriterTest, ABufferOfAnotherSizeIsTheError)
{
    const TemporaryFolder folder("jpeg-writer");
    const std::filesystem::path file = folder.Path() / "shot.jpg";
    const auto written = WriteJpegFile(file, 13, 11, DetailedBottomUp(13, 10), ScreenshotQuality);
    ASSERT_FALSE(written.HasValue());
    EXPECT_NE(written.Error().find("0x63b150"), std::string::npos) << written.Error();
    EXPECT_FALSE(std::filesystem::exists(file));
    EXPECT_FALSE(WriteJpegFile(file, 0, 11, {}, ScreenshotQuality).HasValue());
}
} // namespace
