#pragma once

#include "Kernel/FaithfulPacer.h"
#include "App/FixedStep.h"
#include "Scenes/ILoopClient.h"
#include "Kernel/PacingMode.h"
#include "Platform/WindowState.h"
#include "Core/IClock.h"
#include "Input/InputState.h"

#include <cstdint>
#include <optional>

// The game loop (CL3.2): per frame, the network (the CL4.1 Poll), the logic steps due and the drawing.
//
// In Decoupled, the frame is measured from one Now() to the next, at the start of RunFrame after the Poll, and goes to
// a FixedStep: the drawing is interpolated with its alpha. The first frame only sets the start, and a frame with a
// scene change sets it again after its steps: the time to enter a scene is not game time, as in the original, which
// does not measure the steps. With an FPS cap (maxFps, MuMain's FpsCap), after drawing it sleeps until the frame's
// target: 1 s / maxFps after the previous one's, or after where the previous one ended if it overran
// (SleepRestOfFrame). So what runs between two RunFrame does not add up, and the cap never gives fewer FPS than having
// none. In Faithful, it goes to a FaithfulPacer: the Poll, BeginFrame, those steps, StartMeasure, the drawing with
// alpha 0 and EndFrame; the Poll stays outside the measurement, and the cap is not used (Resolve does not allow it).
namespace Client::App
{
struct FrameStats
{
    std::uint64_t frames = 0;
    std::uint64_t steps = 0;
    // Decoupled only: the frames over MaxFrameTime that were clamped.
    std::uint64_t clampedFrames = 0;
    // Decoupled: FixedStep::Accumulated(); Faithful: AccumulatedMs().
    Core::Duration accumulated{};
    // Faithful only: what the last frame added.
    std::int32_t lastMeasuredMs = 0;
};

class GameLoop
{
public:
    // maxFps: the normal mode's FPS cap; 0 or less is no cap. Main passes 0 or a value from MinFpsLimit to MaxFpsLimit
    // (Resolve and EffectiveMaxFps).
    GameLoop(Core::IClock& clock, Scenes::ILoopClient& client, Kernel::PacingMode mode, int maxFps = 0);
    // One frame: a Poll, the steps due and the drawing if `state.minimized` is false and the client does not hold the
    // last frame (ILoopClient::HoldsTheLastFrame). The Poll always runs, also minimized and in a frame without steps.
    // Without drawing, Decoupled sleeps until the next step instead of spinning; in Faithful, the wait always runs.
    // Each step gets `input`, and after the first one GameLoop calls ConsumeEdges on it: a press is seen by a single
    // step, and in a frame without steps `input` is untouched and the press waits for the next (CL3.3, Q-C45). Returns
    // how many steps ran.
    int RunFrame(const Platform::WindowState& state, Input::InputState& input);
    [[nodiscard]] const FrameStats& Stats() const;
    [[nodiscard]] float LastAlpha() const;

private:
    int RunDecoupled(const Platform::WindowState& state, Input::InputState& input);
    int RunFaithful(const Platform::WindowState& state, Input::InputState& input);
    void RunSteps(int steps, Input::InputState& input);
    // The FPS cap, after drawing the frame that started at `frameStart`.
    void SleepRestOfFrame(Core::Duration frameStart);

    Core::IClock& m_clock;
    Scenes::ILoopClient& m_client;
    Kernel::PacingMode m_mode;
    // 1 s / maxFps, or 0 without a cap.
    Core::Duration m_minFrameTime{};
    // Where the next frame's target counts from: how far the last one slept, or where it ended if it overran. Empty
    // before the first, when minimizing and if the clock went back during the frame.
    std::optional<Core::Duration> m_frameDeadline;
    FixedStep m_fixedStep;
    Kernel::FaithfulPacer m_pacer;
    // Empty before the first frame.
    std::optional<Core::Duration> m_lastNow;
    FrameStats m_stats;
    float m_lastAlpha = 0.0f;
};
} // namespace Client::App
