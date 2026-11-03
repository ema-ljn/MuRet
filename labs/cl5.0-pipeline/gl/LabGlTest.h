#pragma once

#include "HiddenGlWindow.h"
#include "LabFramebuffer.h"
#include "LabGl.h"
#include "LabQuadRenderer.h"
#include "OriginalQuad.h"
#include "Ortho.h"

#include "Core/Expected.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

// Lo que comparten los tests de cl5.0-pipeline/gl/ que abren GL: la regla de la pregunta 9 del plan
// de la CL5 (sin GL se saltean, salvo con MURET_CLIENT_REQUIRE_GL=1) y lo que se dibuja y se lee.
namespace Labs
{
// Para los mensajes de GoogleTest: (r, g, b, a).
inline void PrintTo(const Rgba& color, std::ostream* out)
{
    *out << "(" << int{color.red} << ", " << int{color.green} << ", " << int{color.blue} << ", " << int{color.alpha}
         << ")";
}

// El framebuffer de los tests: 64 × 64, como dice la pregunta 9.
inline constexpr int FramebufferSide = 64;

// El quad que cubre el framebuffer entero, sin escalar: (0, 64), (0, 0), (64, 0) y (64, 64), con la
// UV (0, 0) arriba a la izquierda.
inline std::array<QuadVertex, 4> FullFramebufferQuad()
{
    const OriginalQuad quad{.x = 0, .y = 0, .width = FramebufferSide, .height = FramebufferSide};
    return FanVertices(quad, Screen{FramebufferSide, FramebufferSide});
}

// La gluOrtho2D del framebuffer, la de 0x6219b0 con W y H en 64.
inline Matrix4 FramebufferProjection()
{
    return Ortho2D(0, FramebufferSide, 0, FramebufferSide);
}

// Lo que pinta cada cuadrante del framebuffer, mirado como una pantalla: arriba es la fila de más
// arriba, aunque glReadPixels la dé última. std::nullopt si el cuadrante no es de un solo color.
struct Quadrants
{
    std::optional<Rgba> topLeft;
    std::optional<Rgba> topRight;
    std::optional<Rgba> bottomLeft;
    std::optional<Rgba> bottomRight;
};

// El color del rectángulo [x0, x1) × [y0, y1) de lo leído (la y desde abajo, como GL), si es uno
// solo.
inline std::optional<Rgba> UniformColor(const std::vector<Rgba>& pixels, int width, int x0, int y0, int x1, int y1)
{
    const Rgba first = pixels[static_cast<std::size_t>(y0 * width + x0)];
    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x1; ++x)
        {
            if (!(pixels[static_cast<std::size_t>(y * width + x)] == first))
            {
                return std::nullopt;
            }
        }
    }
    return first;
}

inline Quadrants ReadQuadrants(const LabFramebuffer& framebuffer)
{
    const std::vector<Rgba> pixels = framebuffer.ReadPixels();
    const int width = framebuffer.Width();
    const int height = framebuffer.Height();
    const int halfWidth = width / 2;
    const int halfHeight = height / 2;
    // Las filas de glReadPixels van desde abajo: la mitad de arriba de la pantalla es la segunda.
    return Quadrants{UniformColor(pixels, width, 0, halfHeight, halfWidth, height),
                     UniformColor(pixels, width, halfWidth, halfHeight, width, height),
                     UniformColor(pixels, width, 0, 0, halfWidth, halfHeight),
                     UniformColor(pixels, width, halfWidth, 0, width, halfHeight)};
}

// La base de los tests que abren GL. SetUp() abre la ventana escondida; si no puede, saltea el test
// o, con MURET_CLIENT_REQUIRE_GL=1, lo hace fallar con el motivo.
class LabGlTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Client::Core::Expected<HiddenGlWindow, std::string> window = HiddenGlWindow::Create();
        if (!window.HasValue())
        {
            if (IsGlRequired(ReadRequireGl()))
            {
                FAIL() << RequireGlVariable << "=1 y no se pudo abrir OpenGL 3.3 core: " << window.Error();
            }
            GTEST_SKIP() << "sin OpenGL 3.3 core (" << window.Error() << "): test salteado; con " << RequireGlVariable
                         << "=1 falla";
        }
        m_window.emplace(std::move(window).Value());
    }

    [[nodiscard]] const LabGl& Gl() const
    {
        return m_window->Gl();
    }

    // La ventana va primero: lo que el test cree después (texturas, framebuffers, el renderer) se
    // destruye antes, con el contexto vivo.
    std::optional<HiddenGlWindow> m_window;
};
} // namespace Labs
