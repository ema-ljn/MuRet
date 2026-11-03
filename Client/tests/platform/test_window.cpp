#include "Core/Environment.h"
#include "Core/Expected.h"
#include "Platform/SdlContext.h"
#include "Platform/Window.h"
#include "support/GlTestContext.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

// Client::Platform::Window: lo que se le pide a SDL de la ventana de verdad (CL5.5): el texto
// tecleado y la densidad de píxeles. Abre la ventana con su contexto 3.3 core, así que necesita
// pantalla: lleva la etiqueta gl y, sin pantalla, se saltea, salvo con MURET_CLIENT_REQUIRE_GL=1
// (como los de render_gl/, support/GlTestContext.h).
namespace
{
using Client::Platform::SdlContext;
using Client::Platform::Window;
using Client::Platform::WindowOptions;

class WindowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Client::Core::Expected<std::unique_ptr<SdlContext>, std::string> sdl = SdlContext::Create();
        std::string error = sdl.HasValue() ? std::string() : sdl.Error();
        if (sdl.HasValue())
        {
            m_sdl = std::move(sdl).Value();
            Client::Core::Expected<Window, std::string> window = Window::Create(*m_sdl, WindowOptions{});
            if (window.HasValue())
            {
                m_window.emplace(std::move(window).Value());
                return;
            }
            error = window.Error();
        }
        if (Client::Tests::IsGlRequired(Client::Core::ReadEnvironmentVariable(Client::Tests::RequireGlVariable)))
        {
            FAIL() << Client::Tests::RequireGlVariable << "=1 y no se pudo abrir la ventana: " << error;
        }
        GTEST_SKIP() << "sin ventana (" << error << "): test salteado; con " << Client::Tests::RequireGlVariable
                     << "=1 falla";
    }

    // SDL va primero: la ventana se destruye antes del SDL_Quit.
    std::unique_ptr<SdlContext> m_sdl;
    std::optional<Window> m_window;
};

// SDL no manda SDL_EVENT_TEXT_INPUT hasta SDL_StartTextInput (SDL_keyboard.h): la ventana lo
// prende y lo apaga.
TEST_F(WindowTest, StartsAndStopsTheTextInput)
{
    ASSERT_TRUE(m_window->StartTextInput().HasValue());
    EXPECT_TRUE(m_window->TextInputActive());

    ASSERT_TRUE(m_window->StopTextInput().HasValue());
    EXPECT_FALSE(m_window->TextInputActive());
}

// La densidad que InputEventFor usa para pasar el mouse a píxeles del framebuffer: sin
// SDL_WINDOW_HIGH_PIXEL_DENSITY, en Xvfb, 1.
TEST_F(WindowTest, HasAPositivePixelDensity)
{
    EXPECT_GT(m_window->PixelDensity(), 0.0f);
}
} // namespace
