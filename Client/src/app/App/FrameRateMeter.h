#pragma once

#include "App/GameLoop.h"
#include "Core/IClock.h"

#include <chrono>
#include <cstdint>
#include <optional>

// What --stats prints (CL3.2): the FPS and the steps per second, averaged over StatsInterval. It measures and does
// not print: main writes it, and the tests read it without output.
namespace Client::App
{
inline constexpr Core::Duration StatsInterval = std::chrono::seconds{5};

struct FrameRates
{
    double framesPerSecond = 0.0;
    double stepsPerSecond = 0.0;
};

class FrameRateMeter
{
public:
    // `start`: the Now() the first average counts from, with FrameStats at 0.
    explicit FrameRateMeter(Core::Duration start);
    // If StatsInterval or more passed since the last average, that stretch's frames and steps per second, and the next
    // counts from `now`. Otherwise empty (also if the clock went back).
    [[nodiscard]] std::optional<FrameRates> Update(Core::Duration now, const FrameStats& stats);

private:
    Core::Duration m_last;
    std::uint64_t m_frames = 0;
    std::uint64_t m_steps = 0;
};
} // namespace Client::App
