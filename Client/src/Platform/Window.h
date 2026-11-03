#pragma once

#include "App/VSyncMode.h"
#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Platform/GlLoader.h"
#include "Platform/SdlContext.h"
#include "Platform/SwapInterval.h"

#include <SDL3/SDL_video.h>

#include <memory>
#include <string>

// La ventana del cliente con su contexto OpenGL 3.3 core (CL3.1, lo que en la CL0.3 era
// CreateGlWindow en main.cpp). El contexto es un recurso que vive dentro de la ventana: se crea
// después y se destruye antes.
namespace Client::Platform
{
// La ventana no cambia de tamaño, como la del original: su tamaño es una resolución que valga
// (App/Resolution.h), y main.cpp la mira antes de crearla.
struct WindowOptions
{
    // La resolución de referencia del original (D-C11 del roadmap del cliente).
    int width = 640;
    int height = 480;
    std::string title = "MuRet";
    App::VSyncMode vsync = App::VSyncMode::On;
};

struct WindowDeleter
{
    void operator()(SDL_Window* window) const;
};

struct GlContextDeleter
{
    void operator()(SDL_GLContextState* context) const;
};

class Window
{
public:
    // Necesita un SdlContext vivo. Crea la ventana, el contexto 3.3 core, carga GL y pide VSync.
    [[nodiscard]] static Core::Expected<Window, std::string> Create(const SdlContext& sdl,
                                                                    const WindowOptions& options);

    // Se mueve (Expected lo devuelve por valor) pero no se asigna: la asignación de miembros
    // destruiría la ventana vieja antes que su contexto.
    Window(Window&&) noexcept = default;
    Window& operator=(Window&&) = delete;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window() = default;

    void Swap();
    [[nodiscard]] App::PixelSize PixelSize() const;
    // El SDL_WindowID, para que PumpEvents descarte los eventos de otras ventanas.
    [[nodiscard]] SDL_WindowID Id() const;
    // El que quedó después de ApplySwapInterval.
    [[nodiscard]] App::VSyncMode VSync() const;
    // Por qué no quedó el pedido (el SDL_GetError()); vacío si quedó.
    [[nodiscard]] const std::string& VSyncError() const;
    // La tabla de funciones de GL del contexto de esta ventana.
    [[nodiscard]] const GlFunctions& Gl() const;
    // Cuántos píxeles del framebuffer mide cada coordenada de la ventana
    // (SDL_GetWindowPixelDensity), para el mouse de InputEventFor; 0 si SDL falla.
    [[nodiscard]] float PixelDensity() const;
    // Prende y apaga los SDL_EVENT_TEXT_INPUT, que SDL no manda si no se piden (CL5.5). En
    // algunas plataformas prender muestra un teclado en pantalla o el IME. El error trae el
    // SDL_GetError().
    [[nodiscard]] Core::Expected<void, std::string> StartTextInput();
    [[nodiscard]] Core::Expected<void, std::string> StopTextInput();
    [[nodiscard]] bool TextInputActive() const;

private:
    using WindowPtr = std::unique_ptr<SDL_Window, WindowDeleter>;
    using GlContextPtr = std::unique_ptr<SDL_GLContextState, GlContextDeleter>;

    Window(WindowPtr window, GlContextPtr context, const GlFunctions& gl, SwapIntervalResult vsync);

    // El contexto se declara después de la ventana: los miembros se destruyen al revés, así que el
    // contexto se libera antes que la ventana en la que vive.
    WindowPtr m_window;
    GlContextPtr m_context;
    GlFunctions m_gl;
    SwapIntervalResult m_vsync;
};
} // namespace Client::Platform
