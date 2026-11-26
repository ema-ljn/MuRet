#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlFramebuffer.h"
#include "Render/Gl/GlRenderer.h"
#include "Render/Gl/GlTexture.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "support/GlTestContext.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

// Drawing and reading in the render_gl/ tests: a rectangle with the original's quad UV, the 64 × 64 framebuffer of
// question 9 of CL5's plan and the color of each cell of what is read, seen as a screen.
namespace Client::Render::Gl
{
// For GoogleTest's messages: (r, g, b, a).
inline void PrintTo(const Rgba8& color, std::ostream* out)
{
    *out << "(" << int{color.red} << ", " << int{color.green} << ", " << int{color.blue} << ", " << int{color.alpha}
         << ")";
}
} // namespace Client::Render::Gl

namespace Client::Tests
{
// 64 × 64, as question 9 says.
inline constexpr int FramebufferSide = 64;

// A screen rectangle in pixels, with y up (gluOrtho2D), and its UV: (u0, v0) top left and (u1, v1) bottom right, like
// 0x621b60's quad, which gives the v of the buffer's row 0 to the top edge (06 § Primitivas 2D). By default it covers
// the framebuffer with the whole texture, in white.
struct TestQuad
{
    float left = 0.0f;
    float bottom = 0.0f;
    float right = static_cast<float>(FramebufferSide);
    float top = static_cast<float>(FramebufferSide);
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    Render::Color color{};
};

// The original's fan (top left, bottom left, bottom right and top right) split into (0, 1, 2) and (0, 2, 3).
inline std::array<Render::SpriteVertex, 6> Triangles(const TestQuad& quad)
{
    const Render::SpriteVertex topLeft{quad.left, quad.top, quad.u0, quad.v0, quad.color};
    const Render::SpriteVertex bottomLeft{quad.left, quad.bottom, quad.u0, quad.v1, quad.color};
    const Render::SpriteVertex bottomRight{quad.right, quad.bottom, quad.u1, quad.v1, quad.color};
    const Render::SpriteVertex topRight{quad.right, quad.top, quad.u1, quad.v0, quad.color};
    return {topLeft, bottomLeft, bottomRight, topLeft, bottomRight, topRight};
}

// The 3D tests' camera, in the 640 × 480 rectangle `rect` of a `screen`-pixel screen: at the origin, unrotated (it
// looks toward −z with y up), 90° fov, near 1 and far 100 (71.4 · 1.4). At depth −d, the square from −d to d fills
// the viewport.
inline Render::Pass3D TestPass(Core::PixelSize screen, Render::ViewportRect rect = {0, 0, 640, 480})
{
    Render::Camera3D camera;
    camera.fieldOfView = 90.0f;
    camera.nearPlane = 1.0f;
    camera.farPlane = 100.0f / 1.4f;
    return Render::MakePass3D(screen, rect, camera);
}

// That square at depth −depth, with `color` on each vertex, in two triangles with `state`: counterclockwise seen from
// the camera (the front face), or clockwise if `backFacing`.
inline void AddSquare(Render::ModelBatch& batch, const Render::ModelDrawState& state, float depth, Render::Color color,
                      bool backFacing = false)
{
    std::array<glm::vec3, 4> corners{glm::vec3{-depth, -depth, -depth}, glm::vec3{depth, -depth, -depth},
                                     glm::vec3{depth, depth, -depth}, glm::vec3{-depth, depth, -depth}};
    if (backFacing)
    {
        std::swap(corners[1], corners[3]);
    }
    const auto vertex = [&](std::size_t k) { return Render::ModelVertex{corners[k], {0.0f, 0.0f}, color}; };
    batch.SetState(state);
    batch.Triangle({vertex(0), vertex(1), vertex(2)});
    batch.Triangle({vertex(0), vertex(2), vertex(3)});
}

// With y from the bottom, as glReadPixels gives it. Widened before multiplying.
inline std::size_t PixelIndex(int x, int y, int width)
{
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x);
}

// The color of the rectangle [x0, x1) × [y0, y1) of what was read (y from the bottom), if it is a single one.
inline std::optional<Render::Gl::Rgba8> UniformColor(const std::vector<Render::Gl::Rgba8>& pixels, int width, int x0,
                                                     int y0, int x1, int y1)
{
    const Render::Gl::Rgba8 first = pixels[PixelIndex(x0, y0, width)];
    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x1; ++x)
        {
            if (!(pixels[PixelIndex(x, y, width)] == first))
            {
                return std::nullopt;
            }
        }
    }
    return first;
}

// The color of each cell of a `columns` × `rows` grid over the framebuffer, seen as a screen: the top row first,
// though glReadPixels gives it last, and left to right. std::nullopt in a cell that is not a single color.
inline std::vector<std::optional<Render::Gl::Rgba8>> UniformCells(const Render::Gl::GlFramebuffer& framebuffer,
                                                                  int columns, int rows)
{
    const std::vector<Render::Gl::Rgba8> pixels = framebuffer.ReadPixels();
    const int width = framebuffer.Width();
    const int cellWidth = width / columns;
    const int cellHeight = framebuffer.Height() / rows;
    std::vector<std::optional<Render::Gl::Rgba8>> cells;
    for (int row = 0; row < rows; ++row)
    {
        const int y0 = (rows - 1 - row) * cellHeight;
        for (int column = 0; column < columns; ++column)
        {
            const int x0 = column * cellWidth;
            cells.push_back(UniformColor(pixels, width, x0, y0, x0 + cellWidth, y0 + cellHeight));
        }
    }
    return cells;
}

// The base of the tests that draw: GlTest's context, a bound 64 × 64 framebuffer and the 2D renderer.
class GlDrawTest : public GlTest
{
protected:
    void SetUp() override
    {
        GlTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        Core::Expected<Render::Gl::GlFramebuffer, std::string> framebuffer =
            Render::Gl::GlFramebuffer::Create(Gl(), FramebufferSide, FramebufferSide);
        ASSERT_TRUE(framebuffer.HasValue()) << framebuffer.Error();
        m_framebuffer.emplace(std::move(framebuffer).Value());
        Core::Expected<Render::Gl::GlRenderer, std::string> renderer = Render::Gl::GlRenderer::Create(Gl());
        ASSERT_TRUE(renderer.HasValue()) << renderer.Error();
        m_renderer.emplace(std::move(renderer).Value());
    }

    void Draw(const Render::Gl::GlTexture& texture, const TestQuad& quad)
    {
        const std::array<Render::SpriteVertex, 6> vertices = Triangles(quad);
        m_renderer->Draw(vertices, texture, m_framebuffer->Size());
        ASSERT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
    }

    [[nodiscard]] std::vector<std::optional<Render::Gl::Rgba8>> Cells(int columns, int rows) const
    {
        return UniformCells(*m_framebuffer, columns, rows);
    }

    std::optional<Render::Gl::GlFramebuffer> m_framebuffer;
    std::optional<Render::Gl::GlRenderer> m_renderer;
};
} // namespace Client::Tests
