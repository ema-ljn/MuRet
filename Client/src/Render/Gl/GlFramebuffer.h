#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlName.h"

#include <cstdint>
#include <string>
#include <vector>

// Un framebuffer para dibujar sin pantalla y leer lo dibujado, el de los tests de render_gl/
// (CL5.1; en el lab de la CL5.0, LabFramebuffer): una textura GL_RGBA8 como color y una
// GL_DEPTH_COMPONENT16 como profundidad (CL6.3: los 16 bits que pide el original, cDepthBits en
// 0x4b8e10, y la ventana), atadas a un framebuffer object. Lo que se dibuja con él atado va a la
// textura, y glReadPixels lo lee de ahí. Se mueve y no se copia, como sus GlName, y necesita el
// contexto vivo.
namespace Client::Render::Gl
{
// Un píxel leído con glReadPixels en GL_RGBA y GL_UNSIGNED_BYTE.
struct Rgba8
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 0;

    friend bool operator==(const Rgba8&, const Rgba8&) = default;
};

class GlFramebuffer
{
public:
    // Crea el framebuffer de `width` × `height`, lo deja atado, en (0, 0, 0, 0) y con la profundidad
    // en 1: lo que GL reserva sin datos no tiene un valor fijo. Un lado de 0 o menos no llega a GL.
    [[nodiscard]] static Core::Expected<GlFramebuffer, std::string> Create(const Platform::GlFunctions& gl, int width,
                                                                           int height);

    [[nodiscard]] int Width() const;
    [[nodiscard]] int Height() const;
    // El tamaño, como la pantalla que recibe GlRenderer::Draw.
    [[nodiscard]] App::PixelSize Size() const;

    // glReadPixels de todo el framebuffer: las filas van desde la de abajo, como las da GL.
    [[nodiscard]] std::vector<Rgba8> ReadPixels() const;

private:
    GlFramebuffer(const Platform::GlFunctions& gl, GlName color, GlName depth, GlName framebuffer, int width,
                  int height);

    const Platform::GlFunctions* m_gl;
    GlName m_color;
    GlName m_depth;
    GlName m_framebuffer;
    int m_width;
    int m_height;
};
} // namespace Client::Render::Gl
