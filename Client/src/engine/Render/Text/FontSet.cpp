#include "Render/Text/FontSet.h"

namespace Client::Render::Text
{
int FontStepFor(int screenWidth)
{
    // Los cuatro de WinMain (0x4ba6af..0x4ba6d3) como escalones: cada uno vale desde su ancho.
    if (screenWidth < 800)
    {
        return 12;
    }
    if (screenWidth < 1024)
    {
        return 13;
    }
    if (screenWidth < 1280)
    {
        return 14;
    }
    return 15;
}

int FontHeightFor(FontKind font, int screenWidth)
{
    const int step = FontStepFor(screenWidth);
    switch (font)
    {
    case FontKind::Normal:
    case FontKind::Bold:
        return step - 1;
    case FontKind::BigBold:
        return 2 * (step - 1);
    case FontKind::OtherFace12:
        return 12;
    case FontKind::Credits:
        // 0x63f21c..0x63f2e7: solo esos tres anchos, comparados con ==.
        switch (screenWidth)
        {
        case 800:
            return 14;
        case 1024:
            return 18;
        case 1280:
            return 24;
        default:
            return 10;
        }
    }
    return step - 1;
}

bool IsBold(FontKind font)
{
    return font == FontKind::Bold || font == FontKind::BigBold || font == FontKind::Credits;
}
} // namespace Client::Render::Text
