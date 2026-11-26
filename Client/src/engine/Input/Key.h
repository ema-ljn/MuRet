#pragma once

#include <cstddef>
#include <cstdint>

// Platform maps SDL's scancodes to these (KeyFor) and the rest of the client sees nothing else. What the original reads
// with GetAsyncKeyState: Client/docs/research/02-arranque-y-ciclo-de-vida.md, "El teclado y el mouse: en el paso y en
// el frame (P-C08)" (08-pendientes.md, P-C08). Each group says where it comes from: what 02 names, what the binary
// reads and 02 does not name yet, and what the original does not read, there so CL8 need not touch this enum. The
// original passes only VK_SHIFT, VK_CONTROL and VK_MENU (0x10, 0x11, 0x12), down while either side is, and never 0xA0
// to 0xA5; here each side is a physical key Platform sends, and InputState builds Shift, Ctrl and Alt as the OR of the
// two (InputState.h).
namespace Client::Input
{
enum class Key : std::uint16_t
{
    // Esc: scene 2 (login, 0x634c60), 4 (selection, 0x6342c0), 5 (0x623e70, 0x490490) and six new-UI windows, per frame
    // (0x63d880, 0x642e30, 0x6418b0, 0x63f630, 0x644730, 0x647d40). Enter: scene 4 and three of those windows
    // (0x63d880, 0x642e30, 0x6418b0).
    Escape,
    Enter,
    // Scene 5 (the game): Page Up and Page Down at 0x64d090, 0x650350 and 0x59ff10; Space at 0x5ac8d0.
    PageUp,
    PageDown,
    Space,
    // Print Screen: 0x637700 (0x637cff), after the scene switch (0x637cab..0x637cb9), so every scene reads it.
    PrintScreen,
    // Arrows: 02 names up and down (0x59ff10); the binary reads left and right at 0x467b10 and 0x479960. Home, End and
    // Delete are for the EDIT.
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    Delete,
    // Tab: 02 does not name it; the binary reads it with 0x553520(9) (0x40f6e8..0x40f6ea).
    Tab,
    // Backspace: the original does not read it with GetAsyncKeyState. Each field's Windows EDIT gets it as WM_CHAR 8,
    // and the 0x467b10 subclass passes it to CallWindowProc (0x467d57). Here InputState also adds it to the typed text
    // (BackspaceChar, InputEvent.h), and the account window reads it there, in order.
    Backspace,
    // Built by InputState from the two sides below; Platform does not send them. Shift: 0x59adf0, 0x652330, 0x420e30
    // and, per frame, 0x637700 (0x638305). Ctrl: 0x5588d0, 0x558eb0, 0x5c4110 and 0x59ff10. Alt: 0x59ff10 (0x5a1127,
    // 0x5a118f) and, per frame, the drawing (0x59ec80, 0x5bbbc0).
    Shift,
    Ctrl,
    Alt,
    // The physical sides, which Platform sends.
    ShiftLeft,
    ShiftRight,
    CtrlLeft,
    CtrlRight,
    AltLeft,
    AltRight,
    // Top-row digits: with Alt they pick a macro and without Alt they call 0x59f810, at 0x59ff10 (and with Shift at
    // 0x420e30).
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    // 02 names Q (0x59ff10) and S (0x5e4ce0). The binary also reads A, D, E, F, R and W at 0x5acbe0 (with Q and S), F
    // at 0x4a2f70 (0x4a2fe9), W and E at 0x5c4ae1 and 0x5c4af6, and B, C, G, I, M, P, T and V (the binary review, no
    // address yet). Nobody reads H, J, K, L, N, O, U, X, Y and Z.
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    // 02 names F5 and F6 (0x490490); the binary reads F1 to F4 at 0x5ac8d0 (0x5ac9d0..0x5acad2), the same function that
    // reads Space. Nobody reads F7 to F12.
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    // For the table sizes (InputState). Not a key: never down.
    Count,
};

inline constexpr std::size_t KeyCount = static_cast<std::size_t>(Key::Count);

// 0x640940 reads left, right and middle with GetAsyncKeyState(1), (2) and (4) (02, "El mouse son dos sistemas").
enum class MouseButton : std::uint8_t
{
    Left,
    Middle,
    Right,
};

inline constexpr std::size_t MouseButtonCount = static_cast<std::size_t>(MouseButton::Right) + 1;
} // namespace Client::Input
