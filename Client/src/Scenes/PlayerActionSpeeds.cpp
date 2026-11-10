#include "Scenes/PlayerActionSpeeds.h"

#include <bit>
#include <cstddef>

namespace Client::Scenes
{
void PlayerActionSpeeds::SetWalking(bool slowed)
{
    constexpr float WalkSpeed = std::bit_cast<float>(0x3ea8f5c3U);
    constexpr float RunSpeed = std::bit_cast<float>(0x3eae147bU);
    constexpr std::size_t FirstWalk = 13;
    constexpr std::size_t LastWalk = 20;
    constexpr std::size_t FirstRun = 22;
    constexpr std::size_t LastRun = 33;
    constexpr std::size_t RavenWalk = 73;
    constexpr std::size_t DarkHorseWalk = 75;
    const float factor = slowed ? 0.5f : 1.0f;
    for (std::size_t action = FirstWalk; action <= LastWalk; ++action)
    {
        m_speeds[action] = WalkSpeed * factor;
    }
    for (std::size_t action = FirstRun; action <= LastRun; ++action)
    {
        m_speeds[action] = RunSpeed * factor;
    }
    m_speeds[RavenWalk] = WalkSpeed * factor;
    m_speeds[DarkHorseWalk] = WalkSpeed * factor;
}

void PlayerActionSpeeds::Set(std::size_t action, float speed)
{
    if (action < m_speeds.size())
        m_speeds[action] = speed;
}

float PlayerActionSpeeds::Speed(std::size_t action, float fallback) const
{
    return action < m_speeds.size() ? m_speeds[action].value_or(fallback) : fallback;
}
} // namespace Client::Scenes
