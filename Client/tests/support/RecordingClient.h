#pragma once

#include "App/ILoopClient.h"
#include "Core/IClock.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "support/FakeClock.h"

#include <cstdint>
#include <ostream>
#include <vector>

// El cliente de prueba de los tests del bucle (CL3.2, CL3.3 y CL4.1).
namespace Client::Tests
{
// Cada llamada del bucle al cliente, para mirar el orden.
enum class LoopCall : std::uint8_t
{
    Poll,
    Step,
    Render,
};

// Sin esto, GoogleTest muestra un vector de LoopCall como bytes sueltos.
void PrintTo(LoopCall call, std::ostream* out);

// Cuenta polls, pasos y dibujos, anota el orden en que llegan, guarda cada alfa, y hace "tardar"
// a cada uno lo que se le diga moviendo el FakeClock: así un test arma un frame de 130 ms, un
// breakpoint de 10 s o una red que tarda 20 ms en cada poll.
class RecordingClient final : public App::ILoopClient
{
public:
    explicit RecordingClient(FakeClock& clock);
    void Poll() override;
    void Step(const Input::InputState& input) override;
    void Render(float alpha) override;

    Core::Duration pollCost{};
    Core::Duration stepCost{};
    Core::Duration renderCost{};
    std::uint64_t polls = 0;
    std::uint64_t steps = 0;
    // Todas las llamadas, en orden.
    std::vector<LoopCall> calls;
    // Uno por dibujo.
    std::vector<float> alphas;
    // La tecla que mira cada paso; Count, que nunca se aprieta, si el test no mira ninguna.
    Input::Key watchedKey = Input::Key::Count;
    // Uno por paso: si ese paso vio el apretón de watchedKey (WasPressed).
    std::vector<bool> sawPress;

private:
    FakeClock& m_clock;
};
} // namespace Client::Tests
