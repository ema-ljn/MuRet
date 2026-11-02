#include "App/WindowState.h"

// Apply: la tabla de la CL3.1 (paso 1 del plan de la CL3). Cada evento toca un solo campo.
namespace Client::App
{
WindowState Apply(WindowState state, WindowEvent event, PixelSize size)
{
    switch (event)
    {
    case WindowEvent::CloseRequested:
        state.running = false;
        break;
    case WindowEvent::Minimized:
    case WindowEvent::Occluded:
        state.minimized = true;
        break;
    case WindowEvent::Restored:
    case WindowEvent::Exposed:
        state.minimized = false;
        break;
    case WindowEvent::FocusLost:
        state.focused = false;
        break;
    case WindowEvent::FocusGained:
        state.focused = true;
        break;
    case WindowEvent::PixelSizeChanged:
        // Con un lado en 0 o negativo no se toca: el viewport se queda con el último tamaño. SDL
        // 3.4 no manda un 0 × 0 al minimizar en Windows (sale antes, SDL_windowsevents.c), pero
        // el chequeo no depende de eso.
        if (size.width > 0 && size.height > 0)
        {
            state.pixelSize = size;
        }
        break;
    }
    return state;
}
} // namespace Client::App
