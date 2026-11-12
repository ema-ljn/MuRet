#pragma once

#include "Input/Key.h"

#include <cstdint>
#include <string>

// Un evento de teclado, de mouse o de foco, ya traducido de SDL por Platform (InputEventFor,
// CL3.3). InputState::Apply lo suma al estado del frame.
// Contrato: el plan de la CL3, sub-ola CL3.3, y el de la CL5, sub-ola CL5.5 (el texto). Cada tipo
// usa solo sus campos: KeyDown y KeyUp la tecla, ButtonDown y ButtonUp el botón, Wheel las
// muescas, Motion la posición, Text el texto y FocusLost ninguno. Un InputEvent sin llenar no hace
// nada: es el KeyDown de Key::Count, que no es tecla.
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
        // Lo tecleado, de SDL_EVENT_TEXT_INPUT (CL5.5): ya con el layout, Shift y las teclas
        // muertas aplicados, que el campo de texto de la UI no tiene que saber.
        Text,
    };

    Kind kind = Kind::KeyDown;
    // Una tecla física: Platform no manda Shift, Ctrl ni Alt, sino sus lados (Key.h).
    Key key = Key::Count;
    MouseButton button{};
    // Las muescas de la rueda, positivas hacia adelante. Salen de `integer_y`, con el signo de
    // `direction` (InputEvents.h).
    float wheel = 0.0f;
    // La posición, en píxeles del framebuffer: Platform multiplica la de la ventana por su
    // densidad (CL5.5, pregunta 12 del plan de la CL5). Sin acotar (Q-C47): el original tiene dos
    // posiciones. La de la UI nueva (0x640940) la acota al tamaño de la ventana, en píxeles, sin
    // escalar (+0x28 y +0x2c, que 0x4ba84a le pasa de [0x6d5d60] y [0x6d5d64]); eso lo hace
    // UI::MouseInPixels. La del WndProc la escala a 640 x 480 sin acotar (0x4b358f..0x4b35c8):
    // queda para la CL8. Con el mouse capturado SDL puede dar negativos o más allá del borde.
    float x = 0.0f;
    float y = 0.0f;
    // El texto de un Text, en UTF-8 y copiado: el de SDL vive hasta el próximo SDL_PollEvent. Va
    // último para que los inicializadores designados de los demás campos no cambien.
    std::string text{};
};

// Los bytes que lleva el texto tecleado por Backspace y por Tab, en el lugar en que llegaron: los
// WM_CHAR 8 y 9 que Windows le da al EDIT de un campo (0x467b10), con la repetición del teclado.
// SDL no los manda como texto (SDL_EVENT_TEXT_INPUT trae solo lo imprimible): el apretón lo suma
// InputState::Apply y la repetición la manda Platform como un Text (InputEvents.h).
inline constexpr char BackspaceChar = '\b';
inline constexpr char TabChar = '\t';
// Y el de Enter: el WM_CHAR 0xd que la ventana del juego atiende en 0x4b50b0 (la entrada del chat, CL8 hito 3) y
// que la subclase de los EDIT (0x467b10) le pasa. Los campos que no lo toman lo descartan, como cualquier byte de
// control.
inline constexpr char EnterChar = '\r';
} // namespace Client::Input
