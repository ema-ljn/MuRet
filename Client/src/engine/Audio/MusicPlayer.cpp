#include "Audio/MusicPlayer.h"

#include <string_view>

namespace Client::Audio
{
namespace
{
// wzAudioGetStreamOffsetRange's percent when the stream is at its end.
constexpr int TrackEnd = 100;
} // namespace

void MusicPlayer::Play(std::string_view relativePath)
{
    if (m_stream == nullptr || relativePath.empty() || m_blocked || !m_enabled || relativePath == m_current)
        return;
    m_stream->Play(relativePath);
    m_current.assign(relativePath);
}

void MusicPlayer::Stop(std::string_view relativePath)
{
    if (m_stream == nullptr || relativePath.empty() || !m_enabled || relativePath != m_current)
        return;
    m_stream->Stop();
    m_current = {};
}

bool MusicPlayer::ReachedTheEnd() const
{
    return m_stream != nullptr && m_stream->ProgressPercent() == TrackEnd;
}
} // namespace Client::Audio
