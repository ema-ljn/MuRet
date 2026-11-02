#pragma once

#include <cstdint>

// El estado de la ventana que mira el bucle (CL3.1): si sigue abierta, si se dibuja y su tamaño.
// Platform traduce los eventos de SDL a WindowEvent, y Apply, que es pura, dice qué cambia cada
// uno: así se prueba sin ventana.
namespace Client::App
{
enum class WindowEvent : std::uint8_t
{
    // SDL_EVENT_QUIT o SDL_EVENT_WINDOW_CLOSE_REQUESTED: el WM_DESTROY del original.
    CloseRequested,
    Minimized,
    Restored,
    // Tapada por otra ventana: no se dibuja, igual que minimizada.
    Occluded,
    Exposed,
    FocusLost,
    FocusGained,
    // Cambió el tamaño en píxeles del área de dibujo: el viewport la sigue.
    PixelSizeChanged,
};

// El tamaño del área de dibujo, en píxeles.
struct PixelSize
{
    int width = 0;
    int height = 0;
};

struct WindowState
{
    bool running = true;
    // También cuando está tapada: no se dibuja, y la lógica sigue (CL3.2).
    bool minimized = false;
    bool focused = true;
    PixelSize pixelSize;
};

// Lo que cambia `event` en `state`. `size` solo cuenta con PixelSizeChanged, y uno con un lado en 0
// (en Windows llega un 0 × 0 al minimizar) no toca el tamaño.
[[nodiscard]] WindowState Apply(WindowState state, WindowEvent event, PixelSize size = {});
} // namespace Client::App
