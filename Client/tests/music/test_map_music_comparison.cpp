#include "Audio/MusicPlayer.h"
#include "Music/MapMusic.h"
#include "Music/Tracks.h"
#include "support/RecordingMusicStream.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Music
{
namespace
{
struct Heard
{
    std::vector<std::string> events;
    std::string current;
    int churchCalls = 0;
};

struct Case
{
    std::optional<std::string_view> start;
    std::int32_t map = 0;
    bool safe = false;
    std::int32_t layer = 0;
    std::optional<bool> church;
};

Audio::MusicPlayer StartedPlayer(Tests::RecordingMusicStream& stream, const Case& c)
{
    Audio::MusicPlayer player;
    player.Attach(&stream);
    if (c.start)
        player.Play(*c.start);
    stream.events.clear();
    return player;
}

template <typename Step> Heard RunCase(const Case& c, Step step)
{
    Tests::RecordingMusicStream stream;
    Audio::MusicPlayer player = StartedPlayer(stream, c);
    Heard run;
    std::function<bool()> church;
    if (c.church)
        church = [&run, answer = *c.church]
        {
            ++run.churchCalls;
            return answer;
        };
    step(player, c, church);
    run.events = stream.events;
    run.current = std::string(player.Current());
    return run;
}

std::vector<std::optional<std::string_view>> Starts()
{
    std::vector<std::optional<std::string_view>> starts{std::nullopt};
    for (const std::string_view path : TrackPaths)
        starts.emplace_back(path);
    return starts;
}

std::string Describe(const Case& c)
{
    return "start " + std::string(c.start.value_or("none")) + " map " + std::to_string(c.map) + " safe " +
           std::to_string(c.safe) + " layer " + std::to_string(c.layer) + " church " +
           (c.church ? std::to_string(*c.church) : std::string("none"));
}

TEST(MapMusicComparisonTest, StepTheMapMusicDoesWhatStepWorldDid)
{
    int cases = 0;
    for (const auto start : Starts())
        for (std::int32_t map = 0; map <= 255; ++map)
        {
            std::vector<std::int32_t> layers{0, 4};
            if (map == 0)
            {
                layers.clear();
                for (std::int32_t layer = 0; layer <= 255; ++layer)
                    layers.push_back(layer);
            }
            for (const bool safe : {false, true})
                for (const std::int32_t layer : layers)
                    for (const std::optional<bool> church :
                         {std::optional<bool>{}, std::optional<bool>{false}, std::optional<bool>{true}})
                    {
                        const Case c{.start = start, .map = map, .safe = safe, .layer = layer, .church = church};
                        const Heard before = RunCase(
                            c, [](Audio::MusicPlayer& player, const Case& run, const std::function<bool()>& inTheChurch)
                            { player.StepWorld(run.map, run.safe, run.layer, inTheChurch); });
                        const Heard after = RunCase(
                            c, [](Audio::MusicPlayer& player, const Case& run, const std::function<bool()>& inTheChurch)
                            { StepTheMapMusic(player, run.map, run.safe, run.layer, inTheChurch); });
                        ASSERT_EQ(after.events, before.events) << Describe(c);
                        ASSERT_EQ(after.current, before.current) << Describe(c);
                        ASSERT_EQ(after.churchCalls, before.churchCalls) << Describe(c);
                        ++cases;
                    }
        }
    EXPECT_EQ(cases, 23 * (256 + 255 * 2) * 2 * 3);
}

TEST(MapMusicComparisonTest, StopTheWorldMusicDoesWhatStopWorldDid)
{
    for (const auto start : Starts())
    {
        const Case c{.start = start, .map = 0, .safe = false, .layer = 0, .church = std::nullopt};
        const Heard before = RunCase(c, [](Audio::MusicPlayer& player, const Case&, const std::function<bool()>&)
                                     { player.StopWorld(); });
        const Heard after = RunCase(c, [](Audio::MusicPlayer& player, const Case&, const std::function<bool()>&)
                                    { StopTheWorldMusic(player); });
        ASSERT_EQ(after.events, before.events) << Describe(c);
        ASSERT_EQ(after.current, before.current) << Describe(c);
    }
}
} // namespace
} // namespace Client::Music
