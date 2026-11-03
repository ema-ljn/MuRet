#pragma once

#include "GlName.h"
#include "LabGl.h"

#include "Core/Expected.h"

#include <cstdint>
#include <string>
#include <vector>

// Un framebuffer para dibujar sin pantalla y leer lo dibujado (CL5.0, paso 6): una textura
// GL_RGBA8 como color, atada a un framebuffer object. Lo que se dibuja con él atado va a la
// textura, y glReadPixels lo lee de ahí.
namespace Labs
{
struct Rgba
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 0;

    friend bool operator==(const Rgba&, const Rgba&) = default;
};

class LabFramebuffer
{
public:
    // Crea el framebuffer de `width` × `height` y lo deja atado, con el viewport entero.
    [[nodiscard]] static Client::Core::Expected<LabFramebuffer, std::string> Create(const LabGl& gl, int width,
                                                                                    int height);

    [[nodiscard]] int Width() const;
    [[nodiscard]] int Height() const;

    // glReadPixels de todo el framebuffer: las filas van desde la de abajo, como las da GL.
    [[nodiscard]] std::vector<Rgba> ReadPixels() const;

private:
    LabFramebuffer(const LabGl& gl, GlName color, GlName framebuffer, int width, int height);

    const LabGl* m_gl;
    GlName m_color;
    GlName m_framebuffer;
    int m_width;
    int m_height;
};
} // namespace Labs
