#include "Audio/MusicPlayer.h"

#include <array>
#include <functional>
#include <string_view>

namespace Client::Audio
{
namespace
{
// wzAudioGetStreamOffsetRange's percent when the stream is at its end.
constexpr int TrackEnd = 100;
constexpr std::string_view LoginTheme = "Music\\main_theme.mp3";
constexpr std::string_view PubTheme = "Music\\Pub.mp3";
constexpr std::array<std::string_view, 22> WorldTracks{"Music\\Pub.mp3",
                                                       "Music\\Mutheme.mp3",
                                                       "Music\\Church.mp3",
                                                       "Music\\Devias.mp3",
                                                       "Music\\Noria.mp3",
                                                       "Music\\Dungeon.mp3",
                                                       "Music\\atlans.mp3",
                                                       "Music\\icarus.mp3",
                                                       "Music\\tarkan.mp3",
                                                       "Music\\lost_tower_a.mp3",
                                                       "Music\\lost_tower_b.mp3",
                                                       "Music\\kalima.mp3",
                                                       "Music\\castle.mp3",
                                                       "Music\\charge.mp3",
                                                       "Music\\lastend.mp3",
                                                       "Music\\huntingground.mp3",
                                                       "Music\\Aida.mp3",
                                                       "Music\\crywolf1st.mp3",
                                                       "Music\\crywolf_ready-02.ogg",
                                                       "Music\\crywolf_before-01.ogg",
                                                       "Music\\crywolf_back-03.ogg",
                                                       LoginTheme};

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

void MusicPlayer::StopWorld()
{
    for (const std::string_view track : WorldTracks)
        Stop(track);
}

void MusicPlayer::StepWorld(int map, bool safe, int heroCellLayer, const std::function<bool()>& inDeviasChurch)
{
    // The native loop's path groups run in this order. Each failed map comparison stops only that
    // map's path; retaining the order matters because Play replaces the shared stream immediately.
    if (map == 0)
    {
        if (safe)
            Play(heroCellLayer == 4 ? PubTheme : LoginTheme);
    }
    else
    {
        Stop(PubTheme);
        Stop(LoginTheme);
    }

    if (map == 2)
    {
        if (safe)
            Play(inDeviasChurch && inDeviasChurch() ? "Music\\Church.mp3" : "Music\\Devias.mp3");
    }
    else
    {
        Stop("Music\\Church.mp3");
        Stop("Music\\Devias.mp3");
    }

    if (map == 3)
    {
        if (safe)
            Play("Music\\Noria.mp3");
    }
    else
        Stop("Music\\Noria.mp3");

    if (map == 1 || map == 5)
        Play("Music\\Dungeon.mp3");
    else
        Stop("Music\\Dungeon.mp3");

    if (map == 7)
        Play("Music\\atlans.mp3");
    else
        Stop("Music\\atlans.mp3");

    if (map == 10)
        Play("Music\\icarus.mp3");
    else
        Stop("Music\\icarus.mp3");

    if (map == 8)
        Play("Music\\tarkan.mp3");
    else
        Stop("Music\\tarkan.mp3");

    if (map == 4)
        Play("Music\\lost_tower_a.mp3");
    else
        Stop("Music\\lost_tower_a.mp3");

    if ((map >= 24 && map <= 29) || map == 36)
        Play("Music\\kalima.mp3");
    else
        Stop("Music\\kalima.mp3");

    if (map == 31)
        Play("Music\\huntingground.mp3");
    else
        Stop("Music\\huntingground.mp3");

    if (map == 33)
        Play("Music\\Aida.mp3");
    else
        Stop("Music\\Aida.mp3");
}
} // namespace Client::Audio
