#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlName.h"
#include "Render/Gl/GlShader.h"
#include "Render/Gl/GlTexture.h"
#include "Render/SpriteVertex.h"

#include <span>
#include <string>

// El pipeline del 2D (CL5.1): el shader de Shaders.h, un VAO con el formato de SpriteVertex y el
// VBO que se llena en cada dibujo. Es lo que en el original eran glBegin(GL_TRIANGLE_FAN), un
// glTexCoord2f y un glVertex2f por vértice, glEnd y la matriz de gluOrtho2D (06 § Primitivas 2D).
// Dibuja triángulos con una textura; el SpriteBatch de la CL5.2 arma los de cada quad del original
// y corre sus comandos acá, con la mezcla de cada uno. Se mueve y no se copia, y necesita el
// contexto vivo.
namespace Client::Render::Gl
{
class GlRenderer
{
public:
    // Compila y enlaza el shader y arma el VAO. El error trae el log del compilador de GLSL.
    [[nodiscard]] static Core::Expected<GlRenderer, std::string> Create(const Platform::GlFunctions& gl);

    // Dibuja `vertices`, de a tres por triángulo, con `texture`, en una pantalla de `screen`
    // píxeles con la y para arriba, como el 2D del original (0x6219b0: el viewport entero y
    // gluOrtho2D(0, W, 0, H)): pone ese viewport y la matriz de glm::ortho. Dibuja en el
    // framebuffer que esté atado y no toca la mezcla.
    void Draw(std::span<const Render::SpriteVertex> vertices, const GlTexture& texture, App::PixelSize screen) const;

private:
    GlRenderer(const Platform::GlFunctions& gl, GlShader shader, GlName vertexArray, GlName vertexBuffer,
               GLint projectionLocation);

    const Platform::GlFunctions* m_gl;
    GlShader m_shader;
    GlName m_vertexArray;
    GlName m_vertexBuffer;
    GLint m_projectionLocation;
};
} // namespace Client::Render::Gl
