#pragma once

#include "App/ILoopClient.h"
#include "Core/IClock.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "support/FakeClock.h"

#include <cstdint>
#include <vector>

// El cliente de prueba de los tests del bucle (CL3.2 y CL3.3).
namespace Client::Tests
{
// Cuenta pasos y dibujos, guarda cada alfa, y hace "tardar" a cada paso y a cada dibujo lo que
// se le diga moviendo el FakeClock: así un test arma un frame de 130 ms o un breakpoint de 10 s.
class RecordingClient final : public App::ILoopClient
{
public:
    explicit RecordingClient(FakeClock& clock);
    void Step(const Input::InputState& input) override;
    void Render(float alpha) override;

    Core::Duration stepCost{};
    Core::Duration renderCost{};
    std::uint64_t steps = 0;
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
