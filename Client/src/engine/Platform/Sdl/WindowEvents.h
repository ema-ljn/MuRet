#pragma once

#include "Platform/WindowState.h"
#include "Input/InputState.h"
#include "PlatformApi.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <optional>

namespace Client::Platform::Sdl
{
// Any window: valid SDL_WindowIDs are not 0 (SDL_video.h).
inline constexpr SDL_WindowID AnyWindow = 0;

// Nothing if it is not from window `windowId` (with AnyWindow, from any). SDL_EVENT_QUIT has no window and always
// counts. Does not call SDL: tested with a hand-built SDL_Event.
[[nodiscard]] PLATFORM_API std::optional<WindowEvent> WindowEventFor(const SDL_Event& event,
                                                                     SDL_WindowID windowId = AnyWindow);

// Drains SDL's queue: each window event of `windowId` goes to `state` with Platform::Apply, each input event to
// `input` with InputState::Apply, and the rest are dropped. Focus lost goes to both: the window notes it and the input
// releases everything. `pixelDensity` converts the mouse to framebuffer pixels (InputEventFor): main.cpp passes
// Window::PixelDensity.
PLATFORM_API void PumpEvents(WindowState& state, Input::InputState& input, SDL_WindowID windowId = AnyWindow,
                             float pixelDensity = 1.0f);
} // namespace Client::Platform::Sdl
