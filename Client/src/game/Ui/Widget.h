#pragma once

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Assets/SceneTextures.h"

#include <functional>

// What the login UI windows share (CL5.5): points and rectangles in framebuffer pixels, with y from the top, as the
// original's new UI keeps them (06 § Las ventanas de la UI nueva); the UI mouse, and the images in pixels. Each window
// is handled in the step, with CL3.3's edges, and drawn in the frame (Q-C81).
namespace Client::Ui
{
struct Point
{
    int x = 0;
    int y = 0;

    friend bool operator==(const Point&, const Point&) = default;
};

struct Rect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    // Like PtInRect: the left and top edges belong to the rectangle; the right and bottom ones do not.
    [[nodiscard]] bool Contains(Point point) const;

    friend bool operator==(const Rect&, const Rect&) = default;
};

// The new UI's mouse (0x640940, question 12 of the CL5 plan): the last position of `input`, which already arrives in
// framebuffer pixels, truncated to the pixel and clamped to [0, W − 1] × [0, H − 1]. One that is not a number goes
// to 0.
[[nodiscard]] Point MouseInPixels(const Input::InputState& input, Core::PixelSize screen);

// A slot's texture buffer, for the images' UVs: in the client, the scene's SceneTextures::BufferSize.
using TextureSizes = std::function<Assets::TextureBufferSize(Render::TextureSlot)>;

// A window image: in pixels (fx = fy = 1), with the texture of `slot` and its buffer.
[[nodiscard]] Render::UiImage PixelImage(Render::TextureSlot slot, Rect area, Assets::TextureBufferSize buffer);
} // namespace Client::Ui
