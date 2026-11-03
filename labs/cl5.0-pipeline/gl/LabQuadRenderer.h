#pragma once

#include "GlName.h"
#include "LabGl.h"
#include "OriginalQuad.h"
#include "Ortho.h"

#include "Core/Expected.h"

#include <array>
#include <string>

// El pipeline programable del 2D, lo mínimo para dibujar un quad del original (CL5.0, paso 6): un
// vertex shader que lleva cada vértice al clip con la matriz, un fragment shader que pinta con la
// textura, un VAO con el formato de QuadVertex, el VBO que se llena en cada Draw y el EBO con los
// índices de FanToTriangles(4). Es lo que en la función fija eran glBegin(GL_TRIANGLE_FAN), cuatro
// glTexCoord2f y glVertex2f, glEnd y la matriz de gluOrtho2D (06).
namespace Labs
{
class LabQuadRenderer
{
public:
    // Compila y enlaza el shader y arma el VAO. El error trae el log del compilador de GLSL.
    [[nodiscard]] static Client::Core::Expected<LabQuadRenderer, std::string> Create(const LabGl& gl);

    // Dibuja los cuatro vértices de FanVertices como dos triángulos, con la textura y la matriz.
    // No toca la mezcla: la que esté prendida es la que vale.
    void Draw(const std::array<QuadVertex, 4>& vertices, GLuint texture, const Matrix4& projection) const;

private:
    LabQuadRenderer(const LabGl& gl, GlName program, GlName vertexArray, GlName vertexBuffer, GlName indexBuffer,
                    GLint projectionLocation);

    const LabGl* m_gl;
    GlName m_program;
    GlName m_vertexArray;
    GlName m_vertexBuffer;
    GlName m_indexBuffer;
    GLint m_projectionLocation;
};
} // namespace Labs
