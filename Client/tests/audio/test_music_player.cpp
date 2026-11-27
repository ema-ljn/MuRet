#include "Audio/MusicPlayer.h"
#include "Audio/Playback.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Audio
{
namespace
{
class Playback final : public IPlayback
{
public:
    void PlaySound(std::int32_t, std::optional<glm::vec3>, bool) override {}
    void StopSound(std::int32_t, bool) override {}
    void PlayMusic(std::string_view path, bool loop) override
    {
        played.emplace_back(path);
        loops.push_back(loop);
        events.emplace_back("play:" + std::string(path));
    }
    void StopMusic() override
    {
        ++stops;
        events.emplace_back("stop");
    }
    void SetEffectsVolume(std::uint8_t) override {}
    void SetListener(glm::vec3) override {}

    std::vector<std::string> played;
    std::vector<std::string> events;
    std::vector<bool> loops;
    int stops = 0;
};

TEST(MusicTest, PlaysTrackOnlyWhenItChangesAndStopsOnlyExactCurrentTrack)
{
    Music music;
    Playback playback;
    music.Play(&playback, "Music\\Noria.mp3");
    music.Play(&playback, "Music\\Noria.mp3");
    music.Stop(&playback, "Music\\noria.mp3");
    EXPECT_EQ(playback.played, (std::vector<std::string>{"Music\\Noria.mp3"}));
    ASSERT_EQ(playback.loops.size(), 1u);
    EXPECT_FALSE(playback.loops.front());
    EXPECT_EQ(playback.stops, 0);
    music.Stop(&playback, "Music\\Noria.mp3");
    EXPECT_EQ(playback.stops, 1);
    EXPECT_TRUE(music.Current().empty());
}

TEST(MusicTest, ForceBypassesDisabledButBlockedAlwaysPreventsPlay)
{
    Music music;
    Playback playback;
    music.SetEnabled(false);
    music.Play(&playback, "Music\\Noria.mp3");
    music.Play(&playback, "Music\\Noria.mp3", true);
    music.SetBlocked(true);
    music.Play(&playback, "Music\\Devias.mp3", true);
    EXPECT_EQ(playback.played.size(), 1u);
    EXPECT_EQ(music.Current(), "Music\\Noria.mp3");
}

TEST(MusicTest, ResolvesSafeWorldTracksAndKeepsMapZeroMusicWhileUnsafe)
{
    Music music;
    Playback playback;
    music.StepWorld(&playback, 0, true, 4, {});
    EXPECT_EQ(music.Current(), "Music\\Pub.mp3");
    music.StepWorld(&playback, 0, false, 0, {});
    EXPECT_EQ(playback.played.size(), 1u);
    int deviasReads = 0;
    music.StepWorld(&playback, 2, true, 0,
                    [&deviasReads]
                    {
                        ++deviasReads;
                        return true;
                    });
    EXPECT_EQ(music.Current(), "Music\\Church.mp3");
    EXPECT_EQ(deviasReads, 1);
    music.StepWorld(&playback, 3, true, 0, {});
    EXPECT_EQ(music.Current(), "Music\\Noria.mp3");
}

TEST(MusicTest, DoesNotReadDeviasPositionOutsideSafeMapTwo)
{
    Music music;
    Playback playback;
    int reads = 0;
    music.StepWorld(&playback, 2, false, 0,
                    [&reads]
                    {
                        ++reads;
                        return true;
                    });
    EXPECT_EQ(reads, 0);
    EXPECT_TRUE(music.Current().empty());
}

TEST(MusicTest, FollowsNativeMapGroupOrderWhenSwitchingBetweenPubAndDungeon)
{
    Music music;
    Playback playback;
    music.Play(&playback, "Music\\Pub.mp3");
    playback.events.clear();

    music.StepWorld(&playback, 1, false, 0, {});
    EXPECT_EQ(playback.events, (std::vector<std::string>{"stop", "play:Music\\Dungeon.mp3"}));
    playback.events.clear();

    music.StepWorld(&playback, 0, true, 4, {});
    EXPECT_EQ(playback.events, (std::vector<std::string>{"play:Music\\Pub.mp3"}));
}

TEST(MusicTest, StopsAnyKnownWorldTrackOnWorldReturn)
{
    Music music;
    Playback playback;
    music.Play(&playback, "Music\\Mutheme.mp3");
    music.StopWorld(&playback);
    EXPECT_EQ(playback.stops, 1);
    EXPECT_TRUE(music.Current().empty());
}
} // namespace
} // namespace Client::Audio
