#pragma once

#include "Core/IClock.h"

// The normal mode's fixed step (CL3.2, D-C4): a time accumulator spent LogicStep at a time, capped at MaxFrameTime,
// and the alpha to interpolate the drawing between the last step and the next. It starts at 0: its tests count the
// exact steps per simulated second.
namespace Client::App
{
class FixedStep
{
public:
    // Adds the frame (negative counts 0; more than MaxFrameTime counts MaxFrameTime; exactly MaxFrameTime is not
    // clamped) and returns how many steps run.
    int Advance(Core::Duration frameTime);
    // accumulated / step, clamped to std::nextafter(1.0f, 0.0f): with 39999999 ns the float would round to 1.
    [[nodiscard]] float Alpha() const;
    [[nodiscard]] Core::Duration Accumulated() const;
    [[nodiscard]] bool LastFrameWasClamped() const;

private:
    Core::Duration m_accumulated{};
    bool m_lastFrameWasClamped = false;
};
} // namespace Client::App
