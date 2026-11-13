#pragma once

#include "App/WindowState.h"
#include "Platform/GlLoader.h"
#include "Scenes/Screenshots.h"

#include <cstdint>
#include <vector>

// La lectura de la captura de pantalla (CL8 hito 6; 0x6202e0): lo dibujado en el framebuffer de la ventana, el de
// atrás, después del dibujo del frame y antes del swap. Necesita el contexto vivo.
namespace Client::Render::Gl
{
class GlScreenReader final : public Scenes::IScreenReader
{
public:
    // La tabla de GL vive más.
    explicit GlScreenReader(const Platform::GlFunctions& gl);

    // 0x62031f: glReadPixels(0, 0, ancho, alto, GL_RGB, GL_UNSIGNED_BYTE) a un búfer de ancho × alto × 3. El original
    // no toca GL_PACK_ALIGNMENT, que GL trae en 4: con un ancho que no es múltiplo de 4, GL escribiría filas con
    // relleno y pasaría el final del búfer. MuRet lo pone en 1 mientras lee y lo deja como estaba; con los anchos del
    // original, múltiplos de 4, los bytes son los mismos (Q-C631).
    [[nodiscard]] std::vector<std::uint8_t> ReadTheScreen(App::PixelSize screen) override;

private:
    const Platform::GlFunctions* m_gl;
};
} // namespace Client::Render::Gl
