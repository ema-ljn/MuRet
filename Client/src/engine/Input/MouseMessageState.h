#pragma once

// The game WndProc's flags: WM_LBUTTONDBLCLK replaces the second WM_LBUTTONDOWN and only sets doubleClick (0x4b47b6).
// InputState's physical edges stay available for character selection (0x640940).
namespace Client::Input
{
struct MouseMessageState
{
    bool leftDown = false;
    bool leftPress = false;
    bool leftRelease = false;
    // The last message's value, until another message. The game takes its update once with TakeDoubleClickMessage and
    // then keeps the state its consumers leave.
    bool doubleClick = false;
};
} // namespace Client::Input
