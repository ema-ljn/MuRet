#include "Render/SpriteBatch.h"

#include "App/WindowState.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/OriginalQuad.h"
#include "Render/OriginalSkewedQuad.h"
#include "Render/QuadCorner.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace Client::Render
{
namespace
{
// El fan (0, 1, 2, 3) de un quad son los triángulos (0, 1, 2) y (0, 2, 3): seis vértices por quad.
constexpr std::array<std::size_t, 6> FanTriangles{0, 1, 2, 0, 2, 3};

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

float F32(double value)
{
    return static_cast<float>(value);
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

BlendMode SpriteBatch::Blend() const
{
    return m_blend;
}

void SpriteBatch::EnableTexture()
{
    m_textureOn = true;
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

void SpriteBatch::RotatedQuad(TextureSlot slot, float x, float y, float width, float height, float degrees)
{
    // 0x621cc0..0x621d31: el centro y el tamaño en píxeles, con la y dada vuelta (H − y en la x87).
    const float centerX = ScaleX(x, m_screen);
    const float centerY = F32(static_cast<double>(m_screen.height) - static_cast<double>(ScaleY(y, m_screen)));
    const float pixelWidth = ScaleX(width, m_screen);
    const float pixelHeight = ScaleY(height, m_screen);
    // 0x621d34..0x621d82: las esquinas (−w/2, h/2), (−w/2, −h/2), (w/2, −h/2) y (w/2, h/2), con el −0,5 de
    // 0x6ba298 y el 0,5 de 0x6b882c, y z en 0.
    const float left = F32(static_cast<double>(pixelWidth) * -0.5);
    const float right = F32(static_cast<double>(pixelWidth) * 0.5);
    const float top = F32(static_cast<double>(pixelHeight) * 0.5);
    const float bottom = F32(static_cast<double>(pixelHeight) * -0.5);
    const std::array<glm::vec3, 4> local{glm::vec3(left, top, 0.0f), glm::vec3(left, bottom, 0.0f),
                                         glm::vec3(right, bottom, 0.0f), glm::vec3(right, top, 0.0f)};
    constexpr std::array<std::array<float, 2>, 4> Uvs{{{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}}};
    const Models::OriginalMatrix spin = Models::OriginalAngleMatrix({0.0f, 0.0f, degrees});
    QuadCorners corners{};
    for (std::size_t k = 0; k < corners.size(); ++k)
    {
        // 0x621de3..0x621e01: cada esquina girada (0x5fcbd0), más el centro.
        const glm::vec3 rotated = Models::OriginalVectorRotate(local[k], spin);
        corners[k] = QuadCorner{.x = F32(static_cast<double>(centerX) + static_cast<double>(rotated.x)),
                                .y = F32(static_cast<double>(centerY) + static_cast<double>(rotated.y)),
                                .u = Uvs[k][0],
                                .v = Uvs[k][1]};
    }
    Append(corners, FourOf(m_color), slot);
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
    for (const std::size_t corner : FanTriangles)
    {
        const QuadCorner& from = corners[corner];
        m_vertices.push_back(SpriteVertex{from.x, from.y, from.u, from.v, colors[corner]});
    }

    const bool continues = !m_commands.empty() && m_commands.back().texture == used &&
                           m_commands.back().blend == m_blend && m_commands.back().alphaTest == m_alphaTest;
    if (continues)
    {
        m_commands.back().vertexCount += FanTriangles.size();
        return;
    }
    m_commands.push_back(DrawCommand{.texture = used,
                                     .blend = m_blend,
                                     .alphaTest = m_alphaTest,
                                     .firstVertex = first,
                                     .vertexCount = FanTriangles.size()});
}
} // namespace Client::Render
