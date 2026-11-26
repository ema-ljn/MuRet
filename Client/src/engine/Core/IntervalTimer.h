#pragma once

#include "Core/IClock.h"
#include "EngineApi.h"

#include <optional>

// No drift: the next one is due a period after the previous, not after when it was checked. Checked a period or more
// late (a long frame, a breakpoint), it fires once and the next is a period from then, with no catch-up burst. The
// original has a WM_TIMER, which does not pile up either (02, "Qué avanza por paso, por frame y por reloj").
namespace Client::Core
{
class ENGINE_API IntervalTimer
{
public:
    // `period` is greater than 0.
    explicit IntervalTimer(Duration period);

    // Also restarts; the first one is due at now + period.
    void Start(Duration now);
    void Stop();

    // Once per deadline. A clock that goes back is not due, and the deadline does not move.
    [[nodiscard]] bool Due(Duration now);

    [[nodiscard]] bool Running() const;

private:
    Duration m_period;
    // Empty while stopped.
    std::optional<Duration> m_deadline;
};
} // namespace Client::Core
