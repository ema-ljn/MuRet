#include "Render/Gl/GlShader.h"

#include "Core/Expected.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/Shaders.h"
#include "support/GlTestContext.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

// The log is the driver's: the tests check whose it is, not what it says.
namespace
{
using Client::Render::Gl::GlShader;
using Client::Render::Gl::SpriteFragmentShader;
using Client::Render::Gl::SpriteVertexShader;

using GlShaderTest = Client::Tests::GlTest;

// Names a variable that does not exist: does not compile.
constexpr std::string_view BrokenVertexShader = R"(#version 330 core
void main()
{
    gl_Position = vec4(notDeclared, 0.0, 1.0);
}
)";

// Compiles and hands a vec2 to the next stage.
constexpr std::string_view HandoverVertexShader = R"(#version 330 core
out vec2 handover;
void main()
{
    handover = vec2(0.0, 1.0);
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
)";

// Reads the same output as a vec3: both compile, but the types differ and the program does not link.
constexpr std::string_view MismatchedFragmentShader = R"(#version 330 core
in vec3 handover;
out vec4 color;
void main()
{
    color = vec4(handover, 1.0);
}
)";

// The error starts with `prefix` and carries something after it: GL's log.
void ExpectLogAfter(const std::string& error, std::string_view prefix)
{
    EXPECT_TRUE(error.starts_with(prefix)) << error;
    EXPECT_GT(error.size(), prefix.size()) << error;
}

TEST_F(GlShaderTest, CompilesTheSpriteShader)
{
    const Client::Core::Expected<GlShader, std::string> shader =
        GlShader::Create(Gl(), SpriteVertexShader, SpriteFragmentShader);
    ASSERT_TRUE(shader.HasValue()) << shader.Error();
    EXPECT_NE(shader.Value().Program(), 0u);
    EXPECT_GE(shader.Value().UniformLocation(Client::Render::Gl::SpriteProjectionUniform), 0);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

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

TEST_F(GlShaderTest, ABadLinkSaysItsLog)
{
    const Client::Core::Expected<GlShader, std::string> shader =
        GlShader::Create(Gl(), HandoverVertexShader, MismatchedFragmentShader);
    ASSERT_FALSE(shader.HasValue());
    ExpectLogAfter(shader.Error(), "no enlazó el programa: ");
}
} // namespace
