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
    // GetAsyncKeyState(vk) == 0x8001: WasPressed, si ningún lector lo tomó antes en este paso. El
    // original borra el bit bajo en cada lectura (Q-C45): de los que leen el mismo apretón con
    // 0x8001, lo ve el primero. La UI nueva (0x65fc30) lee antes que el paso de la escena, así que
    // un Esc que toma una ventana no lo ve la escena (CL8, hito 8). Lo devuelve ConsumeEdges.
    // Es const como la consulta del original, y marca la tecla como tomada.
    [[nodiscard]] bool TakePress(Key key) const;
    [[nodiscard]] bool IsDown(MouseButton button) const;
    [[nodiscard]] bool WasPressed(MouseButton button) const;
    [[nodiscard]] bool WasReleased(MouseButton button) const;
    // Si el último apretón del botón izquierdo desde el último ConsumeEdges fue el segundo (el cuarto...) de un doble
    // clic: un `clicks` par (InputEvent.h). Windows lo manda como WM_LBUTTONDBLCLK, que no prende el clic del juego
    // (0x4b4444, P-C187). WasPressed lo cuenta igual: la selección de personaje lleva su propio doble clic
    // (DoubleClick.h).
    [[nodiscard]] bool LeftPressWasDouble() const;
    // Si llegó algún evento desde el último ConsumeEdges, y si el último fue ese doble clic: el WndProc prende
    // 0x7d36849 con WM_LBUTTONDBLCLK (0x4b47b6) y cada mensaje que sigue lo apaga antes del `switch` (0x4b3444), así
    // que dura hasta el próximo mensaje (P-C182). Los mensajes de Windows que SDL no manda no cuentan.
    [[nodiscard]] bool AnyEvent() const;
    [[nodiscard]] bool EndsInADoubleClick() const;
    // Las muescas sumadas desde el último ConsumeEdges.
    [[nodiscard]] float Wheel() const;
    // Las muescas del último evento de la rueda desde el último ConsumeEdges, o 0 sin ninguno: lo que deja el
    // último WM_MOUSEWHEEL en el global del original, que no suma.
    [[nodiscard]] float LastWheel() const;
    // La última posición, en píxeles del framebuffer (InputEvent.h); 0 hasta el primer Motion.
    [[nodiscard]] float MouseX() const;
    [[nodiscard]] float MouseY() const;
    // Lo tecleado desde el último ConsumeEdges, en UTF-8 y en orden, como la rueda (CL5.5): el
    // texto de cada Text y, en su lugar, un BackspaceChar, un TabChar o un EnterChar por cada apretón de esas
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
    // Los apretones que ya tomó un TakePress en este paso.
    mutable std::bitset<KeyCount> m_keysTaken;
    std::bitset<MouseButtonCount> m_buttonsDown;
    std::bitset<MouseButtonCount> m_buttonsPressed;
    std::bitset<MouseButtonCount> m_buttonsReleased;
    bool m_leftPressWasDouble = false;
    bool m_anyEvent = false;
    bool m_endsInADoubleClick = false;
    float m_wheel = 0.0f;
    float m_lastWheel = 0.0f;
    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    std::string m_text;
};
} // namespace Client::Input
