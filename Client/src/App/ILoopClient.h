#pragma once

// Lo que el bucle llama (CL3.2): la lógica, un paso por vez, y el dibujo. Está en su propio header
// porque lo implementan la escena de Main y el cliente de prueba de los tests, y ninguno necesita
// el bucle entero.
namespace Client::App
{
// En la CL3.3, Step recibe el InputState (ver ahí).
class ILoopClient
{
public:
    virtual ~ILoopClient() = default;
    virtual void Step() = 0;
    // alpha en [0, 1): cuánto del próximo paso ya pasó (Decoupled), o 0 (Faithful).
    virtual void Render(float alpha) = 0;
};
} // namespace Client::App
