#include "Kernel/MouseState.h"

#include <cstdint>

namespace Client::Kernel
{
void LatchTheLeftButton(MouseState& state, bool pressed, bool down)
{
    // WM_LBUTTONDBLCLK does not touch 0x7d36874 (0x4b4444..0x4b47b6), which only WM_LBUTTONDOWN (0x4b3961) and
    // WM_LBUTTONUP (0x4b3cf1) write: with a double click's second press as the last message, `down` stays false and
    // the button stays up for the game while it is held, until the next WM_LBUTTONDOWN.
    if (!down)
        state.leftHeld = false;
    else if (pressed || !state.previousLeftDown)
        state.leftHeld = true;
    state.previousLeftDown = down;
    // WM_LBUTTONDOWN sets the edge with the button up (0x4b395a) and WM_LBUTTONUP clears it (0x4b3ce3): a click pressed
    // and released in the same step does not stay, as after the two messages.
    state.pendingClick = down && (state.pendingClick || pressed);
}

void LatchTheLeftRelease(MouseState& state, bool pressed, bool released, bool down, std::int32_t mouseX,
                         std::int32_t mouseY)
{
    if (state.releasedClick && (state.releaseX != mouseX || state.releaseY != mouseY))
        state.releasedClick = false;
    // Pressed after the release, the button stays down: the WM_LBUTTONDOWN was the last message.
    if (pressed && (!released || down))
    {
        state.releasedClick = false;
        return;
    }
    if (!released)
        return;
    state.releasedClick = true;
    state.releaseX = mouseX;
    state.releaseY = mouseY;
}
} // namespace Client::Kernel
