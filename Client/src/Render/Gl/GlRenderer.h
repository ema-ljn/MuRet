#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlName.h"
#include "Render/Gl/GlShader.h"
#include "Render/Gl/GlTexture.h"
#include "Render/SpriteVertex.h"

#include <cstddef>
#include <span>
#include <string>

// El pipeline del 2D (CL5.1): el shader de Shaders.h, un VAO con el formato de SpriteVertex y el
// VBO que se llena en cada dibujo. Es lo que en el original eran glBegin(GL_TRIANGLE_FAN), un
// glTexCoord2f y un glVertex2f por vértice, glEnd y la matriz de gluOrtho2D (06 § Primitivas 2D).
// Dibuja triángulos con una textura o sin ninguna, y con el alpha test o sin él; GlSpriteRenderer
// sube los vértices del SpriteBatch una vez por frame y dibuja cada tramo con DrawRange, con la
// mezcla de cada uno. Se mueve y no se copia, y necesita el contexto vivo.
namespace Client::Render::Gl
{
class GlRenderer
{
public:
    // Compila y enlaza el shader y arma el VAO. El error trae el log del compilador de GLSL.
    [[nodiscard]] static Core::Expected<GlRenderer, std::string> Create(const Platform::GlFunctions& gl);

    // Dibuja `vertices`, de a tres por triángulo, con `texture` y sin el alpha test, en una
    // pantalla de `screen` píxeles: Upload y un DrawRange de todos. No toca la mezcla.
    void Draw(std::span<const Render::SpriteVertex> vertices, const GlTexture& texture, App::PixelSize screen) const;

    // Sube `vertices` al VBO, que se llena de nuevo en cada llamada, para una pantalla de `screen`
    // píxeles con la y para arriba, como el 2D del original (0x6219b0: el viewport entero y
    // gluOrtho2D(0, W, 0, H)): pone ese viewport y la matriz de glm::ortho, y deja atados el
    // programa y el VAO para los DrawRange que siguen.
    void Upload(std::span<const Render::SpriteVertex> vertices, App::PixelSize screen) const;

    // Dibuja `vertexCount` vértices de lo subido, desde `firstVertex`, en el framebuffer que esté
    // atado: con `texture`, o sin muestrear ninguna si es nullptr, y con el alpha test o sin él.
    // No toca la mezcla.
    void DrawRange(std::size_t firstVertex, std::size_t vertexCount, const GlTexture* texture, bool alphaTest) const;

private:
    // Dónde están los uniforms del shader.
    struct Uniforms
    {
        GLint projection = -1;
        GLint textured = -1;
        GLint alphaTest = -1;
    };

    GlRenderer(const Platform::GlFunctions& gl, GlShader shader, GlName vertexArray, GlName vertexBuffer,
               Uniforms uniforms);

    const Platform::GlFunctions* m_gl;
    GlShader m_shader;
    GlName m_vertexArray;
    GlName m_vertexBuffer;
    Uniforms m_uniforms;
};
} // namespace Client::Render::Gl
