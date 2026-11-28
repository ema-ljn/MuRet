#pragma once

#include <cstdint>

// Who takes the frame's click: the UI first, then the modules that ask before the hero walks (06 § HUD y ventanas,
// "El clic es primero de la UI").
namespace Client::Kernel
{
struct MouseState
{
    std::int32_t updateTime = 0;
    // .data[0x6c8dcc] = 6 before the first SendMove.
    std::int32_t updateTimeMax = 6;
    // 0x7d366d4, the left click's edge: the step sets it on the press (before 0x623e70) and whoever uses it clears it,
    // the UI first, and so does the released button.
    bool pendingClick = false;
    bool leftHeld = false;
    bool previousLeftDown = false;
    // 0x7d366ec, the release's edge: WM_LBUTTONUP (0x4b3cea) sets it with that moment's mouse (0x576358c and
    // 0x5763590); WM_LBUTTONDOWN (0x4b3947), any message with the mouse elsewhere (0x4b344d) and its users (the old
    // buttons and the title bar, 0x45b200 and 0x492ac0) clear it.
    bool releasedClick = false;
    std::int32_t releaseX = 0;
    std::int32_t releaseY = 0;
};

// The WndProc: 0x7d366d4 (the click's edge) and 0x7d36874 (the button down), before any logic of the step. The game
// scene calls it at the start of each step, and MoveHero too: a WM_LBUTTONDBLCLK does not set 0x7d36874 (P-C187).
void LatchTheLeftButton(MouseState& state, bool pressed, bool down);

// The left button's release: WM_LBUTTONUP sets 0x7d366ec with that moment's mouse (0x4b3cea, at 0x576358c and
// 0x5763590), WM_LBUTTONDOWN clears it (0x4b3947) and so does any message with the mouse elsewhere (0x4b344d). MuRet
// sees the step's messages together, with the step's mouse (*inferred*, P-C96). It goes before LatchTheLeftButton,
// with the mouse in 640 × 480.
void LatchTheLeftRelease(MouseState& state, bool pressed, bool released, bool down, std::int32_t mouseX,
                         std::int32_t mouseY);
} // namespace Client::Kernel
