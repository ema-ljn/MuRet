#include "Core/PathText.h"

#include <gtest/gtest.h>

#include <string_view>

// Client::Core::PathFromUtf8 y PathText: rutas leídas y escritas como UTF-8, también en Windows,
// donde path(std::string) y path::string() usan la página ANSI.
namespace
{
using Client::Core::PathFromUtf8;
using Client::Core::PathText;

// Una ruta con letras que no están en la página 1252: "Ł" (U+0141) y "データ" (U+30C7, U+30FC,
// U+30BF). Con path(std::string), en Windows llegarían cambiadas por "?".
constexpr std::string_view NonAnsiUtf8 = "C:/Juegos/\xC5\x81ukasz/\xE3\x83\x87\xE3\x83\xBC\xE3\x82\xBF";

TEST(PathTextTest, PathFromUtf8ReadsTheTextAsUtf8)
{
    const auto path = PathFromUtf8(NonAnsiUtf8);

    ASSERT_TRUE(path.has_value());
    EXPECT_TRUE(path->u8string() == u8"C:/Juegos/\u0141ukasz/\u30C7\u30FC\u30BF");
    EXPECT_EQ(PathText(*path), NonAnsiUtf8);
}

TEST(PathTextTest, AnEmptyTextIsAnEmptyPath)
{
    const auto path = PathFromUtf8("");

    ASSERT_TRUE(path.has_value());
    EXPECT_TRUE(path->empty());
}

// Lo que rechaza MultiByteToWideChar con MB_ERR_INVALID_CHARS, que usa path en Windows.
TEST(PathTextTest, PathFromUtf8RejectsTextThatIsNotUtf8)
{
    // Una Ú de la página 1252, sola: un ini o un nombre guardado como ANSI.
    EXPECT_FALSE(PathFromUtf8("C:/MU/\xDA").has_value());
    // Una continuación sin su primer byte, y una secuencia cortada.
    EXPECT_FALSE(PathFromUtf8("a\x80").has_value());
    EXPECT_FALSE(PathFromUtf8("\xE2\x82").has_value());
    // La "/" en dos y en tres bytes, más largos de lo necesario.
    EXPECT_FALSE(PathFromUtf8("\xC0\xAF").has_value());
    EXPECT_FALSE(PathFromUtf8("\xE0\x80\xAF").has_value());
    // U+D800, la mitad de un par de UTF-16, y U+110000, más allá de U+10FFFF.
    EXPECT_FALSE(PathFromUtf8("\xED\xA0\x80").has_value());
    EXPECT_FALSE(PathFromUtf8("\xF4\x90\x80\x80").has_value());
}

// Los bordes que sí son UTF-8: el último de cada largo, el primero del siguiente, y los dos lados
// de las mitades de UTF-16.
TEST(PathTextTest, PathFromUtf8AcceptsTheLimits)
{
    for (const std::string_view text : {"\x7F", "\xC2\x80", "\xDF\xBF", "\xE0\xA0\x80", "\xED\x9F\xBF", "\xEE\x80\x80",
                                        "\xEF\xBF\xBF", "\xF0\x90\x80\x80", "\xF4\x8F\xBF\xBF"})
    {
        EXPECT_TRUE(PathFromUtf8(text).has_value()) << "largo " << text.size();
    }
}
} // namespace
