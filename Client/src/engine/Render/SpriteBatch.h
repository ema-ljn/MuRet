#pragma once

#include "Core/PixelSize.h"
#include "EngineApi.h"
#include "Render/OriginalQuad.h"
#include "Render/OriginalSkewedQuad.h"
#include "Render/QuadCorner.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// A frame's 2D as data, without GL (06 § Primitivas 2D). Consecutive quads with the same texture, blend and alpha test
// share a range; nothing is reordered: with the normal blend, what comes later covers what came before. The GL state
// the primitives change (glColor, the blend, GL_TEXTURE_2D, the alpha test) is left as the original would leave it,
// and Begin does not reset it: as in the original, the state carries over from one frame to the next.
namespace Client::Render
{
enum class BlendMode : std::uint8_t
{
    // 0x620a00: (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA), and turns the alpha test on.
    Alpha,
    // 0x620a90: (GL_ONE, GL_ONE), and turns the alpha test off.
    Additive,
    // 0x620980: no blend (glDisable(GL_BLEND)), no alpha test, texture on: what is drawn covers what is below with its
    // color, ignoring alpha. The game's bottom bar (0x5a9290).
    Opaque,
};

struct DrawCommand
{
    // None for 0x621aa0's untextured quad, an image without a slot, or a quad that came with the texture off (Q-C75).
    std::optional<TextureSlot> texture;
    BlendMode blend = BlendMode::Alpha;
    // The camera's glAlphaFunc(GL_GREATER, 0.25) (06 § Cámara): with it, alpha of 0.25 or less is not drawn.
    bool alphaTest = true;
    std::size_t firstVertex = 0;
    // In sixes: two triangles per quad.
    std::size_t vertexCount = 0;
};

class ENGINE_API SpriteBatch
{
public:
    // Like 0x6219b0: empties the vertices and ranges, and does not touch the GL state it tracks.
    void Begin(Core::PixelSize screen);

    // 0x620a00(1), 0x620a90 or 0x620980: the blend, and with it texture on and the alpha test on (Alpha) or off
    // (Additive and Opaque). Runs even if the blend is the same, like the original.
    void SetBlend(BlendMode blend);

    // 0x620c10: 0x620a00's blend (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) with texture on but the alpha test off, so low
    // alpha also blends. The panels' dialog box (0x6576a0). Blend() returns Alpha.
    void SetBlendWithoutAlphaTest();

    // The blend the last SetBlend left: the one 0x7d366dc stores and the HUD help (0x554340) sets back.
    [[nodiscard]] BlendMode Blend() const;

    // A bare glEnable(GL_TEXTURE_2D), not touching the blend or the alpha test: the HUD help (0x5544ed) before its
    // text, after its untextured quads.
    void EnableTexture();

    // glColor4f: the color of the following vertices, clamped to [0, 1] as the fixed function uses it (06 § Primitivas
    // 2D).
    void SetColor(Color color);

    // 0x621b60 (OriginalQuad.h).
    void Quad(const OriginalQuad& quad);

    // 0x622000 (OriginalSkewedQuad.h), with the current color.
    void SkewedQuad(const OriginalSkewedQuad& quad);

    // 0x621aa0(x, y, w, h): the rectangle in 640 × 480, always scaled, in the current color and untextured. First, like
    // 0x620910(0), it turns texture and alpha test off, and they stay off (Q-C75).
    void ColorQuad(float x, float y, float width, float height);

    // 0x647650 (UiImage.h): sets its color as GL's and turns the texture on if it has a slot, off if not (Q-C75); the
    // blend and the alpha test stay as they were.
    void Image(const UiImage& image);

    // 0x621cc0(slot, x, y, w, h, degrees): the `width` × `height` box of 640 × 480, always scaled, centered at (x, y)
    // and rotated `degrees` about the screen (0x5fc8f0 with (0, 0, degrees) and 0x5fcbd0), with UVs (0, 0), (0, 1), (1,
    // 1) and (1, 0) and the current color. The spinning repair cursor (0x5ac2d0).
    void RotatedQuad(TextureSlot slot, float x, float y, float width, float height, float degrees);

    [[nodiscard]] Core::PixelSize Screen() const;
    [[nodiscard]] std::span<const SpriteVertex> Vertices() const;
    [[nodiscard]] std::span<const DrawCommand> Commands() const;

private:
    // The two triangles of `corners`, in `texture`'s range (if the texture is on) with the current blend and alpha
    // test.
    void Append(const QuadCorners& corners, const std::array<Color, 4>& colors, std::optional<TextureSlot> texture);

    Core::PixelSize m_screen;
    // What 0x620a00(1) with glColor4f(1, 1, 1, 1) leaves, as the login starts drawing (06 § El login, el dibujo):
    // scenes set their own.
    Color m_color{};
    BlendMode m_blend = BlendMode::Alpha;
    bool m_textureOn = true;
    bool m_alphaTest = true;
    std::vector<SpriteVertex> m_vertices;
    std::vector<DrawCommand> m_commands;
};
} // namespace Client::Render
