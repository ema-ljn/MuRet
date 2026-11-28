#pragma once

#include "Input/InputState.h"

// What the loop calls: the network, once per frame; the logic, one step at a time, with the frame's input; and the
// draw. It has its own header because Main's scene and the tests' client implement it, and neither needs the whole
// loop.
namespace Client::Scenes
{
class ILoopClient
{
public:
    virtual ~ILoopClient() = default;
    // Once per frame, before the steps, also minimized, like the packet queue the original dispatches on each turn of
    // the WinMain loop (0x4e1d50). The original dispatches it after the frame (0x4be5b3), the same in steady state:
    // between one frame and the next. In faithful mode it goes before BeginFrame, outside the measurement, like the
    // message loop (GameLoop.h).
    virtual void Poll() = 0;
    // Only the frame's first step sees the edges of `input` (WasPressed, WasReleased, Wheel): GameLoop calls
    // ConsumeEdges afterwards (InputState.h).
    virtual void Step(const Input::InputState& input) = 0;
    // Draws and presents: the swap is inside Render, so faithful mode adds it to the accumulator with the draw, like
    // the original. alpha in [0, 1): how much of the next step has already passed (Decoupled), or 0 (Faithful).
    virtual void Render(float alpha) = 0;
    // Whether this frame is not drawn: the last presented one stays on screen, as in the original while a blocking
    // connect holds the loop (the login's, Q-C85). GameLoop checks it after the steps, and the frame goes on as when
    // minimized: the network and the steps run.
    [[nodiscard]] virtual bool HoldsTheLastFrame() const = 0;
    // Whether a step changed scene since the last time it was asked (and forgets it): entering a scene (loading its
    // textures and models) runs inside a step, and the original does not measure it, because it measures from after the
    // steps (0x63852b). GameLoop checks it after the steps, in both modes.
    [[nodiscard]] virtual bool TakeTheSceneChange() = 0;
    // The end of a loop turn with the window not minimized, drawn or held: what the original does after the frame
    // (0x63945d, after 0x637700) and at the start of the next turn (WinMain), P-C61.
    virtual void FinishTheLoop() {}
};
} // namespace Client::Scenes
