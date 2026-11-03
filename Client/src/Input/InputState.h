#pragma once

#include "Input/InputEvent.h"
#include "Input/Key.h"

#include <bitset>
#include <string>
#include <string_view>

// El teclado y el mouse del frame, armados con los eventos y no consultados al sistema: un apretón
// y su soltada entre dos lecturas se ven igual (SDL_GetKeyboardState no los ve, y el original
// tampoco, Q-C45).
// Contrato: el plan de la CL3, sub-ola CL3.3 (la tabla de flancos está en su paso 1), y
// Client/docs/research/02-arranque-y-ciclo-de-vida.md, "El teclado y el mouse: en el paso y en el
// frame (P-C08)".
//
// "Está abajo" (IsDown) es estado: vale mientras no llegue la soltada. "Se apretó" y "se soltó"
// (WasPressed, WasReleased) son flancos: valen desde el último ConsumeEdges, que GameLoop llama
// después del primer paso del frame. Así un apretón lo ve un solo paso, como en el original, donde
// el primer lector borra el bit bajo; y en un frame sin pasos (el original no los tiene) espera al
// próximo.
//
// Shift, Ctrl y Alt son lógicas (Key.h): IsDown es el OR de sus dos lados, como VK_SHIFT, y sus
// flancos son los del OR: se apretó cuando el primer lado bajó y se soltó cuando subió el último.
// Un KeyDown o KeyUp de una tecla lógica se ignora: las manda Platform por lado, y un evento
// lógico suelto no diría qué lado sigue abajo.
namespace Client::Input
{
class InputState
{
public:
    // Suma un evento. Un KeyDown con la tecla ya abajo no es un flanco, y un KeyUp con la tecla
    // arriba tampoco (lo mismo con los botones). FocusLost suelta todo lo que está abajo, con su
    // WasReleased, y borra los WasPressed, la rueda y el texto: el paso siguiente no ve un clic de
    // antes del alt-tab (como MuMain, Winmain.cpp). Una rueda que no es finita (NaN, infinito) se
    // descarta: envenenaría la suma hasta ConsumeEdges.
    void Apply(const InputEvent& event);

    [[nodiscard]] bool IsDown(Key key) const;
    // Desde el último ConsumeEdges.
    [[nodiscard]] bool WasPressed(Key key) const;
    [[nodiscard]] bool WasReleased(Key key) const;
    [[nodiscard]] bool IsDown(MouseButton button) const;
    [[nodiscard]] bool WasPressed(MouseButton button) const;
    [[nodiscard]] bool WasReleased(MouseButton button) const;
    // Las muescas sumadas desde el último ConsumeEdges.
    [[nodiscard]] float Wheel() const;
    // La última posición, en píxeles del framebuffer (InputEvent.h); 0 hasta el primer Motion.
    [[nodiscard]] float MouseX() const;
    [[nodiscard]] float MouseY() const;
    // Lo tecleado desde el último ConsumeEdges, en UTF-8 y en orden, como la rueda (CL5.5): el
    // texto de cada Text y, en su lugar, un BackspaceChar o un TabChar por cada apretón de esas
    // teclas (InputEvent.h), para que lo de antes y lo de después de un Tab o un Backspace del
    // mismo paso no se mezclen. Vale hasta el próximo Apply o ConsumeEdges. FocusLost lo borra,
    // como los apretones.
    [[nodiscard]] std::string_view Text() const;

    // Lo llama GameLoop después de un paso: borra los flancos, la rueda y el texto, no lo que está
    // abajo.
    void ConsumeEdges();

private:
    void Press(Key key);
    void Release(Key key);
    void Press(MouseButton button);
    void Release(MouseButton button);
    void ReleaseEverything();
    // Pone la tecla lógica de un lado (Shift de ShiftLeft) en el OR de sus dos lados, con su flanco
    // si cambió. Con una tecla que no es un lado, no hace nada.
    void UpdateLogical(Key side);

    std::bitset<KeyCount> m_keysDown;
    std::bitset<KeyCount> m_keysPressed;
    std::bitset<KeyCount> m_keysReleased;
    std::bitset<MouseButtonCount> m_buttonsDown;
    std::bitset<MouseButtonCount> m_buttonsPressed;
    std::bitset<MouseButtonCount> m_buttonsReleased;
    float m_wheel = 0.0f;
    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    std::string m_text;
};
} // namespace Client::Input
