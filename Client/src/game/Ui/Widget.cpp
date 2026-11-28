#include "Ui/Widget.h"

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Assets/SceneTextures.h"

#include <algorithm>

namespace Client::Ui
{
namespace
{
// A mouse coordinate to a pixel in [0, size − 1]. The comparison goes before the cast: a NaN is neither greater than
// nor equal to anything, and a float outside int's range cannot be converted to int.
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

Point MouseInPixels(const Input::InputState& input, Core::PixelSize screen)
{
    return Point{ClampToPixel(input.MouseX(), screen.width), ClampToPixel(input.MouseY(), screen.height)};
}

Render::UiImage PixelImage(Render::TextureSlot slot, Rect area, Assets::TextureBufferSize buffer)
{
    return Render::UiImage{.slot = slot,
                           .x = area.x,
                           .y = area.y,
                           .width = area.width,
                           .height = area.height,
                           .textureWidth = static_cast<float>(buffer.width),
                           .textureHeight = static_cast<float>(buffer.height)};
}
} // namespace Client::Ui
