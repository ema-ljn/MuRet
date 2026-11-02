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
    // La red primero, también minimizada: lo que llegue lo ven los pasos de este frame. En
    // Decoupled lo que tarda entra en el frame (se mide de un Now() al siguiente, después del
    // Poll); en Faithful queda antes de BeginFrame y fuera de la medición, como el bucle de
    // mensajes de WinMain (Q-C44).
    m_client.Poll();
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
        // es más que 0 porque el acumulador quedó debajo de LogicStep. El tope no se usa, y al
        // volver arranca de nuevo.
        m_clock.SleepFor(LogicStep - m_fixedStep.Accumulated());
        m_frameDeadline.reset();
    }
    else
    {
        m_client.Render(m_lastAlpha);
        if (m_minFrameTime > Core::Duration::zero())
        {
            SleepRestOfFrame(now);
        }
    }
    return steps;
}

void GameLoop::SleepRestOfFrame(Core::Duration frameStart)
{
    // La meta es m_frameDeadline (hasta dónde durmió el frame anterior, o dónde terminó si se pasó)
    // más m_minFrameTime, y no frameStart + m_minFrameTime: así lo que corre entre dos RunFrame (los
    // eventos, --stats) cae adentro del frame y no se suma, y el ritmo queda en maxFps, o en lo que
    // dé la máquina si es menos. Sin meta anterior (el primer frame, al volver de minimizada) o con
    // una que quedó adelante (el reloj volvió entre frames), cuenta desde frameStart.
    const bool followsLastDeadline = m_frameDeadline && frameStart >= *m_frameDeadline;
    const Core::Duration deadline = (followsLastDeadline ? *m_frameDeadline : frameStart) + m_minFrameTime;
    const Core::Duration afterRender = m_clock.Now();
    if (afterRender < frameStart)
    {
        // El reloj volvió durante el frame: la resta daría más que un frame entero.
        m_frameDeadline.reset();
    }
    else if (afterRender < deadline)
    {
        m_clock.SleepFor(deadline - afterRender);
        m_frameDeadline = deadline;
    }
    else
    {
        // Se pasó de la meta (un frame largo, o un hueco largo antes de empezar): no duerme, y la
        // próxima cuenta desde acá. Así no se pone al día con frames más cortos, y lo que corra
        // hasta el próximo RunFrame tampoco se suma.
        m_frameDeadline = afterRender;
    }
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
