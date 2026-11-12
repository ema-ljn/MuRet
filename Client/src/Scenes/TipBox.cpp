#include "Scenes/TipBox.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/WorldRuntime.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x465950 con el modo 2: el texto centrado en el ancho.
constexpr std::int32_t CenteredMode = 2;
// 0x5af14f..0x5af169: el medio renglón de los '\n' y el 0,9090909 que divide el alto.
constexpr double HalfLine = 0.5;
constexpr float HeightDivisor = 0.9090908765792847f;
// 0x5af191: el margen del ancho; 0x5af1d9 y los del marco, 1; 0x5af281 y 0x5af439, 2.
constexpr double WidthMargin = 4.0;
constexpr double One = 1.0;
constexpr double Two = 2.0;
// 0x5af4b0: el paso entre renglones.
constexpr float LineStep = 1.1f;
// 0x5af359: el texto en blanco.
constexpr std::uint32_t WhiteText = 0xffffffffu;
constexpr Render::Color Black{0.0f, 0.0f, 0.0f, 1.0f};
// 0x5af2ac: el fondo de la caja, 0x3f4ccccd.
constexpr Render::Color Shade{0.0f, 0.0f, 0.0f, 0.8f};
constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

float F32(double value)
{
    return static_cast<float>(value);
}

// _ftol: trunca hacia el 0.
std::int32_t Truncate(double value)
{
    return static_cast<std::int32_t>(value);
}

Render::Text::FontKind FontOf(const TipLine& line)
{
    return line.bold != 0 ? Render::Text::FontKind::Bold : Render::Text::FontKind::Normal;
}

// 0x5af368: el glColor3f de cada color; con más de 10, ninguno.
std::optional<Render::Color> ColorOf(std::int32_t color)
{
    switch (color)
    {
    case 0:
    case 5:
    case 7:
    case 8:
    case 9:
        return Render::Color{1.0f, 1.0f, 1.0f, 1.0f};
    case 1:
        return Render::Color{0.5f, 0.7f, 1.0f, 1.0f};
    case 2:
        return Render::Color{1.0f, 0.2f, 0.1f, 1.0f};
    case 3:
        return Render::Color{1.0f, 0.8f, 0.1f, 1.0f};
    case 4:
        return Render::Color{0.1f, 1.0f, 0.5f, 1.0f};
    case 6:
        return Render::Color{1.0f, 0.1f, 1.0f, 1.0f};
    case 10:
        return Render::Color{0.4f, 0.4f, 0.4f, 1.0f};
    default:
        return std::nullopt;
    }
}

// 0x5af3d7..0x5af42c: el fondo de 0x465950 por color (el 9 también pone el texto en 0xff00ff00).
std::uint32_t BackgroundOf(std::int32_t color)
{
    switch (color)
    {
    case 5:
        return 0xff0000a0u;
    case 7:
        return 0xffa00000u;
    case 8:
        return 0xff0066a0u;
    case 9:
        return 0xffc83c3cu;
    default:
        return 0;
    }
}
} // namespace

Core::Expected<void, std::string> DrawTheTipBox(TipBoxContext& context, const TipBoxCall& call)
{
    if (call.mode != CenteredMode)
        return Core::Unexpected(std::format("0x5af471: la caja de ayuda con el modo {} de 0x465950", call.mode));
    HudState& hud = context.hud;
    const TipList& list = hud.tips;
    const App::PixelSize screen = context.sprites.Screen();
    const double ratioX = Render::ScaleX(1.0f, screen);
    const double ratioY = Render::ScaleY(1.0f, screen);
    // 0x5af0c5..0x5af13d: hasta el primer renglón vacío, el más ancho, el alto del último y cuántos hay de cada clase.
    std::int32_t count = call.count;
    std::int32_t breaks = 0;
    std::int32_t lines = 0;
    std::int32_t height = 0;
    float widest = 0.0f;
    for (std::int32_t i = 0; i < call.count; ++i)
    {
        if (i >= static_cast<std::int32_t>(TipLineCount))
            return Core::Unexpected(std::format("0x5af0c5: el renglón {} de la caja de ayuda (0x78793c8)", i));
        const TipLine& line = list.lines[static_cast<std::size_t>(i)];
        if (line.text.empty())
        {
            count = i;
            break;
        }
        hud.dcFont = FontOf(line);
        const Render::Text::TextExtent extent = context.text.Measure(hud.dcFont, line.text);
        height = extent.height;
        if (widest < static_cast<float>(extent.width))
            widest = static_cast<float>(extent.width);
        if (line.text.front() == '\n')
            ++breaks;
        else
            ++lines;
    }
    // 0x5af140..0x5af169: (los '\n' · cy · 0,5 + los demás · cy) / (ry · 0,9090909).
    const float boxHeight = F32(
        (static_cast<double>(breaks) * static_cast<double>(height) * HalfLine + static_cast<double>(lines * height)) /
        (ratioY * static_cast<double>(HeightDivisor)));
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    // 0x5af177..0x5af197: el más ancho en 640 × 480, o el doble del ancho pedido, más 4.
    double width = static_cast<double>(widest) / ratioX;
    if (call.width > 0)
    {
        const double half = static_cast<double>(call.width) / ratioX;
        width = half + half;
    }
    const float boxWidth = F32(width + WidthMargin);
    // 0x5af19a..0x5af1e7: centrada en x, sin pasar del borde izquierdo ni del derecho.
    std::int32_t left = Truncate(static_cast<double>(call.x) - static_cast<double>(boxWidth) * HalfLine);
    if (left < 0)
        left = 0;
    const double screenWidth = static_cast<double>(screen.width) / ratioX;
    if (static_cast<double>(left) + static_cast<double>(boxWidth) > screenWidth)
        left = Truncate(screenWidth - static_cast<double>(boxWidth) - One);
    // 0x5af1eb..0x5af2da: el marco de 1 en negro y el fondo negro al 80 %, sin textura; después la textura.
    if (call.frame)
    {
        Render::SpriteBatch& sprites = context.sprites;
        sprites.SetColor(Black);
        const float top = F32(static_cast<double>(call.y) - One);
        const float outerLeft = F32(static_cast<double>(left) - One);
        const float sideHeight = F32(static_cast<double>(boxHeight) + One);
        sprites.ColorQuad(outerLeft, top, F32(static_cast<double>(boxWidth) + One), 1.0f);
        sprites.ColorQuad(outerLeft, top, 1.0f, sideHeight);
        sprites.ColorQuad(F32(static_cast<double>(outerLeft) + static_cast<double>(boxWidth) + One), top, 1.0f,
                          sideHeight);
        sprites.ColorQuad(outerLeft, F32(static_cast<double>(top) + static_cast<double>(boxHeight) + One),
                          F32(static_cast<double>(boxWidth) + Two), 1.0f);
        sprites.SetColor(Shade);
        sprites.ColorQuad(static_cast<float>(left), static_cast<float>(call.y), boxWidth, boxHeight);
        sprites.EnableTexture();
    }
    // 0x5af2e0..0x5af4c5: cada renglón desde (left + 1, y), que baja 1,1 veces lo que da cada uno.
    const std::int32_t textX = left + 1;
    float y = static_cast<float>(call.y);
    for (std::int32_t i = 0; i < count; ++i)
    {
        const TipLine& line = list.lines[static_cast<std::size_t>(i)];
        hud.dcFont = FontOf(line);
        const Render::Text::TextExtent extent = context.text.Measure(hud.dcFont, line.text);
        double advance = static_cast<double>(extent.height) / ratioY;
        if (line.text.front() == '\n' || line.text == " ")
        {
            // 0x5af478..0x5af4ae: sin dibujar, medio renglón con el '\n' y uno con el espacio.
            if (line.text.front() == '\n')
                advance /= Two;
        }
        else
        {
            // 0x5af352..0x5af436: el blanco, el color del renglón y su fondo.
            hud.textColors.text = WhiteText;
            if (const std::optional<Render::Color> color = ColorOf(line.color))
                context.sprites.SetColor(*color);
            const std::uint32_t background = BackgroundOf(line.color);
            if (background != 0)
                return Core::Unexpected(
                    std::format("0x5af3e3: el renglón {} de la caja de ayuda con el color {} lleva un "
                                "fondo del ancho de la caja (0x467060 con el modo 2)",
                                i, line.color));
            hud.textColors.background = background;
            // 0x5af436..0x5af471: 0x465950(left + 1, y, texto, (ancho − 2) · rx, 0, 2, 0, 0x280); el modo 2 corre el
            // texto (ancho − cx) / 2 píxeles desde x · rx, y MuRet lo centra antes de pasar a 640 × 480.
            const std::int32_t pixelWidth = Truncate((static_cast<double>(boxWidth) - Two) * ratioX);
            const std::int32_t pixelLeft = Truncate(static_cast<double>(textX) * ratioX);
            context.text.Draw(Render::Text::DesignX(pixelLeft + (pixelWidth - extent.width) / 2, screen), Truncate(y),
                              line.text, Render::Text::TextStyle{.font = hud.dcFont, .colors = hud.textColors});
        }
        y = F32(advance * static_cast<double>(LineStep) + static_cast<double>(y));
    }
    // 0x5af4cb..0x5af4e5.
    context.sprites.SetColor(White);
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    return {};
}
} // namespace Client::Scenes
