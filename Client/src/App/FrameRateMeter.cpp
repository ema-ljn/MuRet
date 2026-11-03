#include "App/FrameRateMeter.h"

#include "App/GameLoop.h"
#include "Core/IClock.h"

#include <chrono>
#include <optional>

namespace Client::App
{
FrameRateMeter::FrameRateMeter(Core::Duration start) : m_last(start) {}

std::optional<FrameRates> FrameRateMeter::Update(Core::Duration now, const FrameStats& stats)
{
    const Core::Duration elapsed = now - m_last;
    if (elapsed < StatsInterval)
    {
        return std::nullopt;
    }
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const FrameRates rates{static_cast<double>(stats.frames - m_frames) / seconds,
                           static_cast<double>(stats.steps - m_steps) / seconds};
    m_last = now;
    m_frames = stats.frames;
    m_steps = stats.steps;
    return rates;
}
} // namespace Client::App
