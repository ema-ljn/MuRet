#pragma once

#include "Audio/MusicPlayer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Client::Music
{
// The slots of the original's track table, 0x6d5dcc..0x6d5e20, in its order.
enum class Track : std::uint8_t
{
    Pub,
    MuTheme,
    Church,
    Devias,
    Noria,
    Dungeon,
    Atlans,
    Icarus,
    Tarkan,
    LostTowerA,
    LostTowerB,
    Kalima,
    Castle,
    Charge,
    LastEnd,
    HuntingGround,
    Aida,
    Crywolf1st,
    CrywolfReady,
    CrywolfBefore,
    CrywolfBack,
    MainTheme
};

inline constexpr std::size_t TrackCount = static_cast<std::size_t>(Track::MainTheme) + 1;

inline constexpr std::array<std::string_view, TrackCount> TrackPaths{"Music\\Pub.mp3",
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
                                                                     "Music\\main_theme.mp3"};

[[nodiscard]] constexpr std::string_view PathOf(Track track)
{
    return TrackPaths[static_cast<std::size_t>(track)];
}

inline void Play(Audio::MusicPlayer& player, Track track)
{
    player.Play(PathOf(track));
}

inline void Stop(Audio::MusicPlayer& player, Track track)
{
    player.Stop(PathOf(track));
}
} // namespace Client::Music
