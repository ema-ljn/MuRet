#include "UI/HudPrimitives.h"

#include "Render/OriginalQuad.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ITextDrawer.h"

#include <string>
#include <string_view>

namespace Client::UI
{
namespace
{
// Las constantes de 0x6218f0: 0,5 (0x6b882c), 0,7 en float (0x6b8a5c) y 1/16 (0x6b8850).
constexpr double Half = 0.5;
constexpr float DigitAdvance = 0.7f;
constexpr float DigitU = 0.0625f;
constexpr float DigitV = 0.5f;

// La x87 de 53 bits y el fstp en float.
float F32(double value)
{
    return static_cast<float>(value);
}

// glColor3f de las barras chicas: los bytes de 0x5a6af0 (0x3e4ccccd, 0x3e48c8c9, 0x3d20a0a1 y 0x3f48c8c9).
constexpr float Tenth2 = 0.2f;
constexpr float Of255_10 = 10.0f / 255.0f;
constexpr float Of255_50 = 50.0f / 255.0f;
constexpr float Of255_200 = 200.0f / 255.0f;
// El borde de la pantalla para la barra acotada: 640 y 433 (0x6b8cb0, 0x6b8a04), con el 4 y el 5 del marco.
constexpr float GaugeRight = 640.0f;
constexpr float GaugeBottom = 433.0f;
constexpr float FrameGrowth = 4.0f;
constexpr float FrameBack = 5.0f;
} // namespace

float DrawNumber(Render::SpriteBatch& sprites, float x, float y, int number, float width, float height)
{
    const std::string digits = std::to_string(number);
    const auto length = static_cast<int>(digits.size());
    // 0x62191e..0x62192f: fild len, fmul w, fmul 0,5, fsubr x y fstp.
    float at = F32(static_cast<double>(x) - static_cast<double>(length) * static_cast<double>(width) * Half);
    if (length <= 0)
    {
        return at;
    }
    // 0x621934..0x621941: w · 0,7 en float, una vez.
    const float advance = F32(static_cast<double>(width) * static_cast<double>(DigitAdvance));
    for (const char digit : digits)
    {
        const int index = static_cast<int>(digit) - static_cast<int>('0');
        sprites.Quad(Render::OriginalQuad{.slot = DigitsSlot,
                                          .x = at,
                                          .y = y,
                                          .width = width,
                                          .height = height,
                                          .u = F32(static_cast<double>(index) * static_cast<double>(DigitU)),
                                          .v = 0.0f,
                                          .du = DigitU,
                                          .dv = DigitV,
                                          .scaleSize = true,
                                          .scalePosition = true,
                                          .alpha = 0.0f});
        at = F32(static_cast<double>(advance) + static_cast<double>(at));
    }
    return at;
}

void DrawGauge(Render::SpriteBatch& sprites, float x, float y, float width, float height, float fill, bool red,
               bool keepOnScreen)
{
    if (keepOnScreen)
    {
        if (x < 0.0f)
            x = 0.0f;
        if (y < 0.0f)
            y = 0.0f;
        if (static_cast<double>(x) + static_cast<double>(width) + FrameGrowth > GaugeRight)
            x = F32(GaugeRight - (static_cast<double>(width) + FrameBack));
        if (static_cast<double>(y) + static_cast<double>(height) + FrameGrowth > GaugeBottom)
            y = F32(GaugeBottom - (static_cast<double>(height) + FrameBack));
    }
    const float frameWidth = F32(static_cast<double>(width) + FrameGrowth);
    const float frameHeight = F32(static_cast<double>(height) + FrameGrowth);

    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(Render::Color{0.0f, 0.0f, 0.0f, 0.5f});
    sprites.ColorQuad(F32(static_cast<double>(x) + 1.0), F32(static_cast<double>(y) + 1.0), frameWidth, frameHeight);

    sprites.SetBlend(Render::BlendMode::Additive);
    sprites.SetColor(red ? Render::Color{Tenth2, 0.0f, 0.0f, 1.0f} : Render::Color{0.0f, Tenth2, Tenth2, 1.0f});
    sprites.ColorQuad(x, y, frameWidth, frameHeight);

    const float innerX = F32(static_cast<double>(x) + 2.0);
    const float innerY = F32(static_cast<double>(y) + 2.0);
    sprites.SetColor(red ? Render::Color{Of255_50, Of255_10, 0.0f, 1.0f}
                         : Render::Color{0.0f, Of255_50, Of255_50, 1.0f});
    sprites.ColorQuad(innerX, innerY, width, height);

    sprites.SetColor(red ? Render::Color{Of255_200, Of255_50, 0.0f, 1.0f}
                         : Render::Color{0.0f, Of255_200, Of255_50, 1.0f});
    sprites.ColorQuad(innerX, innerY, fill, height);

    sprites.SetBlend(Render::BlendMode::Opaque);
}

void DrawHelp(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, int x, int y, std::string_view help)
{
    // 0x554346..0x55437a: SelectObject con la fuente normal y GetTextExtentPoint32 del texto.
    const Render::Text::TextExtent extent = text.Measure(Render::Text::FontKind::Normal, help);
    const double textWidth =
        static_cast<double>(extent.width) / static_cast<double>(Render::ScaleX(1.0f, sprites.Screen()));
    const double textHeight =
        static_cast<double>(extent.height) / static_cast<double>(Render::ScaleY(1.0f, sprites.Screen()));
    const Render::BlendMode before = sprites.Blend();

    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(Render::Color{0.0f, 0.0f, 0.0f, 1.0f});
    // 0x5543a3..0x5543c7: fy − 3 y fx − 2 (0x6b86b8 y 0x6b8818), guardadas en float.
    const auto fx = static_cast<float>(x);
    const auto fy = static_cast<float>(y);
    const float top = F32(static_cast<double>(fy) - 3.0);
    const float left = F32(static_cast<double>(fx) - 2.0);
    const float frameWidth = F32(textWidth + 4.0);
    const float frameHeight = F32(textHeight + 4.0);
    sprites.ColorQuad(left, top, frameWidth, 1.0f);
    sprites.ColorQuad(left, top, 1.0f, frameHeight);
    sprites.ColorQuad(F32(textWidth + static_cast<double>(left) + 3.0), top, 1.0f, frameHeight);
    sprites.ColorQuad(left, F32(textHeight + static_cast<double>(top) + 3.0), frameWidth, 1.0f);

    sprites.SetColor(Render::Color{0.0f, 0.0f, 0.0f, 0.8f});
    sprites.ColorQuad(F32(static_cast<double>(fx) - 1.0), F32(static_cast<double>(fy) - 2.0), F32(textWidth + 2.0),
                      F32(textHeight + 2.0));

    sprites.SetColor(Render::Color{});
    sprites.EnableTexture();
    // 0x55450f..0x554523: el fondo en 0 y el texto en blanco, y 0x465950(x, y, texto, 0, 0, 1, 0, 0x280).
    text.Draw(x, y, help,
              Render::Text::TextStyle{.font = Render::Text::FontKind::Normal,
                                      .colors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0}});
    // 0x55452d..0x554576: la de antes, por su número (0x620980 con 0, 0x620a00(1) con 2 y 0x620a90 con 3).
    sprites.SetBlend(before);
}
} // namespace Client::UI
