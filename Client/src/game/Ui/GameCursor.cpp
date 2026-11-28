#include "Ui/GameCursor.h"

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Ui/Widget.h"

#include <cmath>
#include <cstdint>

namespace Client::Ui
{
namespace
{
// The low 16 bits of `value`'s pixel, like the lParam's LOWORD or HIWORD. SDL sends no pixel outside int32; it is
// clamped before the cast, which would be undefined with one.
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

// `word` · `logical` / `size`, like the IMUL and unsigned DIV at 0x4b3597..0x4b359f.
int ScaledTo(std::uint32_t word, std::uint32_t logical, int size)
{
    if (size <= 0)
    {
        return 0;
    }
    return static_cast<int>(word * logical / static_cast<std::uint32_t>(size));
}
} // namespace

Point MouseIn640x480(const Input::InputState& input, Core::PixelSize screen)
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
    // 0x41c00000 and the 2.0 at 0x6b8818, subtracted on the x87 and stored as float: exact with these integers.
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

void DrawGameCursor(Render::SpriteBatch& sprites, const Input::InputState& input, Core::PixelSize screen)
{
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(Render::Color{});
    sprites.Quad(CursorQuad(MouseIn640x480(input, screen), input.IsDown(Input::MouseButton::Left)));
}
} // namespace Client::Ui
