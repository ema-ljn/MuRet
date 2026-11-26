#pragma once

#include "Platform/VSyncMode.h"
#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Platform/Sdl/GlLoader.h"
#include "Platform/Sdl/SdlContext.h"
#include "Platform/Sdl/SwapInterval.h"
#include "PlatformApi.h"

#include <SDL3/SDL_video.h>

#include <memory>
#include <string>

// The context is a resource that lives inside the window: created after it and destroyed before it.
namespace Client::Platform::Sdl
{
// The window does not resize, like the original's: its size is a valid resolution (App/Resolution.h), which main.cpp
// checks before creating it.
struct WindowOptions
{
    // The original's reference resolution (D-C11).
    int width = 640;
    int height = 480;
    std::string title = "MuRet";
    VSyncMode vsync = VSyncMode::On;
};

struct PLATFORM_API WindowDeleter
{
    void operator()(SDL_Window* window) const;
};

struct PLATFORM_API GlContextDeleter
{
    void operator()(SDL_GLContextState* context) const;
};

class PLATFORM_API Window
{
public:
    // Needs a live SdlContext. Creates the window and the 3.3 core context, loads GL and requests VSync.
    [[nodiscard]] static Core::Expected<Window, std::string> Create(const SdlContext& sdl,
                                                                    const WindowOptions& options);

    // Movable (Expected returns it by value) but not assignable: memberwise assignment would destroy the old window
    // before its context.
    Window(Window&&) noexcept = default;
    Window& operator=(Window&&) = delete;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window() = default;

    // Native modal; returns once the notice is confirmed. The error keeps SDL_GetError().
    [[nodiscard]] Core::Expected<void, std::string> ShowErrorMessage(const std::string& message);
    void Swap();
    [[nodiscard]] Core::PixelSize PixelSize() const;
    // So PumpEvents drops other windows' events.
    [[nodiscard]] SDL_WindowID Id() const;
    // What stuck after ApplySwapInterval.
    [[nodiscard]] VSyncMode VSync() const;
    // Why the request did not stick (the SDL_GetError()); empty if it did.
    [[nodiscard]] const std::string& VSyncError() const;
    [[nodiscard]] const GlFunctions& Gl() const;
    // Framebuffer pixels per window coordinate (SDL_GetWindowPixelDensity), for InputEventFor's mouse; 0 if SDL fails.
    [[nodiscard]] float PixelDensity() const;
    // SDL does not send SDL_EVENT_TEXT_INPUT unless asked. On some platforms starting it shows an on-screen keyboard or
    // the IME. The error carries SDL_GetError().
    [[nodiscard]] Core::Expected<void, std::string> StartTextInput();
    [[nodiscard]] Core::Expected<void, std::string> StopTextInput();
    [[nodiscard]] bool TextInputActive() const;
    // The game draws its own (UI/GameCursor.h), like the original's ShowCursor(FALSE) (0x4ba238 and 0x4b3327). The
    // error carries SDL_GetError().
    [[nodiscard]] Core::Expected<void, std::string> HideSystemCursor();

private:
    using WindowPtr = std::unique_ptr<SDL_Window, WindowDeleter>;
    using GlContextPtr = std::unique_ptr<SDL_GLContextState, GlContextDeleter>;

    Window(WindowPtr window, GlContextPtr context, const GlFunctions& gl, SwapIntervalResult vsync);

    // The context is declared after the window: members are destroyed in reverse, so the context is freed before the
    // window it lives in.
    WindowPtr m_window;
    GlContextPtr m_context;
    GlFunctions m_gl;
    SwapIntervalResult m_vsync;
};
} // namespace Client::Platform::Sdl
