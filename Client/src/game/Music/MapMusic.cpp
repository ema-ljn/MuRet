#include "Music/MapMusic.h"

#include "Audio/MusicPlayer.h"
#include "Maps/MapId.h"
#include "Music/Tracks.h"

#include <cstdint>
#include <functional>
#include <string_view>

namespace Client::Music
{
namespace
{
// The hero's cell layer inside Lorencia's pub.
constexpr std::int32_t PubLayer = 4;

void PlayOrStop(Audio::MusicPlayer& player, Track track, bool play)
{
    if (play)
        Play(player, track);
    else
        Stop(player, track);
}
} // namespace

void StepTheMapMusic(Audio::MusicPlayer& player, std::int32_t map, bool safeZone, std::int32_t heroCellLayer,
                     const std::function<bool()>& inTheDeviasChurch)
{
    // A play replaces the stream at once, so a group's stops must run before the next group's play.
    if (map == Maps::LorenciaMap) // 0x6388b0
    {
        if (safeZone)
            Play(player, heroCellLayer == PubLayer ? Track::Pub : Track::MainTheme);
    }
    else
    {
        Stop(player, Track::Pub);
        Stop(player, Track::MainTheme);
    }

    if (map == Maps::DeviasMap) // 0x63891b
    {
        if (safeZone)
            Play(player, inTheDeviasChurch && inTheDeviasChurch() ? Track::Church : Track::Devias);
    }
    else
    {
        Stop(player, Track::Church);
        Stop(player, Track::Devias);
    }

    if (map == Maps::NoriaMap) // 0x639239
    {
        if (safeZone)
            Play(player, Track::Noria);
    }
    else
        Stop(player, Track::Noria);

    PlayOrStop(player, Track::Dungeon, map == Maps::DungeonMap || map == Maps::ExileMap); // 0x639278
    PlayOrStop(player, Track::Atlans, map == Maps::AtlansMap);                            // 0x6392ad
    PlayOrStop(player, Track::Icarus, map == Maps::IcarusMap);                            // 0x6392d9
    PlayOrStop(player, Track::Tarkan, map == Maps::TarkanMap);                            // 0x639306
    PlayOrStop(player, Track::LostTowerA, map == Maps::LostTowerMap);                     // 0x639332
    PlayOrStop(player, Track::Kalima, Maps::IsKalimaMap(map));                            // 0x63935e
    PlayOrStop(player, Track::HuntingGround, map == Maps::LandOfTrialsMap);               // 0x6393c2
    PlayOrStop(player, Track::Aida, map == Maps::AidaMap);                                // 0x6393ef
}

void StopTheWorldMusic(Audio::MusicPlayer& player)
{
    for (const std::string_view path : TrackPaths)
        player.Stop(path);
}
} // namespace Client::Music
