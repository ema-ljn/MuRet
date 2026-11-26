#pragma once

#include <chrono>

namespace Client::Core
{
// Integer nanoseconds, with no float rounding.
using Duration = std::chrono::nanoseconds;

// Now() is never negative and is monotonic in the real clock; the fake one can go back on purpose, never below 0 (the
// difference of two Now() does not overflow).
class IClock
{
public:
    virtual ~IClock() = default;
    virtual Duration Now() = 0;
    virtual void SleepFor(Duration duration) = 0;
};
} // namespace Client::Core
