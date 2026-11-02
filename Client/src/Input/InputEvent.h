#pragma once

#include "Input/Key.h"

#include <cstdint>

// Un evento de teclado, de mouse o de foco, ya traducido de SDL por Platform (InputEventFor,
// CL3.3). InputState::Apply lo suma al estado del frame.
// Contrato: el plan de la CL3, sub-ola CL3.3. Cada tipo usa solo sus campos: KeyDown y KeyUp la
// tecla, ButtonDown y ButtonUp el botón, Wheel las muescas, Motion la posición y FocusLost
// ninguno.
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

    Kind kind;
    Key key{};
    MouseButton button{};
    // Las muescas de la rueda, positivas hacia adelante (como SDL_MouseWheelEvent::y).
    float wheel = 0.0f;
    // La posición, en coordenadas de la ventana: píxeles mientras la ventana no pida
    // SDL_WINDOW_HIGH_PIXEL_DENSITY (la de 640 x 480 es de la CL5, D-C11).
    float x = 0.0f;
    float y = 0.0f;
};
} // namespace Client::Input
