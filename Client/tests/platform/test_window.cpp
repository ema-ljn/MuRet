#include "Core/Environment.h"
#include "Core/Expected.h"
#include "Platform/Sdl/SdlContext.h"
#include "Platform/Sdl/Window.h"
#include "support/GlTestContext.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

// Opens the window with its 3.3 core context, so it needs a display: it has the gl label and is skipped without one,
// unless MU_CLIENT_REQUIRE_GL=1 (like the render_gl/ tests, support/GlTestContext.h).
namespace
{
using Client::Platform::Sdl::SdlContext;
using Client::Platform::Sdl::Window;
using Client::Platform::Sdl::WindowOptions;

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

    // SDL goes first: the window is destroyed before SDL_Quit.
    std::unique_ptr<SdlContext> m_sdl;
    std::optional<Window> m_window;
};

// SDL does not send SDL_EVENT_TEXT_INPUT until SDL_StartTextInput (SDL_keyboard.h).
TEST_F(WindowTest, StartsAndStopsTheTextInput)
{
    ASSERT_TRUE(m_window->StartTextInput().HasValue());
    EXPECT_TRUE(m_window->TextInputActive());

    ASSERT_TRUE(m_window->StopTextInput().HasValue());
    EXPECT_FALSE(m_window->TextInputActive());
}

// Without SDL_WINDOW_HIGH_PIXEL_DENSITY, on Xvfb, 1.
TEST_F(WindowTest, HasAPositivePixelDensity)
{
    EXPECT_GT(m_window->PixelDensity(), 0.0f);
}
} // namespace
