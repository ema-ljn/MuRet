#include "Platform/SystemFonts.h"

#include <gtest/gtest.h>

// Client::Platform::FindSystemFontFile y la regla de los nombres de la lista de fuentes de Windows
// (HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts; CL5.3, pregunta 8 del plan de la CL5).
// Un valor se llama como sus caras, separadas por " & " en una colección, y su tipo entre
// paréntesis; la negrita es la cara con " Bold". Las caras de estos tests son inventadas.
namespace
{
using Client::Platform::FindSystemFontFile;
using Client::Platform::FontEntryNamesFace;
using Client::Platform::HasSystemFontList;

// La normal es el nombre exacto de la cara, y la negrita, el nombre con " Bold".
TEST(SystemFontsTest, AnEntryNamesItsFace)
{
    EXPECT_TRUE(FontEntryNamesFace("Cara (TrueType)", "Cara", false));
    EXPECT_TRUE(FontEntryNamesFace("Cara Bold (TrueType)", "Cara", true));
    EXPECT_FALSE(FontEntryNamesFace("Cara Bold (TrueType)", "Cara", false));
    EXPECT_FALSE(FontEntryNamesFace("Cara (TrueType)", "Cara", true));
    EXPECT_FALSE(FontEntryNamesFace("Cara Bold Italic (TrueType)", "Cara", true));
}

// Un .ttc nombra todas sus caras.
TEST(SystemFontsTest, ACollectionNamesEachFace)
{
    EXPECT_TRUE(FontEntryNamesFace("Una & Otra & Tercera (TrueType)", "Otra", false));
    EXPECT_TRUE(FontEntryNamesFace("Una & Otra & Tercera (TrueType)", "Tercera", false));
    EXPECT_FALSE(FontEntryNamesFace("Una & Otra & Tercera (TrueType)", "Una & Otra", false));
}

// GDI no mira mayúsculas en el nombre de la cara.
TEST(SystemFontsTest, TheCaseDoesNotMatter)
{
    EXPECT_TRUE(FontEntryNamesFace("CARA (TrueType)", "cara", false));
    EXPECT_TRUE(FontEntryNamesFace("cara bold (OpenType)", "Cara", true));
}

// Un nombre que empieza como la cara es otra cara.
TEST(SystemFontsTest, APrefixIsAnotherFace)
{
    EXPECT_FALSE(FontEntryNamesFace("Cara Light (TrueType)", "Cara", false));
    EXPECT_FALSE(FontEntryNamesFace("Car (TrueType)", "Cara", false));
    EXPECT_FALSE(FontEntryNamesFace("Caras (TrueType)", "Cara", false));
}

// FreeTypeRasterizer pide contornos (FT_LOAD_NO_BITMAP): las fuentes de mapa de bits de la lista
// (las .fon, sin "(TrueType)" ni "(OpenType)") no cuentan.
TEST(SystemFontsTest, OnlyOutlineFontsCount)
{
    EXPECT_FALSE(FontEntryNamesFace("Cara 8,10,12,14 (All res)", "Cara 8,10,12,14", false));
    EXPECT_FALSE(FontEntryNamesFace("Cara", "Cara", false));
}

// Una cara vacía (el texto 0 de un Text.bmd sin cara) no se encuentra nunca.
TEST(SystemFontsTest, AnEmptyFaceIsNeverFound)
{
    EXPECT_FALSE(FontEntryNamesFace(" (TrueType)", "", false));
    EXPECT_FALSE(FindSystemFontFile("", false).has_value());
}

#ifndef _WIN32
// Fuera de Windows no hay lista: main.cpp no le pasa una búsqueda a App::ChooseFontFiles, y sin
// [Video] FontFile su error dice que el cliente no la busca.
TEST(SystemFontsTest, OutsideWindowsThereIsNoList)
{
    EXPECT_FALSE(HasSystemFontList());
    EXPECT_FALSE(FindSystemFontFile("Cara", false).has_value());
    EXPECT_FALSE(FindSystemFontFile("Cara", true).has_value());
}
#else
// En Windows, la lista del registro: main.cpp le pasa FindSystemFontFile a App::ChooseFontFiles.
TEST(SystemFontsTest, WindowsHasTheList)
{
    EXPECT_TRUE(HasSystemFontList());
}
#endif
} // namespace
