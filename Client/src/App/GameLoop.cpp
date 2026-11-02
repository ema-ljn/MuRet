#include "App/GameLoop.h"

#include "App/LoopTiming.h"

#include <chrono>

namespace Client::App
{
GameLoop::GameLoop(Core::IClock& clock, ILoopClient& client, PacingMode mode)
    : m_clock(clock), m_client(client), m_mode(mode)
{
}

int GameLoop::RunFrame(const WindowState& state)
{
    const int steps = m_mode == PacingMode::Faithful ? RunFaithful(state) : RunDecoupled(state);
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

int GameLoop::RunFaithful(const WindowState& state)
{
    const int steps = m_pacer.BeginFrame();
    RunSteps(steps);
    // Después de los pasos (0x63852b): lo que tardan no se acumula.
    m_pacer.StartMeasure(m_clock);
    // El original dibuja el estado después de los pasos, sin interpolar. Render también presenta
    // (ILoopClient), así que el swap queda adentro de la medición, como en 0x63852b..0x638632.
    m_lastAlpha = 0.0f;
    if (!state.minimized)
    {
        m_client.Render(m_lastAlpha);
    }
    // Minimizada también: sin la espera el acumulador no suma y no corre nunca más un paso.
    m_pacer.EndFrame(m_clock);
    m_stats.accumulated = std::chrono::milliseconds{m_pacer.AccumulatedMs()};
    m_stats.lastMeasuredMs = m_pacer.LastMeasuredMs();
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
