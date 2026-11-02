#include "App/GameLoop.h"

#include "App/LoopTiming.h"

#include <chrono>

namespace Client::App
{
GameLoop::GameLoop(Core::IClock& clock, ILoopClient& client, PacingMode mode, int maxFps)
    : m_clock(clock), m_client(client), m_mode(mode),
      m_minFrameTime(maxFps > 0 ? Core::Duration{std::chrono::seconds{1}} / maxFps : Core::Duration::zero())
{
}

int GameLoop::RunFrame(const WindowState& state, Input::InputState& input)
{
    const int steps = m_mode == PacingMode::Faithful ? RunFaithful(state, input) : RunDecoupled(state, input);
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

int GameLoop::RunDecoupled(const WindowState& state, Input::InputState& input)
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
    RunSteps(steps, input);
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
        // El tope de FPS: duerme lo que le falta al frame para durar m_minFrameTime desde `now`,
        // así el próximo RunFrame mide eso. Con VSync, el swap de Render ya esperó el retrazo, y
        // el tope solo frena si es más bajo que el refresco. Si el reloj volvió (solo el
        // FakeClock), no duerme.
        if (m_minFrameTime > Core::Duration::zero())
        {
            const Core::Duration elapsed = m_clock.Now() - now;
            if (elapsed >= Core::Duration::zero() && elapsed < m_minFrameTime)
            {
                m_clock.SleepFor(m_minFrameTime - elapsed);
            }
        }
    }
    return steps;
}

int GameLoop::RunFaithful(const WindowState& state, Input::InputState& input)
{
    const int steps = m_pacer.BeginFrame();
    RunSteps(steps, input);
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

void GameLoop::RunSteps(int steps, Input::InputState& input)
{
    for (int step = 0; step < steps; ++step)
    {
        m_client.Step(input);
        if (step == 0)
        {
            // Como el primer lector del original, que borra el bit bajo: el segundo paso del
            // frame ya no ve el apretón, y lo que está abajo sigue abajo.
            input.ConsumeEdges();
        }
    }
}
} // namespace Client::App
