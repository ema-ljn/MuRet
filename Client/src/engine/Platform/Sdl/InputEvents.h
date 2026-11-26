#pragma once

#include "Input/InputEvent.h"
#include "Input/Key.h"
#include "PlatformApi.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>

#include <optional>

// Does not call SDL: tested with a hand-built SDL_Event, without SDL_Init or a window. Keys go by scancode, the
// physical position; the original uses GetAsyncKeyState's VKs, which follow the layout (on AZERTY the Q key gives
// 'A'), so they only match on QWERTY. The wheel goes in whole notches, as in the original, without losing what does
// not reach one. The original does `(short)HIWORD(wParam) / 0x78` per WM_MOUSEWHEEL and assigns the result
// (0x4af4e0): a delta under 120 (a high-resolution wheel sends 30 or 40) moves nothing, and with two messages in a
// frame the last wins. Here it comes from `integer_y`, which SDL accumulates in whole notches (since 3.2.12) keeping
// the remainder between events (residual_scroll_y), and InputState sums: a chosen difference. Not from `y`, which is
// fractional on a touchpad. SDL_MOUSEWHEEL_FLIPPED ("natural" scroll) flips the sign, so forward is always positive.
// An event without a whole notch is not an event, and the horizontal wheel (`x`, `integer_x`) is dropped: on macOS,
// Shift and the wheel arrive as horizontal.
namespace Client::Platform::Sdl
{
// A scancode the client does not use gives std::nullopt. So does the keypad Enter: the original sees it as VK_RETURN,
// but with a single Key::Enter, releasing one with the other down would release it; left for CL8, with its side, like
// Shift (P-C08).
[[nodiscard]] PLATFORM_API std::optional<Input::Key> KeyFor(SDL_Scancode scancode);

// std::nullopt for a non-input event, a key or button the client does not use (X1, X2) and a wheel without a whole
// notch. Navigation and Delete repeats stay as repeated KeyDowns for the EDIT; Backspace, Tab and Enter go on as Text
// with their byte (InputEvent.h), like the EDIT's repeated WM_CHAR. Other repeats are not events. Each left DOWN with
// an even positive clicks gives ButtonDoubleClick: the WM_LBUTTONDBLCLK that replaces the second WM_LBUTTONDOWN. The
// physical edges stay available for the selection, which has its own clock. The mouse position is in framebuffer
// pixels: SDL gives window coordinates and `pixelDensity` (SDL_GetWindowPixelDensity) is the pixels per coordinate; a
// density that is not positive and finite counts as 1, since SDL gives 0 on failure. SDL_EVENT_TEXT_INPUT gives Text
// with the text copied (SDL frees it on the next SDL_PollEvent); an empty one is not an event. SDL_EVENT_TEXT_EDITING
// gives Composition with its text copied, start and length, even empty when the IME cancels the composition; it adds
// nothing to the legacy Text().
[[nodiscard]] PLATFORM_API std::optional<Input::InputEvent> InputEventFor(const SDL_Event& event,
                                                                          float pixelDensity = 1.0f);
} // namespace Client::Platform::Sdl
