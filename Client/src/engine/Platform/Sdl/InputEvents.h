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
// La rueda va en muescas enteras, como en el original, pero sin perder lo que no llega a una
// muesca. El original hace `(short)HIWORD(wParam) / 0x78` en cada WM_MOUSEWHEEL y asigna el
// resultado (0x4af4e0): un delta menor que 120 (una rueda de alta resolución manda 30 o 40) no
// mueve nada, y con dos mensajes en un frame gana el último. Acá sale de `integer_y`, que SDL
// acumula en muescas enteras (desde 3.2.12) y guarda el resto entre eventos (residual_scroll_y),
// e InputState suma: una diferencia elegida. No sale de `y`, que es fraccionaria en un touchpad.
// Con SDL_MOUSEWHEEL_FLIPPED (el scroll "natural") se le cambia el signo, para que sea positiva
// hacia adelante siempre. Un evento sin muesca entera no es un evento, y la rueda horizontal
// (`x`, `integer_x`) se descarta: en macOS, Shift y la rueda llegan como horizontal.
namespace Client::Platform
{
// La tecla de un scancode; uno que el cliente no usa da std::nullopt. El Enter del teclado
// numérico también: el original lo ve como VK_RETURN, pero con una sola Key::Enter, soltar uno de
// los dos con el otro abajo la soltaría; queda para la CL8, con su lado, como Shift (P-C08).
[[nodiscard]] std::optional<Input::Key> KeyFor(SDL_Scancode scancode);

// El evento de Input de un SDL_Event. Da std::nullopt con un evento que no es de input, una tecla
// o un botón que el cliente no usa (X1, X2) y una rueda sin muesca entera. Las repeticiones de
// navegación y Delete se conservan como KeyDown repetidos para el EDIT; Backspace, Tab y Enter
// siguen como Text con su byte (InputEvent.h), como el WM_CHAR repetido del EDIT (CL5.5). Las
// otras repeticiones no son eventos.
// SDL_EVENT_WINDOW_FOCUS_LOST da FocusLost (PumpEvents lo pasa también al estado de la ventana).
// Cada DOWN izquierdo con clicks par positivo da ButtonDoubleClick: equivale al WM_LBUTTONDBLCLK que sustituye el
// segundo WM_LBUTTONDOWN. Los flancos físicos siguen disponibles para la selección, que tiene su propio reloj.
//
// La posición del mouse va en píxeles del framebuffer (CL5.5): SDL la da en coordenadas de la
// ventana, y `pixelDensity` (SDL_GetWindowPixelDensity) es cuántos píxeles mide cada una. Una
// densidad que no es positiva y finita cuenta como 1: SDL da 0 si falla.
//
// SDL_EVENT_TEXT_INPUT da Text con el texto copiado (SDL lo libera en el próximo SDL_PollEvent);
// uno vacío no es un evento. SDL_EVENT_TEXT_EDITING da Composition con su texto copiado, start y
// length, incluso vacío cuando el IME cancela la composición; no agrega nada al Text() legado.
[[nodiscard]] std::optional<Input::InputEvent> InputEventFor(const SDL_Event& event, float pixelDensity = 1.0f);
} // namespace Client::Platform
