#include "Audio/MusicPlayer.h"
#include "Maps/MapId.h"
#include "Music/MapMusic.h"
#include "support/RecordingMusicStream.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Client::Music
{
namespace
{
constexpr std::int32_t PubLayer = 4;

struct Attached
{
    Attached()
    {
        music.Attach(&stream);
    }

    Tests::RecordingMusicStream stream;
    Audio::MusicPlayer music;
};

TEST(MapMusicTest, ResolvesSafeWorldTracksAndKeepsMapZeroMusicWhileUnsafe)
{
    Attached player;
    StepTheMapMusic(player.music, Maps::LorenciaMap, true, PubLayer, {});
    EXPECT_EQ(player.music.Current(), "Music\\Pub.mp3");
    StepTheMapMusic(player.music, Maps::LorenciaMap, false, 0, {});
    EXPECT_EQ(player.stream.played.size(), 1u);
    int deviasReads = 0;
    StepTheMapMusic(player.music, Maps::DeviasMap, true, 0,
                    [&deviasReads]
                    {
                        ++deviasReads;
                        return true;
                    });
    EXPECT_EQ(player.music.Current(), "Music\\Church.mp3");
    EXPECT_EQ(deviasReads, 1);
    StepTheMapMusic(player.music, Maps::NoriaMap, true, 0, {});
    EXPECT_EQ(player.music.Current(), "Music\\Noria.mp3");
}

TEST(MapMusicTest, DoesNotReadDeviasPositionOutsideSafeMapTwo)
{
    Attached player;
    int reads = 0;
    StepTheMapMusic(player.music, Maps::DeviasMap, false, 0,
                    [&reads]
                    {
                        ++reads;
                        return true;
                    });
    EXPECT_EQ(reads, 0);
    EXPECT_TRUE(player.music.Current().empty());
}

TEST(MapMusicTest, FollowsNativeMapGroupOrderWhenSwitchingBetweenPubAndDungeon)
{
    Attached player;
    player.music.Play("Music\\Pub.mp3");
    player.stream.events.clear();

    StepTheMapMusic(player.music, Maps::DungeonMap, false, 0, {});
    EXPECT_EQ(player.stream.events, (std::vector<std::string>{"stop", "play:Music\\Dungeon.mp3"}));
    player.stream.events.clear();

    StepTheMapMusic(player.music, Maps::LorenciaMap, true, PubLayer, {});
    EXPECT_EQ(player.stream.events, (std::vector<std::string>{"play:Music\\Pub.mp3"}));
}

TEST(MapMusicTest, StopsAnyKnownWorldTrackOnWorldReturn)
{
    Attached player;
    player.music.Play("Music\\Mutheme.mp3");
    StopTheWorldMusic(player.music);
    EXPECT_EQ(player.stream.stops, 1);
    EXPECT_TRUE(player.music.Current().empty());
}
} // namespace
} // namespace Client::Music
