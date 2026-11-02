#include "support/FakeClock.h"

namespace Client::Tests
{
Core::Duration FakeClock::Now()
{
    return m_now;
}

void FakeClock::SleepFor(Core::Duration duration)
{
    m_now += duration;
    m_sleeps.push_back(duration);
}

void FakeClock::Advance(Core::Duration duration)
{
    m_now += duration;
}

std::span<const Core::Duration> FakeClock::Sleeps() const
{
    return m_sleeps;
}
} // namespace Client::Tests
