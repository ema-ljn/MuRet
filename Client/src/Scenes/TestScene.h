#pragma once

#include "App/ILoopClient.h"
#include "Input/InputState.h"

#include <cstdint>
#include <functional>

// La escena de prueba de la CL3.2, hasta que llegue la LoginScene (CL5.4): cada paso avanza el
// ciclo de 32 pasos de DAT_07d2d360, (n + 1) mod 32 (02, "Qué corre por paso"), y el fondo va de un
// azul oscuro a uno claro y vuelve en cada ciclo, 1,28 s. No ve SDL ni GL: el que la arma le pasa
// cómo pintar el fondo y presentar (main, con el GL de la ventana), y los tests, un registro.
namespace Client::Scenes
{
// Un color, de 0 a 1 por canal.
struct Color
{
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
};

class TestScene final : public App::ILoopClient
{
public:
    // `present` pinta todo el fondo de ese color y presenta (el swap va adentro de Render,
    // ILoopClient).
    explicit TestScene(std::function<void(const Color&)> present);

    // La escena de prueba no lee el input.
    void Step(const Input::InputState& input) override;
    void Render(float alpha) override;
    // El fondo entre el paso actual y el próximo: `alpha` en [0, 1), como el de Render.
    [[nodiscard]] Color Background(float alpha) const;

private:
    std::function<void(const Color&)> m_present;
    std::uint32_t m_cycle = 0;
};
} // namespace Client::Scenes
