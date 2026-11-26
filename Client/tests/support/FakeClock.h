#pragma once

#include "Core/IClock.h"

#include <span>
#include <vector>

// Does not move on its own, so a frame "takes" what the test says, without real time.
namespace Client::Tests
{
class FakeClock final : public Core::IClock
{
public:
    Core::Duration Now() override;
    // Moves it by `duration` and records it. A sleep of 0 or less is a loop bug (the real one would sleep 0): the test
    // fails.
    void SleepFor(Core::Duration duration) override;
    // Moves it without recording; `duration` may be negative.
    void Advance(Core::Duration duration);
    // What each SleepFor asked to sleep, in order.
    [[nodiscard]] std::span<const Core::Duration> Sleeps() const;

private:
    Core::Duration m_now{};
    std::vector<Core::Duration> m_sleeps;
};
} // namespace Client::Tests
