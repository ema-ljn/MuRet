#include "Render/Text/FreeTypeRasterizer.h"

#include "Core/PathText.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextStyle.h"

#include "support/GoldenDir.h"
#include "support/TemporaryFolder.h"
#include "support/TinyTrueTypeFont.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Render::Text::FreeTypeRasterizer: las fuentes de WinMain con FreeType en un bit
// (pregunta 7 del plan de la CL5). Los tests con fuente leen MURET_CLIENT_FONT_FILE (en CI, una
// fuente del runner) y se saltean sin la variable; no se comparan con GDI, sino consigo mismos
// (Q-C76). Los de abrir un archivo que no es una fuente corren siempre.
namespace
{
using Client::Render::Text::FontHeightFor;
using Client::Render::Text::FontKind;
using Client::Render::Text::FreeTypeRasterizer;
using Client::Render::Text::PaintTextBox;
using Client::Render::Text::TextBoxBytes;
using Client::Render::Text::TextBoxPixelCount;
using Client::Render::Text::TextBoxWidth;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextStyle;

constexpr std::array AllFonts = {FontKind::Normal, FontKind::Bold, FontKind::BigBold, FontKind::OtherFace12};

std::vector<std::uint8_t> InkOf(const FreeTypeRasterizer& rasterizer, FontKind font, std::string_view text)
{
    std::vector<std::uint8_t> ink(TextBoxPixelCount, 0);
    rasterizer.DrawInk(font, text, 0, std::span<std::uint8_t, TextBoxPixelCount>(ink));
    return ink;
}

std::size_t InkCount(const std::vector<std::uint8_t>& ink)
{
    return static_cast<std::size_t>(std::ranges::count(ink, std::uint8_t{0xff}));
}

class FreeTypeRasterizerTest : public Client::Tests::FontFileTest
{
protected:
    std::unique_ptr<FreeTypeRasterizer> OpenFont(int screenWidth,
                                                 const std::optional<std::filesystem::path>& bold = std::nullopt)
    {
        auto opened = FreeTypeRasterizer::Open(m_fontFile, bold, screenWidth);
        if (!opened.HasValue())
        {
            ADD_FAILURE() << opened.Error();
            return nullptr;
        }
        return std::move(opened).Value();
    }
};

// Sin suavizado: cada píxel de la tinta es 0 o 0xff, y en la caja cada píxel es del color del texto
// o del fondo. Con FT_RENDER_MODE_NORMAL habría grises.
TEST_F(FreeTypeRasterizerTest, RendersWithoutAntialiasing)
{
    const auto rasterizer = OpenFont(1024);
    ASSERT_NE(rasterizer, nullptr);

    for (const FontKind font : AllFonts)
    {
        const auto ink = InkOf(*rasterizer, font, "Agua sin sal, 0123");
        EXPECT_GT(InkCount(ink), 0u);
        EXPECT_TRUE(std::ranges::all_of(ink, [](std::uint8_t value) { return value == 0 || value == 0xff; }));
    }

    const TextStyle style{FontKind::Normal, TextColors{0xff0000ff, 0x80000000}};
    const auto extent = rasterizer->Measure(style.font, "Agua sin sal");
    std::vector<std::uint8_t> box(TextBoxBytes, 0);
    PaintTextBox(*rasterizer, style, "Agua sin sal", 0, extent.width, extent.height,
                 std::span<std::uint8_t, TextBoxBytes>(box));
    std::size_t text = 0;
    for (int row = 0; row < extent.height; ++row)
    {
        for (int column = 0; column < extent.width; ++column)
        {
            const auto offset = static_cast<std::size_t>((row * TextBoxWidth + column) * 4);
            const std::array<std::uint8_t, 4> pixel{box[offset], box[offset + 1], box[offset + 2], box[offset + 3]};
            const bool isText = pixel == std::array<std::uint8_t, 4>{0xff, 0, 0, 0xff};
            const bool isBackground = pixel == std::array<std::uint8_t, 4>{0, 0, 0, 0x80};
            EXPECT_TRUE(isText || isBackground) << column << ", " << row;
            text += isText ? 1 : 0;
        }
    }
    EXPECT_GT(text, 0u);
}

// Como GetTextExtentPointA, sin kerning: el ancho de un texto es la suma de los avances.
TEST_F(FreeTypeRasterizerTest, MeasuresTheAdvance)
{
    const auto rasterizer = OpenFont(640);
    ASSERT_NE(rasterizer, nullptr);

    const int a = rasterizer->Measure(FontKind::Normal, "a").width;
    const int b = rasterizer->Measure(FontKind::Normal, "b").width;
    const int v = rasterizer->Measure(FontKind::Normal, "V").width;
    EXPECT_GT(a, 0);
    EXPECT_EQ(rasterizer->Measure(FontKind::Normal, "ab").width, a + b);
    EXPECT_EQ(rasterizer->Measure(FontKind::Normal, "aaa").width, 3 * a);
    EXPECT_EQ(rasterizer->Measure(FontKind::Normal, "AV").width, rasterizer->Measure(FontKind::Normal, "A").width + v);
    EXPECT_EQ(rasterizer->Measure(FontKind::Normal, "").width, 0);
}

// El alto es el de la celda que pide CreateFontA, para cada fuente y cada ancho, también sin texto.
TEST_F(FreeTypeRasterizerTest, TheCellHeightIsTheFontHeight)
{
    for (const int width : {640, 800, 1024, 1280})
    {
        const auto rasterizer = OpenFont(width);
        ASSERT_NE(rasterizer, nullptr);
        for (const FontKind font : AllFonts)
        {
            EXPECT_EQ(rasterizer->Measure(font, "Ag").height, FontHeightFor(font, width)) << width;
            EXPECT_EQ(rasterizer->Measure(font, "").height, FontHeightFor(font, width)) << width;
        }
    }
}

// El alto es el de la celda (el ascenso más el descenso de Windows), no el del em, que es lo que
// pediría FT_Set_Pixel_Sizes: el em queda más chico. En DejaVu Sans es 2.048 / 2.384 de la celda,
// y en Arial, 2.048 / 2.288.
TEST_F(FreeTypeRasterizerTest, TheEmIsSmallerThanTheCell)
{
    for (const int width : {640, 1280})
    {
        const auto rasterizer = OpenFont(width);
        ASSERT_NE(rasterizer, nullptr);
        for (const FontKind font : AllFonts)
        {
            const int height = FontHeightFor(font, width);
            EXPECT_LT(rasterizer->PixelsPerEm(font), height) << width;
            EXPECT_GE(rasterizer->PixelsPerEm(font) * 10, height * 7) << width;
        }
    }
}

// Las mayúsculas caen dentro de la celda, con la celda arriba (TA_TOP): las filas de la tinta van
// de 0 al alto de la celda.
TEST_F(FreeTypeRasterizerTest, TheLettersAreInsideTheCell)
{
    const auto rasterizer = OpenFont(1280);
    ASSERT_NE(rasterizer, nullptr);

    for (const FontKind font : AllFonts)
    {
        const auto ink = InkOf(*rasterizer, font, "EHMT");
        const int height = FontHeightFor(font, 1280);
        int lastRow = -1;
        for (std::size_t i = 0; i < ink.size(); ++i)
        {
            if (ink[i] == 0xff)
            {
                lastRow = std::max(lastRow, static_cast<int>(i) / TextBoxWidth);
            }
        }
        EXPECT_GE(lastRow, height / 2);
        EXPECT_LT(lastRow, height);
    }
}

// FT_PIXEL_MODE_MONO: el píxel de la izquierda de cada byte es su bit alto, y una fila empieza
// `pitch` bytes después de la anterior. La "L" lo muestra: la fila de arriba es el palo, del lado
// izquierdo de la letra, y la de abajo, el pie, que llega más a la derecha. Con los bits leídos al
// revés, o con otro paso entre filas, la letra sale espejada o corrida.
TEST_F(FreeTypeRasterizerTest, TheLeftPixelOfAByteIsItsHighBit)
{
    for (const int width : {640, 1024, 1280})
    {
        const auto rasterizer = OpenFont(width);
        ASSERT_NE(rasterizer, nullptr);
        const auto ink = InkOf(*rasterizer, FontKind::Normal, "L");

        // La primera y la última columna con tinta de cada fila.
        std::vector<std::pair<int, int>> rows;
        int left = TextBoxWidth;
        for (int row = 0; row < 32; ++row)
        {
            int first = -1;
            int last = -1;
            for (int column = 0; column < TextBoxWidth; ++column)
            {
                if (ink[static_cast<std::size_t>(row * TextBoxWidth + column)] == 0xff)
                {
                    first = first < 0 ? column : first;
                    last = column;
                }
            }
            if (first >= 0)
            {
                rows.emplace_back(first, last);
                left = std::min(left, first);
            }
        }
        ASSERT_GE(rows.size(), 3u) << width;
        const auto [topFirst, topLast] = rows.front();
        const auto [bottomFirst, bottomLast] = rows.back();
        EXPECT_EQ(topFirst, left) << width;
        EXPECT_EQ(bottomFirst, left) << width;
        EXPECT_GT(bottomLast, topLast) << width;
    }
}

// Una caja de un texto ancho dibuja desde su columna: la segunda caja de un texto de 300 columnas
// es la tinta de la primera corrida 256 columnas.
TEST_F(FreeTypeRasterizerTest, DrawsFromTheFirstColumn)
{
    const auto rasterizer = OpenFont(640);
    ASSERT_NE(rasterizer, nullptr);
    const std::string text(64, 'M');
    ASSERT_GT(rasterizer->Measure(FontKind::Normal, text).width, 260);

    std::vector<std::uint8_t> whole(TextBoxPixelCount, 0);
    std::vector<std::uint8_t> shifted(TextBoxPixelCount, 0);
    rasterizer->DrawInk(FontKind::Normal, text, 0, std::span<std::uint8_t, TextBoxPixelCount>(whole));
    rasterizer->DrawInk(FontKind::Normal, text, 200, std::span<std::uint8_t, TextBoxPixelCount>(shifted));

    for (int row = 0; row < 32; ++row)
    {
        for (int column = 0; column < 56; ++column)
        {
            EXPECT_EQ(shifted[static_cast<std::size_t>(row * TextBoxWidth + column)],
                      whole[static_cast<std::size_t>(row * TextBoxWidth + column + 200)])
                << column << ", " << row;
        }
    }
}

// Sin BoldFontFile la negrita es la normal ensanchada: un píxel más por letra, y más tinta.
TEST_F(FreeTypeRasterizerTest, WithoutABoldFileTheBoldIsOnePixelWiderPerLetter)
{
    const auto rasterizer = OpenFont(640);
    ASSERT_NE(rasterizer, nullptr);

    EXPECT_EQ(rasterizer->Measure(FontKind::Bold, "abc").width, rasterizer->Measure(FontKind::Normal, "abc").width + 3);
    EXPECT_GT(InkCount(InkOf(*rasterizer, FontKind::Bold, "abc")),
              InkCount(InkOf(*rasterizer, FontKind::Normal, "abc")));
}

// Con BoldFontFile la negrita es esa cara, sin ensanchar: con la misma fuente en las dos claves, la
// negrita mide y dibuja como la normal.
TEST_F(FreeTypeRasterizerTest, WithABoldFileThereIsNoSyntheticBold)
{
    const auto rasterizer = OpenFont(640, m_fontFile);
    ASSERT_NE(rasterizer, nullptr);

    EXPECT_EQ(rasterizer->Measure(FontKind::Bold, "abc"), rasterizer->Measure(FontKind::Normal, "abc"));
    EXPECT_EQ(InkOf(*rasterizer, FontKind::Bold, "abc"), InkOf(*rasterizer, FontKind::Normal, "abc"));
}

// Cada byte es el carácter de Latin-1 de su número (Q-C67): 0xE9 es la e con tilde, que en estas
// fuentes avanza como la e y se dibuja distinto.
TEST_F(FreeTypeRasterizerTest, ReadsEachByteAsLatin1)
{
    const auto rasterizer = OpenFont(1024);
    ASSERT_NE(rasterizer, nullptr);

    EXPECT_EQ(rasterizer->Measure(FontKind::Normal, "\xE9"), rasterizer->Measure(FontKind::Normal, "e"));
    EXPECT_NE(InkOf(*rasterizer, FontKind::Normal, "\xE9"), InkOf(*rasterizer, FontKind::Normal, "e"));
    EXPECT_GT(InkCount(InkOf(*rasterizer, FontKind::Normal, "\xE9")),
              InkCount(InkOf(*rasterizer, FontKind::Normal, "e")));
}

// El intérprete v35, el de Windows 98, y no el v40 que FreeType usa por defecto.
TEST_F(FreeTypeRasterizerTest, UsesTheInterpreterOfWindows98)
{
    const auto rasterizer = OpenFont(640);
    ASSERT_NE(rasterizer, nullptr);

    EXPECT_EQ(rasterizer->InterpreterVersion(), 35u);
}

TEST_F(FreeTypeRasterizerTest, AMissingBoldFileSaysItsPath)
{
    const Client::Tests::TemporaryFolder folder("freetype");
    const std::filesystem::path missing = folder.Path() / "negrita.ttf";

    const auto opened = FreeTypeRasterizer::Open(m_fontFile, missing, 640);

    ASSERT_FALSE(opened.HasValue());
    EXPECT_NE(opened.Error().find(Client::Core::PathText(missing)), std::string::npos) << opened.Error();
}

// Sin fuente: el error nombra el archivo, falte o no sea una fuente.
TEST(FreeTypeRasterizerOpenTest, AMissingFileSaysItsPath)
{
    const Client::Tests::TemporaryFolder folder("freetype");
    const std::filesystem::path missing = folder.Path() / "no-esta.ttf";

    const auto opened = FreeTypeRasterizer::Open(missing, std::nullopt, 640);

    ASSERT_FALSE(opened.HasValue());
    EXPECT_NE(opened.Error().find(Client::Core::PathText(missing)), std::string::npos) << opened.Error();
}

TEST(FreeTypeRasterizerOpenTest, AFileThatIsNotAFontSaysItsPath)
{
    const Client::Tests::TemporaryFolder folder("freetype");
    const std::filesystem::path notAFont = folder.Path() / "letra.ttf";
    {
        std::ofstream file(notAFont, std::ios::binary);
        file << "esto no es una fuente";
    }

    const auto opened = FreeTypeRasterizer::Open(notAFont, std::nullopt, 640);

    ASSERT_FALSE(opened.HasValue());
    EXPECT_NE(opened.Error().find(Client::Core::PathText(notAFont)), std::string::npos) << opened.Error();
}

// Una letra sin área, como el guion bajo que el hinting deja en una línea en algunas fuentes:
// FT_Outline_EmboldenXY no la puede ensanchar (FT_ORIENTATION_NONE) y devuelve un error, que no
// tiene que hacer fallar Open. La negrita simulada la deja como está, con su píxel de avance. Usa
// la fuente mínima de support/, sin OS/2 (la celda sale de la hhea): corre sin la variable.
TEST(FreeTypeRasterizerOpenTest, ALetterWithoutAreaIsBoldToo)
{
    const Client::Tests::TemporaryFolder folder("freetype");
    const std::filesystem::path font = folder.Path() / "minima.ttf";
    {
        const std::vector<std::uint8_t> bytes = Client::Tests::TinyTrueTypeFont();
        std::ofstream file(font, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    const auto opened = FreeTypeRasterizer::Open(font, std::nullopt, 640);

    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    const FreeTypeRasterizer& rasterizer = *opened.Value();
    const std::string flat(1, Client::Tests::FlatGlyphLetter);
    EXPECT_EQ(rasterizer.Measure(FontKind::Bold, flat).width, rasterizer.Measure(FontKind::Normal, flat).width + 1);
    EXPECT_EQ(rasterizer.Measure(FontKind::Bold, "a").width, rasterizer.Measure(FontKind::Normal, "a").width + 1);
    EXPECT_EQ(rasterizer.Measure(FontKind::Normal, flat).height, FontHeightFor(FontKind::Normal, 640));
    EXPECT_GT(InkCount(InkOf(rasterizer, FontKind::Bold, "a")), InkCount(InkOf(rasterizer, FontKind::Normal, "a")));
}
} // namespace
