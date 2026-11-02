#pragma once

#include "Input/InputEvent.h"
#include "Input/Key.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>

#include <optional>

// De los eventos de SDL a los de Input: lo único del input que ve SDL (el resto, InputState, no).
// Contrato: el plan de la CL3, sub-ola CL3.3. No llama a SDL: se prueba con un SDL_Event armado a
// mano, sin SDL_Init y sin ventana.
//
// Las teclas van por scancode, la posición física. El original usa los VK de GetAsyncKeyState, que
// siguen el layout (en AZERTY, la tecla de la Q da 'A'): en QWERTY coinciden, en otro no.
// Shift, Ctrl y Alt salen por lado (ShiftLeft, ...): las lógicas las arma InputState (Key.h).
//
// La rueda va en muescas enteras, como el delta / 120 del WndProc del original (02): sale de
// `integer_y`, que SDL acumula en muescas enteras (desde 3.2.12), y no de `y`, que es fraccionaria
// en un touchpad. Con SDL_MOUSEWHEEL_FLIPPED (el scroll "natural") se le cambia el signo, para que
// sea positiva hacia adelante siempre. Un evento sin muesca entera no es un evento.
namespace Client::Platform
{
// La tecla de un scancode; uno que el cliente no usa da std::nullopt. El Enter del teclado
// numérico también: el original lo ve como VK_RETURN, pero con una sola Key::Enter, soltar uno de
// los dos con el otro abajo la soltaría; queda para la CL8, con su lado, como Shift.
[[nodiscard]] std::optional<Input::Key> KeyFor(SDL_Scancode scancode);

// El evento de Input de un SDL_Event. Da std::nullopt con un evento que no es de input, un
// auto-repeat, una tecla o un botón que el cliente no usa (X1, X2) y una rueda sin muesca entera.
// SDL_EVENT_WINDOW_FOCUS_LOST da FocusLost (PumpEvents lo pasa también al estado de la ventana).
[[nodiscard]] std::optional<Input::InputEvent> InputEventFor(const SDL_Event& event);
} // namespace Client::Platform
