#pragma once

#include <cstddef>
#include <cstdint>

// Las teclas y los botones del mouse que usa el cliente, con nombre y sin SDL: Platform traduce
// los scancodes de SDL a estos (KeyFor, CL3.3) y el resto del cliente no ve otra cosa.
// Contrato: el plan de la CL3, sub-ola CL3.3, y lo que lee el original con GetAsyncKeyState en
// Client/docs/research/02-arranque-y-ciclo-de-vida.md, "El teclado y el mouse: en el paso y en el
// frame (P-C08)" (08-pendientes.md, P-C08).
//
// El research no lista todas las teclas: las de las ventanas que van por vtable y el chat quedan
// para la CL8 (P-C08, "Falta"). Acá van las que nombra el 02, y para que la CL8 no tenga que tocar
// este enum, también las letras, los números, F1 a F12, las cuatro flechas y Tab.
//
// Shift, Ctrl y Alt son una tecla cada una, sin lado, como VK_SHIFT, VK_CONTROL y VK_MENU de
// GetAsyncKeyState. Platform les manda los dos lados: con los dos abajo, soltar uno la suelta.
namespace Client::Input
{
enum class Key : std::uint16_t
{
    // Las que nombra el 02 por escena: Esc en la 2 (0x634c60), Enter y Esc en la 4 (0x6342c0) y
    // en seis ventanas de la UI nueva, por frame (0x63d880, 0x642e30, 0x6418b0, ...).
    Escape,
    Enter,
    // La 5 (el juego): F5 y F6 en 0x490490; RePág y AvPág en 0x64d090 y 0x59ff10; Espacio en
    // 0x5ac8d0; las flechas arriba y abajo en 0x59ff10; Impr Pant en 0x637700 (0x637cff).
    F5,
    F6,
    PageUp,
    PageDown,
    Space,
    Up,
    Down,
    PrintScreen,
    // Los modificadores: Shift en 0x59adf0, 0x652330, 0x420e30 y, por frame, en 0x637700
    // (0x638305); Ctrl en 0x5588d0, 0x558eb0, 0x5c4110 y 0x59ff10; Alt en 0x59ff10 (0x5a1127,
    // 0x5a118f) y, por frame, en el dibujo (0x59ec80, 0x5bbbc0).
    Shift,
    Ctrl,
    Alt,
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
    // Las letras: el 02 nombra Q (0x59ff10) y S (0x5e4ce0); las otras no salen del research.
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
    // No salen del research: el resto de las F, las flechas de los costados y Tab.
    F1,
    F2,
    F3,
    F4,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Left,
    Right,
    Tab,
    // Cuántas hay, para el tamaño de las tablas (InputState). No es una tecla.
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
