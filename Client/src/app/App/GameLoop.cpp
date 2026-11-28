#include "App/GameLoop.h"

#include "Scenes/ILoopClient.h"
#include "Kernel/LoopTiming.h"
#include "Kernel/PacingMode.h"
#include "Platform/WindowState.h"
#include "Core/IClock.h"
#include "Input/InputState.h"

#include <chrono>
#include <cstdint>

namespace Client::App
{
GameLoop::GameLoop(Core::IClock& clock, Scenes::ILoopClient& client, Kernel::PacingMode mode, int maxFps)
    : m_clock(clock), m_client(client), m_mode(mode),
      m_minFrameTime(maxFps > 0 ? Core::Duration{std::chrono::seconds{1}} / maxFps : Core::Duration::zero())
{
}

int GameLoop::RunFrame(const Platform::WindowState& state, Input::InputState& input)
{
    // The network first, also minimized: what arrives is seen by this frame's steps. The original dispatches the queue
    // after the frame (0x4be5b3), which between frames is the same. In Decoupled its time goes into the frame (measured
    // from one Now() to the next, after the Poll); in Faithful it stays before BeginFrame and outside the measurement,
    // like WinMain's message loop (Q-C44). Minimized, the original spins nonstop and here the Poll runs at about 25 Hz
    // (it sleeps until the next 40 ms step), plenty for a 20 s keepalive.
    m_client.Poll();
    const int steps = m_mode == Kernel::PacingMode::Faithful ? RunFaithful(state, input) : RunDecoupled(state, input);
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

int GameLoop::RunDecoupled(const Platform::WindowState& state, Input::InputState& input)
{
    const Core::Duration now = m_clock.Now();
    // Measured from the previous Now(), also if the clock went backwards: that frame counts 0 (FixedStep) and the next
    // one is measured from the new value.
    const Core::Duration frameTime = m_lastNow ? now - *m_lastNow : Core::Duration::zero();
    m_lastNow = now;

    const int steps = m_fixedStep.Advance(frameTime);
    if (m_fixedStep.LastFrameWasClamped())
    {
        ++m_stats.clampedFrames;
    }
    RunSteps(steps, input);
    // The time a scene entry took does not count (ILoopClient::TakeTheSceneChange): the next frame is measured from
    // here, after the steps, as the original measures all of them.
    if (m_client.TakeTheSceneChange())
    {
        m_lastNow = m_clock.Now();
    }
    m_stats.accumulated = m_fixedStep.Accumulated();
    m_lastAlpha = m_fixedStep.Alpha();

    // With the frame held (the login's connect, Q-C85), like minimized: checked after the steps, which are the ones
    // that see what the Poll brought.
    if (state.minimized || m_client.HoldsTheLastFrame())
    {
        // Without drawing, the swap does not pace the loop: it sleeps what is left until the next step, more than 0
        // because the accumulator stayed below LogicStep. The cap is not used, and it starts over on return.
        m_clock.SleepFor(Kernel::LogicStep - m_fixedStep.Accumulated());
        m_frameDeadline.reset();
        // Held, the original goes around and runs the frame without presenting it; minimized, it takes the frameless
        // branch, which is not modeled (P-C61).
        if (!state.minimized)
        {
            m_client.FinishTheLoop();
        }
    }
    else
    {
        m_client.Render(m_lastAlpha);
        m_client.FinishTheLoop();
        if (m_minFrameTime > Core::Duration::zero())
        {
            SleepRestOfFrame(now);
        }
    }
    return steps;
}

void GameLoop::SleepRestOfFrame(Core::Duration frameStart)
{
    // The target is m_frameDeadline (how far the previous frame slept, or where it ended if it overran) plus
    // m_minFrameTime, not frameStart + m_minFrameTime: so what runs between two RunFrame (the events, --stats) falls
    // inside the frame and does not add up, and the rate stays at maxFps, or at what the machine gives if less. With no
    // previous target (the first frame, back from minimized) or one left ahead (the clock went back between frames), it
    // counts from frameStart.
    const bool followsLastDeadline = m_frameDeadline && frameStart >= *m_frameDeadline;
    const Core::Duration deadline = (followsLastDeadline ? *m_frameDeadline : frameStart) + m_minFrameTime;
    const Core::Duration afterRender = m_clock.Now();
    if (afterRender < frameStart)
    {
        // The clock went back during the frame: the subtraction would give more than a whole frame.
        m_frameDeadline.reset();
    }
    else if (afterRender < deadline)
    {
        m_clock.SleepFor(deadline - afterRender);
        m_frameDeadline = deadline;
    }
    else
    {
        // Past the target (a long frame, or a long gap before it started): no sleep, and the next counts from here. So
        // it does not catch up with shorter frames, and what runs until the next RunFrame does not add up either.
        m_frameDeadline = afterRender;
    }
}

int GameLoop::RunFaithful(const Platform::WindowState& state, Input::InputState& input)
{
    const int steps = m_pacer.BeginFrame();
    RunSteps(steps, input);
    // The measurement already starts after the steps: the scene change does not add up.
    static_cast<void>(m_client.TakeTheSceneChange());
    // After the steps (0x63852b): their time does not accumulate.
    m_pacer.StartMeasure(m_clock);
    // The original draws the state after the steps, without interpolating. Render also presents (ILoopClient), so the
    // swap stays inside the measurement, as in 0x63852b..0x638632.
    m_lastAlpha = 0.0f;
    if (!state.minimized && !m_client.HoldsTheLastFrame())
    {
        m_client.Render(m_lastAlpha);
    }
    // Without drawing too: without the wait the accumulator does not grow and no step ever runs again.
    m_pacer.EndFrame(m_clock);
    if (!state.minimized)
    {
        m_client.FinishTheLoop();
    }
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
            // Like the original's first reader, which clears the low bit: the frame's second step no longer sees the
            // press, and what is held stays held.
            input.ConsumeEdges();
        }
    }
}
} // namespace Client::App
