#include "Audio/IMusicStream.h"
#include "Audio/MusicPlayer.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace Client::Audio
{
namespace
{
class RecordingStream final : public IMusicStream
{
public:
    void Play(std::string_view path) override
    {
        played.emplace_back(path);
        events.emplace_back("play:" + std::string(path));
    }
    void Stop() override
    {
        ++stops;
        events.emplace_back("stop");
    }
    [[nodiscard]] int ProgressPercent() const override
    {
        return progress;
    }

    std::vector<std::string> played;
    std::vector<std::string> events;
    int stops = 0;
    int progress = 0;
};

struct Attached
{
    Attached()
    {
        music.Attach(&stream);
    }

    RecordingStream stream;
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

TEST(MusicPlayerTest, ResolvesSafeWorldTracksAndKeepsMapZeroMusicWhileUnsafe)
{
    Attached player;
    player.music.StepWorld(0, true, 4, {});
    EXPECT_EQ(player.music.Current(), "Music\\Pub.mp3");
    player.music.StepWorld(0, false, 0, {});
    EXPECT_EQ(player.stream.played.size(), 1u);
    int deviasReads = 0;
    player.music.StepWorld(2, true, 0,
                           [&deviasReads]
                           {
                               ++deviasReads;
                               return true;
                           });
    EXPECT_EQ(player.music.Current(), "Music\\Church.mp3");
    EXPECT_EQ(deviasReads, 1);
    player.music.StepWorld(3, true, 0, {});
    EXPECT_EQ(player.music.Current(), "Music\\Noria.mp3");
}

TEST(MusicPlayerTest, DoesNotReadDeviasPositionOutsideSafeMapTwo)
{
    Attached player;
    int reads = 0;
    player.music.StepWorld(2, false, 0,
                           [&reads]
                           {
                               ++reads;
                               return true;
                           });
    EXPECT_EQ(reads, 0);
    EXPECT_TRUE(player.music.Current().empty());
}

TEST(MusicPlayerTest, FollowsNativeMapGroupOrderWhenSwitchingBetweenPubAndDungeon)
{
    Attached player;
    player.music.Play("Music\\Pub.mp3");
    player.stream.events.clear();

    player.music.StepWorld(1, false, 0, {});
    EXPECT_EQ(player.stream.events, (std::vector<std::string>{"stop", "play:Music\\Dungeon.mp3"}));
    player.stream.events.clear();

    player.music.StepWorld(0, true, 4, {});
    EXPECT_EQ(player.stream.events, (std::vector<std::string>{"play:Music\\Pub.mp3"}));
}

TEST(MusicPlayerTest, StopsAnyKnownWorldTrackOnWorldReturn)
{
    Attached player;
    player.music.Play("Music\\Mutheme.mp3");
    player.music.StopWorld();
    EXPECT_EQ(player.stream.stops, 1);
    EXPECT_TRUE(player.music.Current().empty());
}
} // namespace
} // namespace Client::Audio
