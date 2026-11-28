#include "App/FixedStep.h"

#include "Kernel/LoopTiming.h"
#include "Core/IClock.h"

#include <algorithm>
#include <cmath>

namespace Client::App
{
int FixedStep::Advance(Core::Duration frameTime)
{
    // A clock that goes backwards cannot leave the accumulator negative: the frame counts 0.
    frameTime = std::max(frameTime, Core::Duration::zero());
    m_lastFrameWasClamped = frameTime > Kernel::MaxFrameTime;
    frameTime = std::min(frameTime, Kernel::MaxFrameTime);

    m_accumulated += frameTime;
    // With the cap, the accumulator stays below MaxFrameTime + LogicStep: 25 steps at most.
    const auto steps = m_accumulated / Kernel::LogicStep;
    m_accumulated -= steps * Kernel::LogicStep;
    return static_cast<int>(steps);
}

float FixedStep::Alpha() const
{
    // A double is precise enough for a step's nanoseconds; the resulting float can round to 1, and the clamp keeps it
    // below.
    const double fraction = static_cast<double>(m_accumulated.count()) / static_cast<double>(Kernel::LogicStep.count());
    return std::min(static_cast<float>(fraction), std::nextafter(1.0f, 0.0f));
}

Core::Duration FixedStep::Accumulated() const
{
    return m_accumulated;
}

bool FixedStep::LastFrameWasClamped() const
{
    return m_lastFrameWasClamped;
}
} // namespace Client::App
