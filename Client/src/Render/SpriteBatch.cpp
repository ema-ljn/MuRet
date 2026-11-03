#include "Render/SpriteBatch.h"

#include <algorithm>

namespace Client::Render
{
namespace
{
// Un canal como lo usa la función fija: recortado a [0, 1].
float Clamped(float channel)
{
    return std::clamp(channel, 0.0f, 1.0f);
}

Color Clamped(Color color)
{
    return Color{.red = Clamped(color.red),
                 .green = Clamped(color.green),
                 .blue = Clamped(color.blue),
                 .alpha = Clamped(color.alpha)};
}

std::array<Color, 4> FourOf(Color color)
{
    return {color, color, color, color};
}
} // namespace

void SpriteBatch::Begin(App::PixelSize screen)
{
    m_screen = screen;
    m_vertices.clear();
    m_commands.clear();
}

void SpriteBatch::SetBlend(BlendMode blend)
{
    // 0x620a00 y 0x620a90 cambian la mezcla solo si es otra, pero el alpha test y GL_TEXTURE_2D los
    // ponen siempre.
    m_blend = blend;
    m_textureOn = true;
    m_alphaTest = blend == BlendMode::Alpha;
}

void SpriteBatch::SetColor(Color color)
{
    m_color = Clamped(color);
}

void SpriteBatch::Quad(const OriginalQuad& quad)
{
    // 0x621c3f..0x621ca4: con el alfa en más de 0, glColor4f(1, 1, 1, alfa) antes de cada vértice y
    // glColor4f(1, 1, 1, 1) después (Q-C57).
    if (quad.alpha > 0.0f)
    {
        const Color vertexColor = Clamped(Color{.alpha = quad.alpha});
        Append(FanCorners(quad, m_screen), FourOf(vertexColor), quad.slot);
        m_color = Color{};
        return;
    }
    Append(FanCorners(quad, m_screen), FourOf(m_color), quad.slot);
}

void SpriteBatch::SkewedQuad(const OriginalSkewedQuad& quad)
{
    Append(SkewedFanCorners(quad, m_screen), FourOf(m_color), quad.slot);
}

void SpriteBatch::ColorQuad(float x, float y, float width, float height)
{
    // 0x620910(0): sin textura y sin alpha test, hasta que alguien los vuelva a prender (Q-C75).
    m_textureOn = false;
    m_alphaTest = false;
    // 0x621aa0 hace las cuentas de 0x621b60 con las dos banderas, y no manda UV.
    const OriginalQuad rectangle{.x = x,
                                 .y = y,
                                 .width = width,
                                 .height = height,
                                 .du = 0.0f,
                                 .dv = 0.0f,
                                 .scaleSize = true,
                                 .scalePosition = true};
    Append(FanCorners(rectangle, m_screen), FourOf(m_color), std::nullopt);
}

void SpriteBatch::Image(const UiImage& image)
{
    // 0x647650: GL_TEXTURE_2D prendido con un slot y apagado sin él, y glColor4ub con el suyo, que
    // queda (Q-C75).
    m_textureOn = image.slot.has_value();
    m_color = Clamped(image.color);
    Append(UiImageCorners(image, m_screen), FourOf(m_color), image.slot);
}

App::PixelSize SpriteBatch::Screen() const
{
    return m_screen;
}

std::span<const SpriteVertex> SpriteBatch::Vertices() const
{
    return m_vertices;
}

std::span<const DrawCommand> SpriteBatch::Commands() const
{
    return m_commands;
}

void SpriteBatch::Append(const QuadCorners& corners, const std::array<Color, 4>& colors,
                         std::optional<TextureSlot> texture)
{
    // Con GL_TEXTURE_2D apagado, la textura atada no se usa (Q-C75).
    const std::optional<TextureSlot> used = m_textureOn ? texture : std::nullopt;
    const std::size_t first = m_vertices.size();
    // El fan (0, 1, 2, 3) son los triángulos (0, 1, 2) y (0, 2, 3).
    for (const std::size_t corner : {0u, 1u, 2u, 0u, 2u, 3u})
    {
        const QuadCorner& from = corners[corner];
        m_vertices.push_back(SpriteVertex{from.x, from.y, from.u, from.v, colors[corner]});
    }

    const bool continues = !m_commands.empty() && m_commands.back().texture == used &&
                           m_commands.back().blend == m_blend && m_commands.back().alphaTest == m_alphaTest;
    if (continues)
    {
        m_commands.back().vertexCount += 6;
        return;
    }
    m_commands.push_back(DrawCommand{
        .texture = used, .blend = m_blend, .alphaTest = m_alphaTest, .firstVertex = first, .vertexCount = 6});
}
} // namespace Client::Render
