#include "Core/IntervalTimer.h"

#include "Core/IClock.h"

#include <cassert>

namespace Client::Core
{
IntervalTimer::IntervalTimer(Duration period) : m_period(period)
{
    assert(period > Duration::zero() && "un periodo de 0 vencería en cada Due");
}

void IntervalTimer::Start(Duration now)
{
    m_deadline = now + m_period;
}

void IntervalTimer::Stop()
{
    m_deadline.reset();
}

bool IntervalTimer::Due(Duration now)
{
    if (!m_deadline || now < *m_deadline)
    {
        return false;
    }
    // A period or more late, the next one counts from now; otherwise it stays on the grid.
    const Duration next = *m_deadline + m_period;
    m_deadline = now < next ? next : now + m_period;
    return true;
}

bool IntervalTimer::Running() const
{
    return m_deadline.has_value();
}
} // namespace Client::Core
