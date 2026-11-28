#pragma once

#include "Core/IClock.h"
#include "Models/Animation.h"

#include <cstdint>

// The original's frames of a scene, counted by the logic steps it ran and not by the draw clock (#307). The original
// runs the steps it owes and then draws (02 § El ritmo, 0x637791..0x6381a5): each draw is a frame, which at 21.33 FPS
// sees 1 or 2 steps (75 every 64 frames), more on a slower machine; it never runs two frames in one draw or a frame
// without steps. Counted by the draw clock, at 60 FPS, the draw crossing the 46.875 ms mark sometimes came before its
// step and sometimes after: some 40 frames a minute saw no step and the next saw 2, and the walker's layer jerked.
namespace Client::Scenes
{
class OriginalFramesOfTheSteps
{
public:
    void Step()
    {
        ++m_steps;
    }

    // Whether this draw is a frame of the original (1) or an intermediate one (0). The scene's first one is; in
    // faithful mode (OncePerDraw), all are. Otherwise, it is the draw that brings at least one new step and
    // completes 46.875 ms of steps since the previous frame. At most one per draw: what is left of one more frame is
    // discarded, and below 21.33 FPS each draw is a frame, with more steps, like the original on a slow machine.
    [[nodiscard]] int FramesAtDraw(Models::OriginalFramePace pace);

    // What FramesAtDraw would give now, without counting the draw: whether the next one is a frame of the original. The
    // game's PrepareFrame asks it before the draw, to forget only in a frame of the original what that frame rewrites.
    [[nodiscard]] bool NextDrawIsAnOriginalFrame(Models::OriginalFramePace pace) const;

private:
    std::uint64_t m_steps = 0;
    std::uint64_t m_stepsAtTheLastDraw = 0;
    Core::Duration m_carry{};
    bool m_started = false;
};
} // namespace Client::Scenes
