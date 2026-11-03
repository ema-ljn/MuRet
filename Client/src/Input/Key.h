#pragma once

#include <cstddef>
#include <cstdint>

// Las teclas y los botones del mouse que usa el cliente, con nombre y sin SDL: Platform traduce
// los scancodes de SDL a estos (KeyFor, CL3.3) y el resto del cliente no ve otra cosa.
// Contrato: el plan de la CL3, sub-ola CL3.3, y lo que lee el original con GetAsyncKeyState en
// Client/docs/research/02-arranque-y-ciclo-de-vida.md, "El teclado y el mouse: en el paso y en el
// frame (P-C08)" (08-pendientes.md, P-C08).
//
// Cada grupo dice de dónde sale: lo que nombra el 02, lo que el binario lee y el 02 no nombra
// todavía (de las revisiones de la CL3.3, para sumar a P-C08), y lo que el original no lee, que
// está para que la CL8 no tenga que tocar este enum.
//
// Shift, Ctrl y Alt: el original pasa solo VK_SHIFT, VK_CONTROL y VK_MENU (0x10, 0x11, 0x12), que
// están abajo mientras cualquiera de los dos lados lo esté, y nunca 0xA0 a 0xA5. Acá cada lado es
// una tecla física (ShiftLeft, ShiftRight, ...), que manda Platform, y Shift, Ctrl y Alt son
// lógicas: las arma InputState con el OR de los dos lados (InputState.h).
namespace Client::Input
{
enum class Key : std::uint16_t
{
    // Esc: la escena 2 (login, 0x634c60), la 4 (selección, 0x6342c0), la 5 (0x623e70, 0x490490) y
    // seis ventanas de la UI nueva, por frame (0x63d880, 0x642e30, 0x6418b0, 0x63f630, 0x644730,
    // 0x647d40). Enter: la escena 4 y tres de esas ventanas (0x63d880, 0x642e30, 0x6418b0).
    Escape,
    Enter,
    // La escena 5 (el juego): RePág y AvPág en 0x64d090, 0x650350 y 0x59ff10; Espacio en
    // 0x5ac8d0.
    PageUp,
    PageDown,
    Space,
    // Impr Pant: 0x637700 (0x637cff), después del switch de escenas (0x637cab..0x637cb9), así que
    // se lee en todas.
    PrintScreen,
    // Las flechas: arriba y abajo las nombra el 02 (0x59ff10); izquierda y derecha las lee el
    // binario en 0x467b10 y 0x479960.
    Up,
    Down,
    Left,
    Right,
    // Tab: no la nombra el 02; la lee el binario con 0x553520(9) (0x40f6e8..0x40f6ea).
    Tab,
    // Retroceso: el original no la lee con GetAsyncKeyState. Le llega al EDIT de Windows de cada
    // campo como WM_CHAR 8, y la subclase 0x467b10 la deja pasar a CallWindowProc (0x467d57).
    // Acá InputState la suma también al texto tecleado (BackspaceChar, InputEvent.h), y la ventana
    // de la cuenta la lee ahí, en orden (CL5.5).
    Backspace,
    // Los modificadores lógicos, que arma InputState con los dos lados de abajo; Platform no los
    // manda. Shift: 0x59adf0, 0x652330, 0x420e30 y, por frame, 0x637700 (0x638305). Ctrl:
    // 0x5588d0, 0x558eb0, 0x5c4110 y 0x59ff10. Alt: 0x59ff10 (0x5a1127, 0x5a118f) y, por frame, el
    // dibujo (0x59ec80, 0x5bbbc0).
    Shift,
    Ctrl,
    Alt,
    // Los lados físicos de los modificadores, los que manda Platform.
    ShiftLeft,
    ShiftRight,
    CtrlLeft,
    CtrlRight,
    AltLeft,
    AltRight,
    // Los dígitos de la fila de arriba: con Alt eligen una macro y sin Alt llaman a 0x59f810, en
    // 0x59ff10 (y con Shift en 0x420e30).
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
    // Las letras. El 02 nombra Q (0x59ff10) y S (0x5e4ce0). El binario lee además A, D, E, F, R
    // y W en 0x5acbe0 (con Q y S), F en 0x4a2f70 (0x4a2fe9), W y E en 0x5c4ae1 y 0x5c4af6, y B,
    // C, G, I, M, P, T y V (la revisión del binario, sin dirección todavía). H, J, K, L, N, O, U,
    // X, Y y Z no las lee nadie.
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
    // Las F. El 02 nombra F5 y F6 (0x490490); F1 a F4 las lee el binario en 0x5ac8d0
    // (0x5ac9d0..0x5acad2), la misma función que lee Espacio. F7 a F12 no las lee nadie.
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
    // Cuántas hay, para el tamaño de las tablas (InputState). No es una tecla: nunca está abajo.
    Count,
};

// Cuántas teclas hay, como índice de tabla.
inline constexpr std::size_t KeyCount = static_cast<std::size_t>(Key::Count);

// Los botones del mouse: 0x640940 lee el izquierdo, el derecho y el del medio con
// GetAsyncKeyState(1), (2) y (4) (02, "El mouse son dos sistemas").
enum class MouseButton : std::uint8_t
{
    Left,
    Middle,
    Right,
};

// Cuántos botones hay, como índice de tabla: el último más uno.
inline constexpr std::size_t MouseButtonCount = static_cast<std::size_t>(MouseButton::Right) + 1;
} // namespace Client::Input
