#include "Render/Text/FontSet.h"

#include "App/Resolution.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

// Client::Render::Text::FontStepFor, FontHeightFor e IsBold: las cuatro fuentes de WinMain
// (0x4ba6af..0x4ba813) y el paso de la letra por el ancho de la pantalla (pregunta 10 del plan de
// la CL5, Q-C59).
namespace
{
using Client::Render::Text::FontHeightFor;
using Client::Render::Text::FontKind;
using Client::Render::Text::FontStepFor;
using Client::Render::Text::IsBold;

// Los cuatro anchos de la tabla de WinMain, y el escalón para los demás: el paso del más grande de
// los cuatro que no pasa al ancho, y 12 por debajo de 800. Con 1600, el original deja el paso en
// 0 y las letras en -1 y -2 (Q-C59); MuRet da 15.
TEST(FontSetTest, TheStepFollowsTheWidth)
{
    EXPECT_EQ(FontStepFor(640), 12);
    EXPECT_EQ(FontStepFor(800), 13);
    EXPECT_EQ(FontStepFor(1024), 14);
    EXPECT_EQ(FontStepFor(1280), 15);

    EXPECT_EQ(FontStepFor(320), 12);
    EXPECT_EQ(FontStepFor(799), 12);
    EXPECT_EQ(FontStepFor(1000), 13);
    EXPECT_EQ(FontStepFor(1023), 13);
    EXPECT_EQ(FontStepFor(1279), 14);
    EXPECT_EQ(FontStepFor(1600), 15);
    EXPECT_EQ(FontStepFor(1920), 15);
}

// Las resoluciones que valen (App::IsValidResolution) son las cinco del original o un modo del
// monitor, de cualquier ancho: las cinco con su paso, y unos modos de monitor de hoy.
TEST(FontSetTest, EveryValidResolutionHasAStep)
{
    constexpr std::array<int, 5> OriginalSteps = {12, 13, 14, 15, 15};
    for (std::size_t i = 0; i < Client::App::OriginalResolutions.size(); ++i)
    {
        EXPECT_EQ(FontStepFor(Client::App::OriginalResolutions[i].width), OriginalSteps[i]) << i;
    }

    EXPECT_EQ(FontStepFor(1152), 14);
    EXPECT_EQ(FontStepFor(1366), 15);
    EXPECT_EQ(FontStepFor(2560), 15);
    EXPECT_EQ(FontStepFor(3840), 15);
}

// CreateFontA con n − 1 la normal y la negrita, 2 (n − 1) la negrita grande y 12 fijo la de la otra
// cara (0x4ba737, 0x4ba777, 0x4ba7b9 y 0x4ba813).
TEST(FontSetTest, EachFontHasItsHeight)
{
    EXPECT_EQ(FontHeightFor(FontKind::Normal, 640), 11);
    EXPECT_EQ(FontHeightFor(FontKind::Bold, 640), 11);
    EXPECT_EQ(FontHeightFor(FontKind::BigBold, 640), 22);
    EXPECT_EQ(FontHeightFor(FontKind::OtherFace12, 640), 12);

    EXPECT_EQ(FontHeightFor(FontKind::Normal, 1280), 14);
    EXPECT_EQ(FontHeightFor(FontKind::Bold, 1024), 13);
    EXPECT_EQ(FontHeightFor(FontKind::BigBold, 1280), 28);
    EXPECT_EQ(FontHeightFor(FontKind::OtherFace12, 1920), 12);
}

// La de los créditos (0x63f21c..0x63f2e7): 14, 18 y 24 con 800, 1024 y 1280 justos, y 10 con cualquier
// otro ancho, también 640 y 1600.
TEST(FontSetTest, TheCreditsFontGoesByTheExactWidth)
{
    EXPECT_EQ(FontHeightFor(FontKind::Credits, 800), 14);
    EXPECT_EQ(FontHeightFor(FontKind::Credits, 1024), 18);
    EXPECT_EQ(FontHeightFor(FontKind::Credits, 1280), 24);
    EXPECT_EQ(FontHeightFor(FontKind::Credits, 640), 10);
    EXPECT_EQ(FontHeightFor(FontKind::Credits, 801), 10);
    EXPECT_EQ(FontHeightFor(FontKind::Credits, 1600), 10);
}

// Peso 700 la negrita, la negrita grande y la de los créditos; 400 las otras dos.
TEST(FontSetTest, OnlyTheBoldOnesAreBold)
{
    EXPECT_FALSE(IsBold(FontKind::Normal));
    EXPECT_TRUE(IsBold(FontKind::Bold));
    EXPECT_TRUE(IsBold(FontKind::BigBold));
    EXPECT_FALSE(IsBold(FontKind::OtherFace12));
    EXPECT_TRUE(IsBold(FontKind::Credits));
}
} // namespace
