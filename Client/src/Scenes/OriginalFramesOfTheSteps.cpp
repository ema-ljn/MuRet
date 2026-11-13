#include "Scenes/OriginalFramesOfTheSteps.h"

#include "App/LoopTiming.h"
#include "Core/IClock.h"
#include "Render/Models/Animation.h"

#include <cstdint>

namespace Client::Scenes
{
int OriginalFramesOfTheSteps::FramesAtDraw(Render::Models::OriginalFramePace pace)
{
    const std::uint64_t newSteps = m_steps - m_stepsAtTheLastDraw;
    m_stepsAtTheLastDraw = m_steps;
    if (!m_started || pace == Render::Models::OriginalFramePace::OncePerDraw)
    {
        m_started = true;
        m_carry = Core::Duration::zero();
        return 1;
    }
    m_carry += App::LogicStep * static_cast<std::int64_t>(newSteps);
    if (newSteps == 0 || m_carry < Render::Models::OriginalFrameTime)
        return 0;
    m_carry -= Render::Models::OriginalFrameTime;
    if (m_carry >= Render::Models::OriginalFrameTime)
        m_carry %= Render::Models::OriginalFrameTime;
    return 1;
}

bool OriginalFramesOfTheSteps::NextDrawIsAnOriginalFrame(Render::Models::OriginalFramePace pace) const
{
    if (!m_started || pace == Render::Models::OriginalFramePace::OncePerDraw)
        return true;
    const std::uint64_t newSteps = m_steps - m_stepsAtTheLastDraw;
    return newSteps != 0 &&
           m_carry + (App::LogicStep * static_cast<std::int64_t>(newSteps)) >= Render::Models::OriginalFrameTime;
}
} // namespace Client::Scenes
