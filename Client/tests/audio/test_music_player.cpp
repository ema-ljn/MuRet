#include "Audio/MusicPlayer.h"
#include "support/RecordingMusicStream.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace Client::Audio
{
namespace
{
struct Attached
{
    Attached()
    {
        music.Attach(&stream);
    }

    Tests::RecordingMusicStream stream;
    MusicPlayer music;
};

TEST(MusicPlayerTest, PlaysTrackOnlyWhenItChangesAndStopsOnlyExactCurrentTrack)
{
    Attached player;
    player.music.Play("Music\\Noria.mp3");
    player.music.Play("Music\\Noria.mp3");
    player.music.Stop("Music\\noria.mp3");
    EXPECT_EQ(player.stream.played, (std::vector<std::string>{"Music\\Noria.mp3"}));
    EXPECT_EQ(player.stream.stops, 0);
    player.music.Stop("Music\\Noria.mp3");
    EXPECT_EQ(player.stream.stops, 1);
    EXPECT_TRUE(player.music.Current().empty());
}

TEST(MusicPlayerTest, DisabledPlaysAndStopsNothing)
{
    Attached player;
    player.music.SetEnabled(false);
    player.music.Play("Music\\Noria.mp3");
    EXPECT_TRUE(player.stream.played.empty());

    player.music.SetEnabled(true);
    player.music.Play("Music\\Noria.mp3");
    player.music.SetEnabled(false);
    player.music.Stop("Music\\Noria.mp3");
    EXPECT_EQ(player.stream.stops, 0);
    EXPECT_EQ(player.music.Current(), "Music\\Noria.mp3");
}

TEST(MusicPlayerTest, BlockedPlaysNothingButStillStops)
{
    Attached player;
    player.music.Play("Music\\Noria.mp3");
    player.music.SetBlocked(true);
    player.music.Play("Music\\Devias.mp3");
    EXPECT_EQ(player.stream.played, (std::vector<std::string>{"Music\\Noria.mp3"}));
    player.music.Stop("Music\\Noria.mp3");
    EXPECT_EQ(player.stream.stops, 1);
}

TEST(MusicPlayerTest, ReachesTheEndOnlyAtOneHundredPercent)
{
    Attached player;
    for (const int progress : {0, 99, 101})
    {
        player.stream.progress = progress;
        EXPECT_FALSE(player.music.ReachedTheEnd()) << progress;
    }
    player.stream.progress = 100;
    EXPECT_TRUE(player.music.ReachedTheEnd());
}

TEST(MusicPlayerTest, WithoutAStreamNothingPlaysAndNoTrackEnds)
{
    MusicPlayer music;
    music.Play("Music\\Noria.mp3");
    EXPECT_TRUE(music.Current().empty());
    EXPECT_FALSE(music.ReachedTheEnd());
}
} // namespace
} // namespace Client::Audio
