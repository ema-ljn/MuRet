#include "UI/GameCursor.h"

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "UI/Widget.h"

#include <cmath>
#include <cstdint>

namespace Client::UI
{
namespace
{
// Los 16 bits bajos del píxel de `value`, como el LOWORD o el HIWORD del lParam. Un píxel fuera del rango
// de int32 no llega de SDL; se acota antes del cast, que con él no estaría definido.
std::uint32_t LowWordOf(float value)
{
    if (!std::isfinite(value))
    {
        return 0;
    }
    constexpr double Lowest = -2147483648.0;
    constexpr double Highest = 2147483647.0;
    const double pixel = std::floor(static_cast<double>(value));
    const double bounded = pixel < Lowest ? Lowest : (pixel > Highest ? Highest : pixel);
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(bounded)) & 0xffffU;
}

// `word` · `logical` / `size`, como el IMUL y el DIV sin signo de 0x4b3597..0x4b359f.
int ScaledTo(std::uint32_t word, std::uint32_t logical, int size)
{
    if (size <= 0)
    {
        return 0;
    }
    return static_cast<int>(word * logical / static_cast<std::uint32_t>(size));
}
} // namespace

Point MouseIn640x480(const Input::InputState& input, App::PixelSize screen)
{
    constexpr std::uint32_t Width640 = 640;
    constexpr std::uint32_t Height480 = 480;
    const std::uint32_t x = LowWordOf(input.MouseX());
    const std::uint32_t y = LowWordOf(input.MouseY());
    if (screen.width <= 0 || screen.height <= 0)
    {
        return Point{};
    }
    return Point{ScaledTo(x, Width640, screen.width), ScaledTo(y, Height480, screen.height)};
}

Render::OriginalQuad CursorQuad(Point mouse, bool leftDown)
{
    // 0x41c00000 y el 2,0 de 0x6b8818, restado en la x87 y guardado en float: exacto con estos enteros.
    constexpr float Size = 24.0f;
    constexpr float HotSpot = 2.0f;
    return Render::OriginalQuad{.slot = leftDown ? CursorPushSlot : CursorSlot,
                                .x = static_cast<float>(mouse.x) - HotSpot,
                                .y = static_cast<float>(mouse.y) - HotSpot,
                                .width = Size,
                                .height = Size,
                                .u = 0.0f,
                                .v = 0.0f,
                                .du = 1.0f,
                                .dv = 1.0f,
                                .scaleSize = true,
                                .scalePosition = true,
                                .alpha = 0.0f};
}

void DrawGameCursor(Render::SpriteBatch& sprites, const Input::InputState& input, App::PixelSize screen)
{
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(Render::Color{});
    sprites.Quad(CursorQuad(MouseIn640x480(input, screen), input.IsDown(Input::MouseButton::Left)));
}
} // namespace Client::UI
