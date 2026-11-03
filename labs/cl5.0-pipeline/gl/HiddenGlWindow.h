#pragma once

#include "LabGl.h"

#include "Core/Expected.h"
#include "Platform/SdlContext.h"
#include "Platform/Window.h"

#include <SDL3/SDL_video.h>

#include <memory>
#include <optional>
#include <string>

// Una ventana escondida con un contexto OpenGL 3.3 core, para dibujar en un framebuffer y leerlo
// con glReadPixels (CL5.0, paso 6; la pregunta 9 del plan de la CL5). No se muestra nunca: lo que
// se dibuja va al framebuffer del test, no a la ventana.
//
// Usa lo que ya tiene el cliente: SdlContext para SDL_Init y SDL_Quit, los deleters de la ventana y
// del contexto de Platform/Window.h, LoadGlFunctions (las cinco de la CL0.3, que alcanzan para
// preguntar la versión) y los chequeos de la versión y del perfil. Recién con un 3.3 core pide el
// resto, con LoadLabGl. No usa Window::Create porque esa ventana se muestra, es de 640 × 480 y pide
// VSync, que un test no necesita.
namespace Labs
{
// Con "1", un test que no puede abrir OpenGL falla en vez de saltearse: el paso de Xvfb del CI la
// pone, para que un Xvfb mal armado no deje los tests salteados en silencio.
inline constexpr const char* RequireGlVariable = "MURET_CLIENT_REQUIRE_GL";

// La regla, separada de la lectura del entorno para probarla: solo "1" pide GL.
[[nodiscard]] bool IsGlRequired(const std::optional<std::string>& value);

// El valor de MURET_CLIENT_REQUIRE_GL, o std::nullopt si no está.
[[nodiscard]] std::optional<std::string> ReadRequireGl();

class HiddenGlWindow
{
public:
    // SDL_Init, la ventana escondida y su contexto 3.3 core, que queda como el actual de este hilo,
    // con las funciones del lab. El error dice qué faltó: la pantalla, el contexto, la versión, el
    // perfil o una función.
    [[nodiscard]] static Client::Core::Expected<HiddenGlWindow, std::string> Create();

    // Se mueve (Expected la devuelve por valor) pero no se asigna, como Platform::Window.
    HiddenGlWindow(HiddenGlWindow&&) noexcept = default;
    HiddenGlWindow& operator=(HiddenGlWindow&&) = delete;
    HiddenGlWindow(const HiddenGlWindow&) = delete;
    HiddenGlWindow& operator=(const HiddenGlWindow&) = delete;
    ~HiddenGlWindow() = default;

    [[nodiscard]] const LabGl& Gl() const;

private:
    using SdlPtr = std::unique_ptr<Client::Platform::SdlContext>;
    using WindowPtr = std::unique_ptr<SDL_Window, Client::Platform::WindowDeleter>;
    using GlContextPtr = std::unique_ptr<SDL_GLContextState, Client::Platform::GlContextDeleter>;

    HiddenGlWindow(SdlPtr sdl, WindowPtr window, GlContextPtr context, const LabGl& gl);

    // Los miembros se destruyen al revés de como se declaran: el contexto antes que la ventana en
    // la que vive, y la ventana antes que el SDL_Quit.
    SdlPtr m_sdl;
    WindowPtr m_window;
    GlContextPtr m_context;
    LabGl m_gl;
};
} // namespace Labs
