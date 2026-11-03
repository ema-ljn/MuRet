#include "Render/Gl/GlFramebuffer.h"

#include "Core/Expected.h"
#include "Platform/GlLoader.h"
#include "support/GlDrawTest.h"
#include "support/GlTestContext.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

// Client::Render::Gl::GlFramebuffer: dónde dibujan los tests de render_gl/ y de dónde leen.
namespace
{
using Client::Render::Gl::GlFramebuffer;
using Client::Render::Gl::Rgba8;

using GlFramebufferTest = Client::Tests::GlTest;

// Un tamaño de cero o negativo no llega a GL: se prueba con una tabla vacía, sin contexto.
TEST(GlFramebufferRulesTest, RefusesAnEmptySize)
{
    const Client::Platform::GlFunctions noGl{};
    const Client::Core::Expected<GlFramebuffer, std::string> empty = GlFramebuffer::Create(noGl, 0, 64);
    ASSERT_FALSE(empty.HasValue());
    EXPECT_NE(empty.Error().find("0 × 64"), std::string::npos) << empty.Error();
    EXPECT_FALSE(GlFramebuffer::Create(noGl, 64, -1).HasValue());
}

// Lo que GL reserva sin datos no tiene un valor fijo: Create lo deja en (0, 0, 0, 0), y el test que
// dibuja sabe qué hay donde no dibujó.
TEST_F(GlFramebufferTest, StartsTransparentBlack)
{
    Client::Core::Expected<GlFramebuffer, std::string> framebuffer = GlFramebuffer::Create(Gl(), 64, 32);
    ASSERT_TRUE(framebuffer.HasValue()) << framebuffer.Error();
    EXPECT_EQ(framebuffer.Value().Width(), 64);
    EXPECT_EQ(framebuffer.Value().Height(), 32);
    EXPECT_EQ(Client::Tests::UniformCells(framebuffer.Value(), 1, 1),
              (std::vector<std::optional<Rgba8>>{Rgba8{0, 0, 0, 0}}));
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}
} // namespace
