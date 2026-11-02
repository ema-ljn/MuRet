#pragma once

#include "Input/Key.h"

#include <cstdint>

// Un evento de teclado, de mouse o de foco, ya traducido de SDL por Platform (InputEventFor,
// CL3.3). InputState::Apply lo suma al estado del frame.
// Contrato: el plan de la CL3, sub-ola CL3.3. Cada tipo usa solo sus campos: KeyDown y KeyUp la
// tecla, ButtonDown y ButtonUp el botón, Wheel las muescas, Motion la posición y FocusLost
// ninguno. Un InputEvent sin llenar no hace nada: es el KeyDown de Key::Count, que no es tecla.
namespace Client::Input
{
struct InputEvent
{
    enum class Kind : std::uint8_t
    {
        KeyDown,
        KeyUp,
        ButtonDown,
        ButtonUp,
        Wheel,
        Motion,
        FocusLost,
    };

    Kind kind = Kind::KeyDown;
    // Una tecla física: Platform no manda Shift, Ctrl ni Alt, sino sus lados (Key.h).
    Key key = Key::Count;
    MouseButton button{};
    // Las muescas de la rueda, positivas hacia adelante. Salen de `integer_y`, con el signo de
    // `direction` (InputEvents.h).
    float wheel = 0.0f;
    // La posición, en coordenadas de la ventana: píxeles mientras la ventana no pida
    // SDL_WINDOW_HIGH_PIXEL_DENSITY (la de 640 x 480 es de la CL5, D-C11). Sin acotar (Q-C47):
    // el original tiene dos posiciones. La de la UI nueva (0x640940) la acota al tamaño de la
    // ventana, en píxeles, sin escalar (+0x28 y +0x2c, que 0x4ba84a le pasa de [0x6d5d60] y
    // [0x6d5d64]); la del WndProc la escala a 640 x 480 sin acotar (0x4b358f..0x4b35c8). Con el
    // mouse capturado SDL puede dar negativos o más allá del borde. Se decide en la CL5.
    float x = 0.0f;
    float y = 0.0f;
};
} // namespace Client::Input
