#pragma once

#include "Audio/Playback.h"

#include <functional>
#include <string>
#include <string_view>

namespace Client::Audio
{
class Music
{
public:
    void Play(IPlayback* playback, std::string_view relativePath, bool force = false);
    void Stop(IPlayback* playback, std::string_view relativePath, bool force = false);
    void StopWorld(IPlayback* playback);
    void SetEnabled(bool enabled)
    {
        m_enabled = enabled;
    }
    void SetBlocked(bool blocked)
    {
        m_blocked = blocked;
    }
    [[nodiscard]] std::string_view Current() const
    {
        return m_current;
    }

    // Native ordered track-group policy. The Devias callback is evaluated only for safe map 2.
    void StepWorld(IPlayback* playback, int map, bool safe, int heroCellLayer,
                   const std::function<bool()>& inDeviasChurch);

private:
    bool m_enabled = true;
    bool m_blocked = false;
    std::string m_current{};
};
} // namespace Client::Audio
