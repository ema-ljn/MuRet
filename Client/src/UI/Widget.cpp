#include "UI/Widget.h"

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Scenes/SceneTextures.h"

#include <algorithm>

namespace Client::UI
{
namespace
{
// Una coordenada del mouse a un píxel de [0, size − 1]. La comparación va antes del cast: un NaN
// no es mayor ni igual a nada, y un float fuera del rango de int no se puede pasar a int.
int ClampToPixel(float value, int size)
{
    const int last = std::max(size - 1, 0);
    if (!(value >= 0.0f))
    {
        return 0;
    }
    if (value >= static_cast<float>(last))
    {
        return last;
    }
    return static_cast<int>(value);
}
} // namespace

bool Rect::Contains(Point point) const
{
    return point.x >= x && point.x < x + width && point.y >= y && point.y < y + height;
}

Point MouseInPixels(const Input::InputState& input, App::PixelSize screen)
{
    return Point{ClampToPixel(input.MouseX(), screen.width), ClampToPixel(input.MouseY(), screen.height)};
}

Render::UiImage PixelImage(Render::TextureSlot slot, Rect area, Scenes::TextureBufferSize buffer)
{
    return Render::UiImage{.slot = slot,
                           .x = area.x,
                           .y = area.y,
                           .width = area.width,
                           .height = area.height,
                           .textureWidth = static_cast<float>(buffer.width),
                           .textureHeight = static_cast<float>(buffer.height)};
}
} // namespace Client::UI
