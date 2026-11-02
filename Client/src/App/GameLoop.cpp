#include "App/GameLoop.h"

#include "App/LoopTiming.h"

namespace Client::App
{
GameLoop::GameLoop(Core::IClock& clock, ILoopClient& client, PacingMode mode)
    : m_clock(clock), m_client(client), m_mode(mode)
{
}

int GameLoop::RunFrame(const WindowState& state)
{
    // El modo fiel llega en el PR B de la CL3.2; hasta entonces los dos corren el normal.
    static_cast<void>(m_mode);
    const int steps = RunDecoupled(state);
    ++m_stats.frames;
    m_stats.steps += static_cast<std::uint64_t>(steps);
    return steps;
}

const FrameStats& GameLoop::Stats() const
{
    return m_stats;
}

float GameLoop::LastAlpha() const
{
    return m_lastAlpha;
}

int GameLoop::RunDecoupled(const WindowState& state)
{
    const Core::Duration now = m_clock.Now();
    // El frame se mide desde el Now() anterior, también si el reloj volvió para atrás: ese frame
    // cuenta 0 (FixedStep) y el siguiente se mide desde el valor nuevo.
    const Core::Duration frameTime = m_lastNow ? now - *m_lastNow : Core::Duration::zero();
    m_lastNow = now;

    const int steps = m_fixedStep.Advance(frameTime);
    if (m_fixedStep.LastFrameWasClamped())
    {
        ++m_stats.clampedFrames;
    }
    RunSteps(steps);
    m_stats.accumulated = m_fixedStep.Accumulated();
    m_lastAlpha = m_fixedStep.Alpha();

    if (state.minimized)
    {
        // Sin dibujo, el swap no frena el bucle: duerme lo que falta para el próximo paso, que
        // es más que 0 porque el acumulador quedó debajo de LogicStep.
        m_clock.SleepFor(LogicStep - m_fixedStep.Accumulated());
    }
    else
    {
        m_client.Render(m_lastAlpha);
    }
    return steps;
}

void GameLoop::RunSteps(int steps)
{
    for (int step = 0; step < steps; ++step)
    {
        m_client.Step();
    }
}
} // namespace Client::App
