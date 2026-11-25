#include "Audio/Music.h"
#include "Audio/Playback.h"

#include <array>
#include <functional>
#include <string_view>

namespace Client::Audio
{
namespace
{
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

void Music::Play(IPlayback* playback, std::string_view relativePath, bool force)
{
    if (playback == nullptr || relativePath.empty() || m_blocked || (!force && !m_enabled) || relativePath == m_current)
        return;
    playback->PlayMusic(relativePath, false);
    m_current.assign(relativePath);
}

void Music::Stop(IPlayback* playback, std::string_view relativePath, bool force)
{
    if (playback == nullptr || relativePath.empty() || (!force && !m_enabled) || relativePath != m_current)
        return;
    playback->StopMusic();
    m_current = {};
}

void Music::StopWorld(IPlayback* playback)
{
    for (const std::string_view track : WorldTracks)
        Stop(playback, track);
}

void Music::StepWorld(IPlayback* playback, int map, bool safe, int heroCellLayer,
                      const std::function<bool()>& inDeviasChurch)
{
    // The native loop's path groups run in this order. Each failed map comparison stops only that
    // map's path; retaining the order matters because Play replaces the shared stream immediately.
    if (map == 0)
    {
        if (safe)
            Play(playback, heroCellLayer == 4 ? PubTheme : LoginTheme);
    }
    else
    {
        Stop(playback, PubTheme);
        Stop(playback, LoginTheme);
    }

    if (map == 2)
    {
        if (safe)
            Play(playback, inDeviasChurch && inDeviasChurch() ? "Music\\Church.mp3" : "Music\\Devias.mp3");
    }
    else
    {
        Stop(playback, "Music\\Church.mp3");
        Stop(playback, "Music\\Devias.mp3");
    }

    if (map == 3)
    {
        if (safe)
            Play(playback, "Music\\Noria.mp3");
    }
    else
        Stop(playback, "Music\\Noria.mp3");

    if (map == 1 || map == 5)
        Play(playback, "Music\\Dungeon.mp3");
    else
        Stop(playback, "Music\\Dungeon.mp3");

    if (map == 7)
        Play(playback, "Music\\atlans.mp3");
    else
        Stop(playback, "Music\\atlans.mp3");

    if (map == 10)
        Play(playback, "Music\\icarus.mp3");
    else
        Stop(playback, "Music\\icarus.mp3");

    if (map == 8)
        Play(playback, "Music\\tarkan.mp3");
    else
        Stop(playback, "Music\\tarkan.mp3");

    if (map == 4)
        Play(playback, "Music\\lost_tower_a.mp3");
    else
        Stop(playback, "Music\\lost_tower_a.mp3");

    if ((map >= 24 && map <= 29) || map == 36)
        Play(playback, "Music\\kalima.mp3");
    else
        Stop(playback, "Music\\kalima.mp3");

    if (map == 31)
        Play(playback, "Music\\huntingground.mp3");
    else
        Stop(playback, "Music\\huntingground.mp3");

    if (map == 33)
        Play(playback, "Music\\Aida.mp3");
    else
        Stop(playback, "Music\\Aida.mp3");
}
} // namespace Client::Audio
