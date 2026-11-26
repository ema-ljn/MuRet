#pragma once

#include "Core/Expected.h"
#include "Data/Textures/Texture.h"
#include "Platform/Sdl/GlLoader.h"
#include "Platform/Sdl/SdlContext.h"
#include "Platform/Sdl/Window.h"

#include <SDL3/SDL_video.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>

// A hidden window with an OpenGL 3.3 core context, for the render_gl/ tests (question 9 of CL5's plan): they draw into
// a framebuffer and read it with glReadPixels. It is never shown. It does not use Window::Create because that window
// is shown, is 640 × 480 and asks for VSync, which a test does not need.
namespace Client::Tests
{
// With "1", a test that cannot open OpenGL fails instead of skipping: the CI's Xvfb step sets it, so that a broken
// Xvfb does not leave the tests silently skipped.
inline constexpr const char* RequireGlVariable = "MU_CLIENT_REQUIRE_GL";

// Only "1" asks for GL; separate from reading the environment so it can be tested.
[[nodiscard]] bool IsGlRequired(const std::optional<std::string>& value);

// An RGBA texture of (GL_MAX_TEXTURE_SIZE + 1) × 1 with its whole buffer: the buffer is fine, but glTexImage2D rejects
// it with GL_INVALID_VALUE. It makes the driver say no without really running out of memory (AUD-C03). It does not
// use TextureSide, which does not go past 512.
[[nodiscard]] Data::Textures::Texture TextureOverTheGlLimit(const Platform::Sdl::GlFunctions& gl);

class GlTestContext
{
public:
    // SDL_Init, the hidden window and its 3.3 core context, left current on this thread, with GL's table. The error
    // says what was missing: the display, the context, the version, the profile or a function. In a unique_ptr because
    // GL names (Render::Gl::GlName) keep a pointer to Gl(), so the context cannot change address.
    [[nodiscard]] static Core::Expected<std::unique_ptr<GlTestContext>, std::string> Create();

    // Neither copied nor moved: moving it would leave the pointers to m_gl dangling.
    GlTestContext(const GlTestContext&) = delete;
    GlTestContext& operator=(const GlTestContext&) = delete;
    GlTestContext(GlTestContext&&) = delete;
    GlTestContext& operator=(GlTestContext&&) = delete;
    ~GlTestContext() = default;

    [[nodiscard]] const Platform::Sdl::GlFunctions& Gl() const;

private:
    using SdlPtr = std::unique_ptr<Platform::Sdl::SdlContext>;
    using WindowPtr = std::unique_ptr<SDL_Window, Platform::Sdl::WindowDeleter>;
    using GlContextPtr = std::unique_ptr<SDL_GLContextState, Platform::Sdl::GlContextDeleter>;

    GlTestContext(SdlPtr sdl, WindowPtr window, GlContextPtr context, const Platform::Sdl::GlFunctions& gl);

    // Members are destroyed in reverse declaration order: the context before the window it lives in, and the window
    // before SDL_Quit.
    SdlPtr m_sdl;
    WindowPtr m_window;
    GlContextPtr m_context;
    Platform::Sdl::GlFunctions m_gl;
};

// The base of the tests that open GL. SetUp() opens the context; if it cannot, it skips the test or, with
// MU_CLIENT_REQUIRE_GL=1, fails it with the reason.
class GlTest : public ::testing::Test
{
protected:
    void SetUp() override;

    [[nodiscard]] const Platform::Sdl::GlFunctions& Gl() const;

    // The context goes first: what the test creates later (textures, framebuffers, the renderer) is destroyed before
    // it, with the context alive.
    std::unique_ptr<GlTestContext> m_context;
};
} // namespace Client::Tests
