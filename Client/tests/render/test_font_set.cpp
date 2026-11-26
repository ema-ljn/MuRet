#include "Render/Text/FontSet.h"

#include "Platform/Resolution.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

// The four fonts of WinMain (0x4ba6af..0x4ba813) and the letter step by screen width (question 10 of the CL5 plan,
// Q-C59).
namespace
{
using Client::Render::Text::FontHeightFor;
using Client::Render::Text::FontKind;
using Client::Render::Text::FontStepFor;
using Client::Render::Text::IsBold;

// The four widths of WinMain's table, and the step for the rest: the step of the largest of the four that does not
// exceed the width, and 12 below 800. With 1600 the original leaves the step at 0 and the letters at -1 and -2
// (Q-C59); MuRet gives 15.
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

// The valid resolutions (Platform::IsValidResolution) are the original's five or a monitor mode, of any width: the
// five with their step, and some of today's monitor modes.
TEST(FontSetTest, EveryValidResolutionHasAStep)
{
    constexpr std::array<int, 5> OriginalSteps = {12, 13, 14, 15, 15};
    for (std::size_t i = 0; i < Client::Platform::OriginalResolutions.size(); ++i)
    {
        EXPECT_EQ(FontStepFor(Client::Platform::OriginalResolutions[i].width), OriginalSteps[i]) << i;
    }

    EXPECT_EQ(FontStepFor(1152), 14);
    EXPECT_EQ(FontStepFor(1366), 15);
    EXPECT_EQ(FontStepFor(2560), 15);
    EXPECT_EQ(FontStepFor(3840), 15);
}

// CreateFontA with n − 1 for the normal and the bold, 2 (n − 1) for the big bold and a fixed 12 for the other face
// (0x4ba737, 0x4ba777, 0x4ba7b9 and 0x4ba813).
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

// The credits font (0x63f21c..0x63f2e7): 14, 18 and 24 at exactly 800, 1024 and 1280, and 10 at any other width,
// 640 and 1600 included.
TEST(FontSetTest, TheCreditsFontGoesByTheExactWidth)
{
    EXPECT_EQ(FontHeightFor(FontKind::Credits, 800), 14);
    EXPECT_EQ(FontHeightFor(FontKind::Credits, 1024), 18);
    EXPECT_EQ(FontHeightFor(FontKind::Credits, 1280), 24);
    EXPECT_EQ(FontHeightFor(FontKind::Credits, 640), 10);
    EXPECT_EQ(FontHeightFor(FontKind::Credits, 801), 10);
    EXPECT_EQ(FontHeightFor(FontKind::Credits, 1600), 10);
}

// Weight 700 for the bold, the big bold and the credits font; 400 for the other two.
TEST(FontSetTest, OnlyTheBoldOnesAreBold)
{
    EXPECT_FALSE(IsBold(FontKind::Normal));
    EXPECT_TRUE(IsBold(FontKind::Bold));
    EXPECT_TRUE(IsBold(FontKind::BigBold));
    EXPECT_FALSE(IsBold(FontKind::OtherFace12));
    EXPECT_TRUE(IsBold(FontKind::Credits));
}
} // namespace
