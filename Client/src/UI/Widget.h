#pragma once

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Scenes/SceneTextures.h"

#include <functional>

// Lo que comparten las ventanas de la UI del login (CL5.5): los puntos y los rectángulos en
// píxeles del framebuffer, con la y desde arriba, como los guarda la UI nueva del original (06 §
// Las ventanas de la UI nueva); el mouse de la UI, y las imágenes en píxeles. Cada ventana se
// atiende en el paso, con los flancos de la CL3.3, y se dibuja en el frame (Q-C81).
namespace Client::UI
{
struct Point
{
    int x = 0;
    int y = 0;

    friend bool operator==(const Point&, const Point&) = default;
};

struct Rect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    // Como PtInRect: el borde de la izquierda y el de arriba son del rectángulo; el de la derecha
    // y el de abajo, no.
    [[nodiscard]] bool Contains(Point point) const;

    friend bool operator==(const Rect&, const Rect&) = default;
};

// El mouse de la UI nueva (0x640940, pregunta 12 del plan de la CL5): la última posición de
// `input`, que ya llega en píxeles del framebuffer, truncada al píxel y acotada a [0, W − 1] ×
// [0, H − 1]. Una que no es un número va a 0.
[[nodiscard]] Point MouseInPixels(const Input::InputState& input, App::PixelSize screen);

// El buffer de la textura de un slot, para las UV de las imágenes: en el cliente,
// SceneTextures::BufferSize de la escena.
using TextureSizes = std::function<Scenes::TextureBufferSize(Render::TextureSlot)>;

// Una imagen de una ventana: en píxeles (fx = fy = 1), con la textura de `slot` y su buffer.
[[nodiscard]] Render::UiImage PixelImage(Render::TextureSlot slot, Rect area, Scenes::TextureBufferSize buffer);
} // namespace Client::UI
