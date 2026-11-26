#include "Platform/Sdl/SystemFonts.h"

#include <gtest/gtest.h>

// Windows' font list (HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts) names a value after its faces,
// separated by " & " in a collection, with its type in parentheses; bold is the face with " Bold". The faces in these
// tests are invented.
namespace
{
using Client::Platform::Sdl::FindSystemFontFile;
using Client::Platform::Sdl::FontEntryNamesFace;
using Client::Platform::Sdl::HasSystemFontList;

TEST(SystemFontsTest, AnEntryNamesItsFace)
{
    EXPECT_TRUE(FontEntryNamesFace("Cara (TrueType)", "Cara", false));
    EXPECT_TRUE(FontEntryNamesFace("Cara Bold (TrueType)", "Cara", true));
    EXPECT_FALSE(FontEntryNamesFace("Cara Bold (TrueType)", "Cara", false));
    EXPECT_FALSE(FontEntryNamesFace("Cara (TrueType)", "Cara", true));
    EXPECT_FALSE(FontEntryNamesFace("Cara Bold Italic (TrueType)", "Cara", true));
}

TEST(SystemFontsTest, ACollectionNamesEachFace)
{
    EXPECT_TRUE(FontEntryNamesFace("Una & Otra & Tercera (TrueType)", "Otra", false));
    EXPECT_TRUE(FontEntryNamesFace("Una & Otra & Tercera (TrueType)", "Tercera", false));
    EXPECT_FALSE(FontEntryNamesFace("Una & Otra & Tercera (TrueType)", "Una & Otra", false));
}

// GDI ignores case in the face name.
TEST(SystemFontsTest, TheCaseDoesNotMatter)
{
    EXPECT_TRUE(FontEntryNamesFace("CARA (TrueType)", "cara", false));
    EXPECT_TRUE(FontEntryNamesFace("cara bold (OpenType)", "Cara", true));
}

TEST(SystemFontsTest, APrefixIsAnotherFace)
{
    EXPECT_FALSE(FontEntryNamesFace("Cara Light (TrueType)", "Cara", false));
    EXPECT_FALSE(FontEntryNamesFace("Car (TrueType)", "Cara", false));
    EXPECT_FALSE(FontEntryNamesFace("Caras (TrueType)", "Cara", false));
}

// FreeTypeRasterizer asks for outlines (FT_LOAD_NO_BITMAP): the list's bitmap fonts (the .fon ones, without
// "(TrueType)" or "(OpenType)") do not count.
TEST(SystemFontsTest, OnlyOutlineFontsCount)
{
    EXPECT_FALSE(FontEntryNamesFace("Cara 8,10,12,14 (All res)", "Cara 8,10,12,14", false));
    EXPECT_FALSE(FontEntryNamesFace("Cara", "Cara", false));
}

// Text 0 of a Text.bmd with no face.
TEST(SystemFontsTest, AnEmptyFaceIsNeverFound)
{
    EXPECT_FALSE(FontEntryNamesFace(" (TrueType)", "", false));
    EXPECT_FALSE(FindSystemFontFile("", false).has_value());
}

#ifndef _WIN32
// main.cpp passes App::ChooseFontFiles no search, and without [Video] FontFile its error says the client does not look
// for the face.
TEST(SystemFontsTest, OutsideWindowsThereIsNoList)
{
    EXPECT_FALSE(HasSystemFontList());
    EXPECT_FALSE(FindSystemFontFile("Cara", false).has_value());
    EXPECT_FALSE(FindSystemFontFile("Cara", true).has_value());
}
#else
// main.cpp passes FindSystemFontFile to App::ChooseFontFiles.
TEST(SystemFontsTest, WindowsHasTheList)
{
    EXPECT_TRUE(HasSystemFontList());
}
#endif
} // namespace
