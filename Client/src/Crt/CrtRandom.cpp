#include "Crt/CrtRandom.h"

#include <cstdint>

namespace Client::Crt
{
namespace
{
// The linear congruential step: state * multiplier + increment.
struct LcgStep
{
    std::uint32_t multiplier;
    std::uint32_t increment;

    [[nodiscard]] constexpr std::uint32_t Apply(std::uint32_t state) const noexcept
    {
        return state * multiplier + increment;
    }

    [[nodiscard]] constexpr LcgStep Twice() const noexcept
    {
        return {.multiplier = multiplier * multiplier, .increment = multiplier * increment + increment};
    }
};

constexpr LcgStep NextStep{.multiplier = 0x343fdu, .increment = 0x269ec3u};
constexpr unsigned ValueShift = 16;
constexpr std::uint32_t MaxValue = 0x7fffu;
} // namespace

int CrtRandom::Next() noexcept
{
    m_state = NextStep.Apply(m_state);
    return static_cast<int>((m_state >> ValueShift) & MaxValue);
}

void CrtRandom::Discard(std::uint32_t count) noexcept
{
    // Bit k of `count` applies the step of 2^k calls.
    LcgStep step = NextStep;
    while (count != 0)
    {
        if ((count & 1u) != 0)
            m_state = step.Apply(m_state);
        step = step.Twice();
        count >>= 1u;
    }
}
} // namespace Client::Crt
