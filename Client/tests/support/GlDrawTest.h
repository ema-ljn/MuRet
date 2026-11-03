#pragma once

#include "Core/Expected.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlFramebuffer.h"
#include "Render/Gl/GlRenderer.h"
#include "Render/Gl/GlTexture.h"
#include "Render/SpriteVertex.h"
#include "support/GlTestContext.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

// Dibujar y leer en los tests de render_gl/: un rectángulo con la UV del quad del original, el
// framebuffer de 64 × 64 de la pregunta 9 del plan de la CL5 y el color de cada celda de lo leído,
// mirado como una pantalla. Sale de LabGlTest.h, en el lab de la CL5.0.
namespace Client::Render::Gl
{
// Para los mensajes de GoogleTest: (r, g, b, a).
inline void PrintTo(const Rgba8& color, std::ostream* out)
{
    *out << "(" << int{color.red} << ", " << int{color.green} << ", " << int{color.blue} << ", " << int{color.alpha}
         << ")";
}
} // namespace Client::Render::Gl

namespace Client::Tests
{
// El framebuffer de los tests: 64 × 64, como dice la pregunta 9.
inline constexpr int FramebufferSide = 64;

// Un rectángulo de la pantalla en píxeles, con la y para arriba (gluOrtho2D), y su UV: (u0, v0)
// arriba a la izquierda y (u1, v1) abajo a la derecha, como el quad de 0x621b60, que le da la v de
// la fila 0 del buffer al borde de arriba (06 § Primitivas 2D). Por defecto cubre el framebuffer
// con la textura entera, en blanco.
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

// Los dos triángulos del rectángulo: el fan del original (arriba a la izquierda, abajo a la
// izquierda, abajo a la derecha y arriba a la derecha) partido en (0, 1, 2) y (0, 2, 3).
inline std::array<Render::SpriteVertex, 6> Triangles(const TestQuad& quad)
{
    const Render::SpriteVertex topLeft{quad.left, quad.top, quad.u0, quad.v0, quad.color};
    const Render::SpriteVertex bottomLeft{quad.left, quad.bottom, quad.u0, quad.v1, quad.color};
    const Render::SpriteVertex bottomRight{quad.right, quad.bottom, quad.u1, quad.v1, quad.color};
    const Render::SpriteVertex topRight{quad.right, quad.top, quad.u1, quad.v0, quad.color};
    return {topLeft, bottomLeft, bottomRight, topLeft, bottomRight, topRight};
}

// El índice del píxel (x, y) en lo leído, con la y desde abajo, como lo da glReadPixels. Se
// ensancha antes de multiplicar.
inline std::size_t PixelIndex(int x, int y, int width)
{
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x);
}

// El color del rectángulo [x0, x1) × [y0, y1) de lo leído (la y desde abajo), si es uno solo.
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

// El color de cada celda de una grilla de `columns` × `rows` sobre el framebuffer, mirado como una
// pantalla: primero la fila de arriba, aunque glReadPixels la dé última, y de izquierda a derecha.
// std::nullopt en una celda que no es de un solo color.
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

// La base de los tests que dibujan: con el contexto de GlTest, un framebuffer de 64 × 64 atado y
// el renderer del 2D.
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

    // Dibuja `quad` con `texture` en el framebuffer.
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
