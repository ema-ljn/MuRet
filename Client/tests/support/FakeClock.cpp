#include "support/FakeClock.h"

#include "Core/IClock.h"

#include <gtest/gtest.h>

#include <span>

namespace Client::Tests
{
Core::Duration FakeClock::Now()
{
    return m_now;
}

void FakeClock::SleepFor(Core::Duration duration)
{
    if (duration <= Core::Duration::zero())
    {
        ADD_FAILURE() << "SleepFor con " << duration.count() << " ns: el bucle no duerme 0 ni para atrás";
    }
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
