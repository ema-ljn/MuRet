#pragma once

#include "App/ILoopClient.h"
#include "Core/IClock.h"
#include "support/FakeClock.h"

#include <cstdint>
#include <vector>

// El cliente de prueba de los tests del bucle (CL3.2).
namespace Client::Tests
{
// Cuenta pasos y dibujos, guarda cada alfa, y hace "tardar" a cada paso y a cada dibujo lo que
// se le diga moviendo el FakeClock: así un test arma un frame de 130 ms o un breakpoint de 10 s.
class RecordingClient final : public App::ILoopClient
{
public:
    explicit RecordingClient(FakeClock& clock);
    void Step() override;
    void Render(float alpha) override;

    Core::Duration stepCost{};
    Core::Duration renderCost{};
    std::uint64_t steps = 0;
    // Uno por dibujo.
    std::vector<float> alphas;

private:
    FakeClock& m_clock;
};
} // namespace Client::Tests
