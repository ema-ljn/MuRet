#include "Crt/CrtRandom.h"

#include <cstdint>

namespace Client::Crt
{
int CrtRandom::Next() noexcept
{
    m_state = m_state * 0x343fdu + 0x269ec3u;
    return static_cast<int>((m_state >> 16) & 0x7fffu);
}

void CrtRandom::Discard(std::uint32_t count) noexcept
{
    // `step` advances 2^k states and applies when bit k of `count` is set; (a1, c1) after (a2, c2) is
    // (a1 * a2, a1 * c2 + c1).
    std::uint32_t stepMultiplier = 0x343fdu;
    std::uint32_t stepIncrement = 0x269ec3u;
    while (count != 0)
    {
        if ((count & 1u) != 0)
            m_state = m_state * stepMultiplier + stepIncrement;
        stepIncrement = stepMultiplier * stepIncrement + stepIncrement;
        stepMultiplier *= stepMultiplier;
        count >>= 1u;
    }
}
} // namespace Client::Crt
