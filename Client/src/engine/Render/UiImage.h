#pragma once

#include "Core/PixelSize.h"
#include "EngineApi.h"
#include "Render/QuadCorner.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"

#include <optional>

// The original's new UI image (06 § La carga and § Las ventanas de la UI nueva): a 0xa8-byte object built by
// 0x647050(w, h, slot, ..., fx, fy), placed by 0x6472e0(x, y, 3) and drawn by 0x647650 as a GL_TRIANGLE_FAN. Unlike
// 0x621b60's quad it lives in its own space: y from the top, the screen height mapped into it (H / fy), each corner
// times fx and fy when drawn, and UVs half a texel inside the edge. Not carried yet: the +0x9c anchor (0 in the images
// read so far) and an animation's own advance (0x6475d0). The +0x58/+0x5c point, which 0x6472e0 puts where asked, is
// subtracted by whoever places it: x and y are always the corner. Frames are carried (frameTop, frameLeft; 0x647260).
namespace Client::Render
{
struct UiImage
{
    // The texture slot, or none: with -1 the original draws only the color, untextured.
    std::optional<TextureSlot> slot{};
    // Where 0x6472e0(x, y, 3) places it: the top-left corner, in the image's space (screen pixels divided by fx and
    // fy), y from the top.
    int x = 0;
    int y = 0;
    // 0x647050's first two arguments: the size in that space and, for the UVs, in texels.
    int width = 0;
    int height = 0;
    // The texture buffer size (the floats at +0x20 and +0x24 of its slot, the power-of-2 sides glTexImage2D uploads),
    // for the UVs. Unused without a slot; above 0 with one.
    float textureWidth = 1.0f;
    float textureHeight = 1.0f;
    // fx and fy (+0x90 and +0x94): from that space to screen pixels.
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    // glColor4ub's (+0x61 to +0x63, alpha at +0x60): white unless changed. It stays as GL's color after drawing
    // (Q-C75).
    Color color{};
    // 0x647370(w, h, mode) changed its width (mode 1) or height (mode 2) after building it: the right u is width / tw
    // (0x6473dd) or the bottom v height / th (0x647457), without the half texel. Used by the loading bar (0x6402d0 and
    // 0x640580). The corners do not change: 0x647370 computes them like 0x647050 and 0x6472e0.
    bool resizedWidth = false;
    bool resizedHeight = false;
    // The frame 0x647560 chose, as the texel row of its top edge: buttons (0x63c330) stack theirs downward, frame k at
    // k · h. Without frames, none. Only with a slot.
    std::optional<int> frameTop{};
    // And the column of its left edge, with frameTop: 0 for buttons, and the image's origin in its texture for
    // 0x647260's (0x647050's list with a single frame). An image with frames does not follow 0x647370 (0x647050 sets
    // +0x98 to 0): with frameTop, resizedWidth and resizedHeight are false.
    int frameLeft = 0;
    // The height 0x647050 built it with, if 0x647370 later changed the height without moving the UVs (an image with
    // +0x98 at 0, like those with frames): the UVs come from this height, and the corners from height. The texture
    // stretches. Without it, both come from height.
    std::optional<int> uvHeight{};
};

// The four corners, like 0x647050, 0x6472e0 and 0x647650. The top edge is H / fy − y, stored as float, and the bottom
// the unrounded top minus the image height (which 0x6472e0 takes from the corners 0x647050 left); each corner goes
// through fx or fy. UVs go from (0.5 / tw, 0.5 / th) top left to ((w − 0.5) / tw, (h − 0.5) / th) bottom right, or to
// w / tw and h / th on the changed side (resizedWidth, resizedHeight); without a slot, 0, as 0x647050 leaves them. With
// frameTop, 0x647560 shifts the left u to (frameLeft + 0.5) / tw and the top v to (frameTop + 0.5) / th; the right is
// that plus the image's u width, and the bottom the top plus its v height (stored as float).
[[nodiscard]] ENGINE_API QuadCorners UiImageCorners(const UiImage& image, Core::PixelSize screen);

// One screen axis's factor for a UI designed at `designSize` pixels: the size times the float inverse of `designSize`,
// like 0x65eeb0 with 800 and 600 (0x6ba884 and 0x6ba880).
[[nodiscard]] ENGINE_API float DesignScale(int screenSize, int designSize);
} // namespace Client::Render
