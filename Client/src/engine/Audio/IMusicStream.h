#pragma once

#include <string_view>

namespace Client::Audio
{
// What the client asks of wzAudio.dll while it runs: wzAudioPlay, wzAudioStop and wzAudioGetStreamOffsetRange.
class IMusicStream
{
public:
    virtual ~IMusicStream() = default;
    virtual void Play(std::string_view relativePath) = 0;
    virtual void Stop() = 0;
    [[nodiscard]] virtual int ProgressPercent() const = 0;
};
} // namespace Client::Audio
