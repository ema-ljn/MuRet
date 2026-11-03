#include "Render/Gl/GlShader.h"

#include "Core/Expected.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/Shaders.h"
#include "support/GlTestContext.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

// Client::Render::Gl::GlShader: compilar y enlazar el GLSL 330 del 2D (Shaders.h), y lo que dice
// cuando no compila o no enlaza. El log es el del driver: los tests miran de quién es, no qué dice.
namespace
{
using Client::Render::Gl::GlShader;
using Client::Render::Gl::SpriteFragmentShader;
using Client::Render::Gl::SpriteVertexShader;

using GlShaderTest = Client::Tests::GlTest;

// Un vertex shader que nombra una variable que no existe: no compila.
constexpr std::string_view BrokenVertexShader = R"(#version 330 core
void main()
{
    gl_Position = vec4(notDeclared, 0.0, 1.0);
}
)";

// Uno que compila y le pasa a la etapa siguiente un vec2.
constexpr std::string_view HandoverVertexShader = R"(#version 330 core
out vec2 handover;
void main()
{
    handover = vec2(0.0, 1.0);
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
)";

// Un fragment shader que lee la misma salida como un vec3: los dos compilan, pero los tipos no
// coinciden y el programa no enlaza.
constexpr std::string_view MismatchedFragmentShader = R"(#version 330 core
in vec3 handover;
out vec4 color;
void main()
{
    color = vec4(handover, 1.0);
}
)";

// El error empieza con `prefix` y trae algo después: el log de GL.
void ExpectLogAfter(const std::string& error, std::string_view prefix)
{
    EXPECT_TRUE(error.starts_with(prefix)) << error;
    EXPECT_GT(error.size(), prefix.size()) << error;
}

// El shader del 2D compila y enlaza, y tiene la matriz que le pone GlRenderer.
TEST_F(GlShaderTest, CompilesTheSpriteShader)
{
    const Client::Core::Expected<GlShader, std::string> shader =
        GlShader::Create(Gl(), SpriteVertexShader, SpriteFragmentShader);
    ASSERT_TRUE(shader.HasValue()) << shader.Error();
    EXPECT_NE(shader.Value().Program(), 0u);
    EXPECT_GE(shader.Value().UniformLocation(Client::Render::Gl::SpriteProjectionUniform), 0);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

// El error dice cuál de los dos no compiló y trae el log de glGetShaderInfoLog.
TEST_F(GlShaderTest, ABadShaderSaysItsLog)
{
    const Client::Core::Expected<GlShader, std::string> vertex =
        GlShader::Create(Gl(), BrokenVertexShader, SpriteFragmentShader);
    ASSERT_FALSE(vertex.HasValue());
    ExpectLogAfter(vertex.Error(), "no compiló el vertex shader: ");

    const Client::Core::Expected<GlShader, std::string> fragment =
        GlShader::Create(Gl(), SpriteVertexShader, BrokenVertexShader);
    ASSERT_FALSE(fragment.HasValue());
    ExpectLogAfter(fragment.Error(), "no compiló el fragment shader: ");
}

// Los dos compilan y no enlazan: el error trae el log de glGetProgramInfoLog.
TEST_F(GlShaderTest, ABadLinkSaysItsLog)
{
    const Client::Core::Expected<GlShader, std::string> shader =
        GlShader::Create(Gl(), HandoverVertexShader, MismatchedFragmentShader);
    ASSERT_FALSE(shader.HasValue());
    ExpectLogAfter(shader.Error(), "no enlazó el programa: ");
}
} // namespace
