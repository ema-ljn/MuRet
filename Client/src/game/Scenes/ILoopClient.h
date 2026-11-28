#pragma once

#include "Input/InputState.h"

// Lo que el bucle llama (CL3.2): la red, una vez por frame (CL4.1); la lógica, un paso por vez, con
// el input del frame (CL3.3); y el dibujo. Está en su propio header porque lo implementan la
// escena de Main y el cliente de prueba de los tests, y ninguno necesita el bucle entero.
namespace Client::App
{
class ILoopClient
{
public:
    virtual ~ILoopClient() = default;
    // La red: una vez por frame, antes de los pasos, también minimizada, como la cola de paquetes
    // que el original despacha en cada vuelta del bucle de WinMain (0x4e1d50). El original la
    // despacha después del frame (0x4be5b3), que en régimen es lo mismo: entre un frame y el
    // siguiente. En el modo fiel va antes de BeginFrame, fuera de la medición, como el bucle de
    // mensajes (GameLoop.h).
    virtual void Poll() = 0;
    // Un paso. Los flancos de `input` (WasPressed, WasReleased, Wheel) los ve solo el primer paso
    // del frame: GameLoop llama a ConsumeEdges después (InputState.h).
    virtual void Step(const Input::InputState& input) = 0;
    // Dibuja y presenta: el swap va adentro de Render, así que el modo fiel lo suma al acumulador
    // con el dibujo, como el original. alpha en [0, 1): cuánto del próximo paso ya pasó
    // (Decoupled), o 0 (Faithful).
    virtual void Render(float alpha) = 0;
    // Si este frame no se dibuja: en la pantalla queda el último que se presentó, como en el
    // original mientras un connect bloqueante tiene parado el bucle (el del login, Q-C85). GameLoop
    // lo mira después de los pasos, y el frame sigue como minimizada: la red y los pasos corren.
    [[nodiscard]] virtual bool HoldsTheLastFrame() const = 0;
    // Si un paso cambió de escena desde la última vez que se preguntó (y lo olvida): la entrada a una
    // escena (cargar sus texturas y sus modelos) corre dentro de un paso, y el original no la mide, porque
    // mide desde después de los pasos (0x63852b). GameLoop lo mira después de los pasos, en los dos modos.
    [[nodiscard]] virtual bool TakeTheSceneChange() = 0;
    // El fin de una vuelta del bucle con la ventana sin minimizar, dibujada o retenida: lo que el original saca
    // después del frame (0x63945d, después de 0x637700) y al empezar la vuelta siguiente (WinMain), P-C61. Sin
    // nada que hacer por omisión.
    virtual void FinishTheLoop() {}
};
} // namespace Client::App
