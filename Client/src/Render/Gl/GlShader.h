#pragma once

#include "Core/Expected.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlName.h"

#include <string>
#include <string_view>

// Un programa de GLSL: un vertex shader y un fragment shader compilados y enlazados (CL5.1; en el
// lab de la CL5.0, el Compile y el Link de LabQuadRenderer.cpp). Es lo que reemplaza a la función
// fija del original. Se mueve y no se copia, como su GlName, y necesita el contexto vivo.
namespace Client::Render::Gl
{
class GlShader
{
public:
    // Compila los dos y los enlaza. El error dice cuál no compiló, o que no enlazó, con el log que
    // da GL (glGetShaderInfoLog o glGetProgramInfoLog). Los shaders se borran al terminar: el
    // programa ya tiene lo suyo.
    [[nodiscard]] static Core::Expected<GlShader, std::string>
    Create(const Platform::GlFunctions& gl, std::string_view vertexSource, std::string_view fragmentSource);

    [[nodiscard]] GLuint Program() const;

    // glGetUniformLocation: -1 si el programa no tiene ese uniform (o si el compilador lo sacó
    // porque nadie lo usa).
    [[nodiscard]] GLint UniformLocation(const char* name) const;

private:
    GlShader(const Platform::GlFunctions& gl, GlName program);

    const Platform::GlFunctions* m_gl;
    GlName m_program;
};
} // namespace Client::Render::Gl
