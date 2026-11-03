#pragma once

#include "Core/Expected.h"
#include "Data/Textures/Texture.h"
#include "Platform/GlLoader.h"
#include "Platform/SdlContext.h"
#include "Platform/Window.h"

#include <SDL3/SDL_video.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>

// Una ventana escondida con un contexto OpenGL 3.3 core, para los tests de render_gl/ (pregunta 9
// del plan de la CL5): dibujan en un framebuffer y lo leen con glReadPixels. Sale de la
// HiddenGlWindow del lab de la CL5.0 (labs/cl5.0-pipeline/gl/). No se muestra nunca: lo que se
// dibuja va al framebuffer del test, no a la ventana.
//
// Usa lo que ya tiene el cliente: SdlContext para SDL_Init y SDL_Quit, los deleters de la ventana y
// del contexto de Platform/Window.h y LoadGlFunctions, que mira la versión y el perfil antes de
// pedir el resto. No usa Window::Create porque esa ventana se muestra, es de 640 × 480 y pide
// VSync, que un test no necesita.
namespace Client::Tests
{
// Con "1", un test que no puede abrir OpenGL falla en vez de saltearse: el paso de Xvfb del CI la
// pone, para que un Xvfb mal armado no deje los tests salteados en silencio.
inline constexpr const char* RequireGlVariable = "MURET_CLIENT_REQUIRE_GL";

// La regla, separada de la lectura del entorno para probarla: solo "1" pide GL.
[[nodiscard]] bool IsGlRequired(const std::optional<std::string>& value);

// Una textura RGBA de (GL_MAX_TEXTURE_SIZE + 1) × 1 con su buffer entero: el buffer está bien, pero
// glTexImage2D la rechaza con GL_INVALID_VALUE. Es la forma de que el driver diga que no sin
// quedarse sin memoria de verdad (la auditoría del 2026-11-03, AUD-C03). No usa TextureSide, que
// no pasa de 512.
[[nodiscard]] Data::Textures::Texture TextureOverTheGlLimit(const Platform::GlFunctions& gl);

class GlTestContext
{
public:
    // SDL_Init, la ventana escondida y su contexto 3.3 core, que queda como el actual de este hilo,
    // con la tabla de GL. El error dice qué faltó: la pantalla, el contexto, la versión, el perfil
    // o una función. Va en un unique_ptr: los nombres de GL (Render::Gl::GlName) guardan un puntero
    // a Gl(), así que el contexto no puede cambiar de dirección.
    [[nodiscard]] static Core::Expected<std::unique_ptr<GlTestContext>, std::string> Create();

    // Ni se copia ni se mueve: moverlo dejaría colgados los punteros a m_gl.
    GlTestContext(const GlTestContext&) = delete;
    GlTestContext& operator=(const GlTestContext&) = delete;
    GlTestContext(GlTestContext&&) = delete;
    GlTestContext& operator=(GlTestContext&&) = delete;
    ~GlTestContext() = default;

    [[nodiscard]] const Platform::GlFunctions& Gl() const;

private:
    using SdlPtr = std::unique_ptr<Platform::SdlContext>;
    using WindowPtr = std::unique_ptr<SDL_Window, Platform::WindowDeleter>;
    using GlContextPtr = std::unique_ptr<SDL_GLContextState, Platform::GlContextDeleter>;

    GlTestContext(SdlPtr sdl, WindowPtr window, GlContextPtr context, const Platform::GlFunctions& gl);

    // Los miembros se destruyen al revés de como se declaran: el contexto antes que la ventana en
    // la que vive, y la ventana antes que el SDL_Quit.
    SdlPtr m_sdl;
    WindowPtr m_window;
    GlContextPtr m_context;
    Platform::GlFunctions m_gl;
};

// La base de los tests que abren GL. SetUp() abre el contexto; si no puede, saltea el test o, con
// MURET_CLIENT_REQUIRE_GL=1, lo hace fallar con el motivo.
class GlTest : public ::testing::Test
{
protected:
    void SetUp() override;

    [[nodiscard]] const Platform::GlFunctions& Gl() const;

    // El contexto va primero: lo que el test cree después (texturas, framebuffers, el renderer) se
    // destruye antes, con el contexto vivo.
    std::unique_ptr<GlTestContext> m_context;
};
} // namespace Client::Tests
