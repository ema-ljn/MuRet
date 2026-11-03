#include "LabGlTest.h"

#include "GlName.h"
#include "LabFramebuffer.h"
#include "LabQuadRenderer.h"
#include "LabTexture.h"

#include "Core/Expected.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

// El quad del original dibujado de verdad (CL5.0, paso 6): con una ventana escondida, un
// framebuffer de 64 × 64, el shader del 2D y glReadPixels. Y la mezcla de la actividad 12: con la
// normal el orden cambia lo que se ve, con la aditiva no.
namespace
{
using Labs::GlName;
using Labs::LabFramebuffer;
using Labs::LabQuadRenderer;
using Labs::Quadrants;
using Labs::Rgba;
using Labs::TextureImage;

using LabFramebufferTest = Labs::LabGlTest;

// Una textura de 2 × 2 con cuatro colores, en filas RGB de 6 bytes: la fila 0 es rojo y verde, la
// 1, azul y blanco. 6 no es múltiplo de 4: se sube con GL_UNPACK_ALIGNMENT en 1.
constexpr std::array<std::uint8_t, 12> FourColors{255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
constexpr Rgba Red{255, 0, 0, 255};
constexpr Rgba Green{0, 255, 0, 255};
constexpr Rgba Blue{0, 0, 255, 255};
constexpr Rgba White{255, 255, 255, 255};

// Dos colores con alfa 128 (0,502) que no se saturan al sumarlos.
constexpr std::array<std::uint8_t, 4> Orange{100, 50, 0, 128};
constexpr std::array<std::uint8_t, 4> Teal{0, 50, 100, 128};

// Lo que tiene que haber para dibujar: el framebuffer atado y el renderer.
struct Target
{
    LabFramebuffer framebuffer;
    LabQuadRenderer renderer;
};

// El framebuffer limpio, en negro opaco, y el renderer; con un fallo fatal si no se pueden armar.
std::optional<Target> MakeTarget(const Labs::LabGl& gl)
{
    Client::Core::Expected<LabFramebuffer, std::string> framebuffer =
        LabFramebuffer::Create(gl, Labs::FramebufferSide, Labs::FramebufferSide);
    if (!framebuffer.HasValue())
    {
        ADD_FAILURE() << framebuffer.Error();
        return std::nullopt;
    }
    Client::Core::Expected<LabQuadRenderer, std::string> renderer = LabQuadRenderer::Create(gl);
    if (!renderer.HasValue())
    {
        ADD_FAILURE() << renderer.Error();
        return std::nullopt;
    }
    gl.clearColor(0, 0, 0, 1);
    gl.clear(GL_COLOR_BUFFER_BIT);
    return Target{std::move(framebuffer).Value(), std::move(renderer).Value()};
}

// Una textura de 1 × 1 RGBA.
GlName OnePixel(const Labs::LabGl& gl, const std::array<std::uint8_t, 4>& color)
{
    Client::Core::Expected<GlName, std::string> texture =
        Labs::UploadTexture(gl, TextureImage{.width = 1, .height = 1, .channels = 4, .pixels = color}, 1);
    EXPECT_TRUE(texture.HasValue()) << (texture.HasValue() ? "" : texture.Error());
    return texture.HasValue() ? std::move(texture).Value() : GlName{};
}

// Dos quads que cubren todo, uno con `first` y otro con `second`, con la mezcla que diga
// `source`/`destination`. Devuelve el color de un píxel cualquiera (todos son iguales).
Rgba DrawTwo(const Labs::LabGl& gl, const Target& target, const GlName& first, const GlName& second, GLenum source,
             GLenum destination)
{
    gl.clear(GL_COLOR_BUFFER_BIT);
    gl.enable(GL_BLEND);
    gl.blendFunc(source, destination);
    target.renderer.Draw(Labs::FullFramebufferQuad(), first.Get(), Labs::FramebufferProjection());
    target.renderer.Draw(Labs::FullFramebufferQuad(), second.Get(), Labs::FramebufferProjection());
    gl.disable(GL_BLEND);
    return target.framebuffer.ReadPixels()[0];
}

// El paso 6 del plan: la textura de 2 × 2 en el quad que cubre el framebuffer, con GL_NEAREST, da
// un color por cuadrante, y el texel (0, 0), la primera fila del buffer, queda arriba a la
// izquierda, como en el original. Si algo diera vuelta la y otra vez (la matriz, las UV o las
// filas), el rojo quedaría abajo.
TEST_F(LabFramebufferTest, DrawsTheQuadIntoAFramebuffer)
{
    const std::optional<Target> target = MakeTarget(Gl());
    ASSERT_TRUE(target.has_value());
    Client::Core::Expected<GlName, std::string> texture =
        Labs::UploadTexture(Gl(), TextureImage{.width = 2, .height = 2, .channels = 3, .pixels = FourColors}, 1);
    ASSERT_TRUE(texture.HasValue()) << texture.Error();

    target->renderer.Draw(Labs::FullFramebufferQuad(), texture.Value().Get(), Labs::FramebufferProjection());
    ASSERT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));

    const Quadrants quadrants = Labs::ReadQuadrants(target->framebuffer);
    EXPECT_EQ(quadrants.topLeft, Red);
    EXPECT_EQ(quadrants.topRight, Green);
    EXPECT_EQ(quadrants.bottomLeft, Blue);
    EXPECT_EQ(quadrants.bottomRight, White);
}

// La actividad 12: con la mezcla normal (SRC_ALPHA, ONE_MINUS_SRC_ALPHA) cada quad tapa en parte al
// anterior, y el orden se ve: naranja y después verde azulado no es lo mismo que al revés. Con la
// aditiva (ONE, ONE) se suman, y la suma no depende del orden: (100, 100, 100) las dos veces.
TEST_F(LabFramebufferTest, TheOrderMattersOnlyWithNormalBlending)
{
    const std::optional<Target> target = MakeTarget(Gl());
    ASSERT_TRUE(target.has_value());
    const GlName orange = OnePixel(Gl(), Orange);
    const GlName teal = OnePixel(Gl(), Teal);

    const Rgba orangeThenTeal = DrawTwo(Gl(), *target, orange, teal, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const Rgba tealThenOrange = DrawTwo(Gl(), *target, teal, orange, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Naranja sobre negro deja (50, 25, 0); el verde azulado encima, la mitad de él más la mitad de
    // eso: unos (25, 37, 50). Al revés, unos (50, 37, 25). El redondeo a 8 bits puede mover uno.
    EXPECT_LE(std::abs(orangeThenTeal.red - 25), 1);
    EXPECT_LE(std::abs(orangeThenTeal.green - 37), 1);
    EXPECT_LE(std::abs(orangeThenTeal.blue - 50), 1);
    EXPECT_LE(std::abs(tealThenOrange.red - 50), 1);
    EXPECT_LE(std::abs(tealThenOrange.blue - 25), 1);
    EXPECT_NE(orangeThenTeal.red, tealThenOrange.red);

    const Rgba addedOneWay = DrawTwo(Gl(), *target, orange, teal, GL_ONE, GL_ONE);
    const Rgba addedTheOther = DrawTwo(Gl(), *target, teal, orange, GL_ONE, GL_ONE);
    EXPECT_EQ(addedOneWay, addedTheOther);
    EXPECT_EQ(addedOneWay.red, 100);
    EXPECT_EQ(addedOneWay.green, 100);
    EXPECT_EQ(addedOneWay.blue, 100);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}
} // namespace
