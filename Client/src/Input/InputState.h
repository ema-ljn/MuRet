#pragma once

#include "Input/InputEvent.h"
#include "Input/Key.h"

#include <bitset>

// El teclado y el mouse del frame, armados con los eventos y no consultados al sistema: un apretón
// y su soltada entre dos lecturas se ven igual (SDL_GetKeyboardState no los ve, y el original
// tampoco, Q-C45).
// Contrato: el plan de la CL3, sub-ola CL3.3 (la tabla de flancos está en su paso 1), y
// Client/docs/research/02-arranque-y-ciclo-de-vida.md, "El teclado y el mouse: en el paso y en el
// frame (P-C08)".
//
// "Está abajo" (IsDown) es estado: vale mientras no llegue la soltada. "Se apretó" y "se soltó"
// (WasPressed, WasReleased) son flancos: valen desde el último ConsumeEdges, que GameLoop llama
// después del primer paso del frame. Así un apretón lo ve un solo paso, y en un frame sin pasos
// espera al próximo, como en el original, donde el primer lector borra el bit bajo.
namespace Client::Input
{
class InputState
{
public:
    // Suma un evento. Un KeyDown con la tecla ya abajo no es un flanco, y un KeyUp con la tecla
    // arriba tampoco (lo mismo con los botones). FocusLost suelta todo lo que está abajo.
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
    // La última posición, en coordenadas de la ventana; 0 hasta el primer Motion.
    [[nodiscard]] float MouseX() const;
    [[nodiscard]] float MouseY() const;

    // Lo llama GameLoop después de un paso: borra los flancos y la rueda, no lo que está abajo.
    void ConsumeEdges();

private:
    void Press(Key key);
    void Release(Key key);
    void Press(MouseButton button);
    void Release(MouseButton button);
    void ReleaseEverything();

    std::bitset<KeyCount> m_keysDown;
    std::bitset<KeyCount> m_keysPressed;
    std::bitset<KeyCount> m_keysReleased;
    std::bitset<MouseButtonCount> m_buttonsDown;
    std::bitset<MouseButtonCount> m_buttonsPressed;
    std::bitset<MouseButtonCount> m_buttonsReleased;
    float m_wheel = 0.0f;
    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
};
} // namespace Client::Input
