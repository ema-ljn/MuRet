#pragma once

#include "Audio/IMusicStream.h"
#include "EngineApi.h"

#include <string>
#include <string_view>

namespace Client::Audio
{
class ENGINE_API MusicPlayer
{
public:
    // Without a stream, Play and Stop change nothing and the track never reaches its end.
    void Attach(IMusicStream* stream)
    {
        m_stream = stream;
    }
    // 0x4acfd0
    void Play(std::string_view relativePath);
    // 0x4acf60
    void Stop(std::string_view relativePath);
    // 0x4ad060
    [[nodiscard]] bool ReachedTheEnd() const;
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

private:
    IMusicStream* m_stream = nullptr;
    bool m_enabled = true;
    bool m_blocked = false;
    std::string m_current{};
};
} // namespace Client::Audio
