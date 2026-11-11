#include "Scenes/OriginalFramesOfTheSteps.h"

#include "App/LoopTiming.h"
#include "Core/IClock.h"
#include "Render/Models/Animation.h"

#include <cstdint>

namespace Client::Scenes
{
int OriginalFramesOfTheSteps::FramesAtDraw(Render::Models::OriginalFramePace pace)
{
    const Core::Duration stepClock = App::LogicStep * static_cast<std::int64_t>(m_steps);
    return m_frames.FramesAt(stepClock, pace);
}
} // namespace Client::Scenes
