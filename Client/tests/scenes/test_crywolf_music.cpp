#include "Scenes/CrywolfMusic.h"

#include "Audio/Playback.h"
#include "Kernel/WorldRuntime.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Client::Scenes::StepTheCrywolfMusic (0x430250). The stages are the bytes of the BD 00, written as the original
// compares them.
namespace
{
using Client::Scenes::StepTheCrywolfMusic;
using Client::Scenes::WorldRuntime;

constexpr std::int32_t CrywolfMap = 34;
constexpr std::int32_t AidaMap = 33;
constexpr std::int32_t MapAfterCrywolf = 35;
constexpr std::int32_t NoBanner = 0;
constexpr std::int32_t FailureBanner = 0xa;
constexpr std::int32_t SuccessBanner = 0xb;
constexpr std::string_view MapTrack = "Music\\crywolf1st.mp3";
constexpr std::string_view SecondNoticeTrack = "Music\\crywolf_before-01.ogg";
constexpr std::string_view ReadyTrack = "Music\\crywolf_ready-02.ogg";
constexpr std::string_view StartTrack = "Music\\crywolf_back-03.ogg";

using Stages = std::initializer_list<std::uint8_t>;

class RecordingPlayback final : public Client::Audio::IPlayback
{
public:
    void PlaySound(std::int32_t id, std::optional<glm::vec3>, bool) override
    {
        events.push_back(std::format("sound:{:#x}", id));
    }
    void StopSound(std::int32_t, bool) override {}
    void PlayMusic(std::string_view path, bool) override
    {
        events.push_back(std::format("play:{}", path));
        if (progressOfANewTrack.has_value())
            progress = *progressOfANewTrack;
    }
    void StopMusic() override
    {
        events.emplace_back("stop");
    }
    void SetEffectsVolume(std::uint8_t) override {}
    void SetListener(glm::vec3) override {}
    [[nodiscard]] int MusicProgressPercent() const override
    {
        return progress;
    }

    [[nodiscard]] std::string Last() const
    {
        return events.empty() ? std::string{} : events.back();
    }

    std::vector<std::string> events{};
    int progress = 0;
    std::optional<int> progressOfANewTrack{};
};

std::string Played(std::string_view track)
{
    return std::format("play:{}", track);
}

// A world with a playback device. The runtime is a quarter of MSVC's stack, so it goes on the heap.
struct Frame
{
    explicit Frame(std::int32_t map = CrywolfMap)
    {
        runtime.audio = &playback;
        runtime.map = map;
    }

    void StepAt(std::uint8_t stage)
    {
        runtime.crywolf.stage = stage;
        StepTheCrywolfMusic(runtime);
    }

    void StartWith(std::string_view track)
    {
        runtime.music.Play(&playback, track);
    }

    RecordingPlayback playback{};
    std::unique_ptr<WorldRuntime> owned = std::make_unique<WorldRuntime>();
    WorldRuntime& runtime = *owned;
};

TEST(CrywolfMusicTest, EachStagePlaysItsTrackAndStopsThePreviousStages)
{
    Frame frame;
    frame.playback.progress = 99;

    for (const std::uint8_t stage : Stages{0, 2, 3, 4})
        frame.StepAt(stage);

    EXPECT_EQ(
        frame.playback.events,
        (std::vector<std::string>{"play:Music\\crywolf1st.mp3", "stop", "play:Music\\crywolf_before-01.ogg", "stop",
                                  "play:Music\\crywolf_ready-02.ogg", "stop", "play:Music\\crywolf_back-03.ogg"}));
}

TEST(CrywolfMusicTest, AStageWithoutATrackOfItsOwnPlaysTheMaps)
{
    for (const std::uint8_t stage : Stages{0, 1, 6, 7, 0xff})
    {
        Frame frame;

        frame.StepAt(stage);

        EXPECT_EQ(frame.runtime.music.Current(), MapTrack) << "stage " << int{stage};
    }
}

TEST(CrywolfMusicTest, OnlyProgressOneHundredEndsTheTrackAndTheNextFrameStartsItAgain)
{
    struct Case
    {
        std::uint8_t stage;
        std::string_view track;
    };
    for (const auto [stage, track] :
         {Case{0, MapTrack}, Case{2, SecondNoticeTrack}, Case{3, ReadyTrack}, Case{4, StartTrack}})
    {
        Frame frame;
        for (const int progress : {99, 101})
        {
            frame.playback.progress = progress;
            frame.StepAt(stage);
        }
        EXPECT_EQ(frame.playback.events, (std::vector<std::string>{Played(track)})) << "stage " << int{stage};

        frame.playback.progress = 100;
        frame.StepAt(stage);
        EXPECT_EQ(frame.playback.events, (std::vector<std::string>{Played(track), "stop"})) << "stage " << int{stage};
        EXPECT_TRUE(frame.runtime.music.Current().empty());

        frame.playback.progress = 0;
        frame.StepAt(stage);
        EXPECT_EQ(frame.playback.events, (std::vector<std::string>{Played(track), "stop", Played(track)}))
            << "stage " << int{stage};
    }
}

// The track that ends is the one the frame has just asked for: the stream of the track it replaces is at its end.
TEST(CrywolfMusicTest, TheProgressIsReadAfterTheTrackStarts)
{
    Frame frame;
    frame.StartWith(MapTrack);
    frame.playback.progress = 100;
    frame.playback.progressOfANewTrack = 0;

    frame.StepAt(2);

    EXPECT_EQ(frame.runtime.music.Current(), SecondNoticeTrack);
}

TEST(CrywolfMusicTest, AStageThatSkipsOneReplacesTheTrackWithoutAStop)
{
    Frame frame;

    frame.StepAt(2);
    frame.StepAt(4);

    EXPECT_EQ(frame.playback.events,
              (std::vector<std::string>{"play:Music\\crywolf_before-01.ogg", "play:Music\\crywolf_back-03.ogg"}));
}

// With the failure banner of a fresh runtime, the end stage's rows end with the sound of the defeat.
TEST(CrywolfMusicTest, EveryStageWithEveryTrackPlaying)
{
    struct Row
    {
        std::uint8_t stage;
        std::string_view playing;
        std::vector<std::string> then;
    };
    const std::vector<Row> rows{
        {0, MapTrack, {}},
        {0, SecondNoticeTrack, {Played(MapTrack)}},
        {0, ReadyTrack, {Played(MapTrack)}},
        {0, StartTrack, {Played(MapTrack)}},
        {2, MapTrack, {"stop", Played(SecondNoticeTrack)}},
        {2, SecondNoticeTrack, {}},
        {2, ReadyTrack, {Played(SecondNoticeTrack)}},
        {2, StartTrack, {Played(SecondNoticeTrack)}},
        {3, MapTrack, {Played(ReadyTrack)}},
        {3, SecondNoticeTrack, {"stop", Played(ReadyTrack)}},
        {3, ReadyTrack, {}},
        {3, StartTrack, {Played(ReadyTrack)}},
        {4, MapTrack, {Played(StartTrack)}},
        {4, SecondNoticeTrack, {Played(StartTrack)}},
        {4, ReadyTrack, {"stop", Played(StartTrack)}},
        {4, StartTrack, {}},
        {5, MapTrack, {"sound:0x245"}},
        {5, SecondNoticeTrack, {"sound:0x245"}},
        {5, ReadyTrack, {"stop", "sound:0x245"}},
        {5, StartTrack, {"stop", "sound:0x245"}},
    };
    for (const Row& row : rows)
    {
        Frame frame;
        frame.StartWith(row.playing);

        frame.StepAt(row.stage);

        std::vector<std::string> expected{Played(row.playing)};
        expected.insert(expected.end(), row.then.begin(), row.then.end());
        EXPECT_EQ(frame.playback.events, expected) << "stage " << int{row.stage} << " with " << row.playing;
    }
}

TEST(CrywolfMusicTest, TheEndStageStopsTheBattleTracksAndRequestsTheResult)
{
    struct Case
    {
        std::int32_t banner;
        std::string sound;
    };
    for (const auto& [banner, sound] :
         {Case{SuccessBanner, "sound:0x244"}, Case{NoBanner, "sound:0x244"}, Case{FailureBanner, "sound:0x245"}})
    {
        Frame frame;
        frame.StartWith(StartTrack);
        frame.runtime.crywolf.ui.dword6c73dc = banner;

        frame.StepAt(5);
        EXPECT_EQ(frame.playback.events, (std::vector<std::string>{"play:Music\\crywolf_back-03.ogg", "stop", sound}));

        frame.StepAt(5);
        EXPECT_EQ(frame.playback.Last(), sound);
        EXPECT_EQ(frame.playback.events.size(), 4u);
    }
}

TEST(CrywolfMusicTest, TheResultNeedsTheMapAndTheEndStage)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t stage;
    };
    for (const auto [map, stage] :
         {Case{CrywolfMap, 4}, Case{CrywolfMap, 6}, Case{AidaMap, 5}, Case{MapAfterCrywolf, 5}})
    {
        Frame frame(map);

        frame.StepAt(stage);

        EXPECT_FALSE(frame.runtime.lastSoundId.has_value()) << "map " << map << ", stage " << int{stage};
    }
}

TEST(CrywolfMusicTest, OnAnotherMapItStopsOnlyTheMapsTrack)
{
    for (const std::uint8_t stage : Stages{0, 1, 2, 3, 4, 5, 6})
    {
        Frame withTheMapsTrack(AidaMap);
        withTheMapsTrack.StartWith(MapTrack);
        withTheMapsTrack.StepAt(stage);
        EXPECT_EQ(withTheMapsTrack.playback.events, (std::vector<std::string>{"play:Music\\crywolf1st.mp3", "stop"}))
            << "stage " << int{stage};

        for (const std::string_view track : {SecondNoticeTrack, ReadyTrack, StartTrack})
        {
            Frame withAStagesTrack(AidaMap);
            withAStagesTrack.StartWith(track);
            withAStagesTrack.StepAt(stage);
            EXPECT_EQ(withAStagesTrack.playback.events, (std::vector<std::string>{Played(track)}))
                << "stage " << int{stage} << " with " << track;
        }
    }
}

TEST(CrywolfMusicTest, WithoutAPlaybackDeviceTheResultIsStillRecorded)
{
    Frame frame;
    frame.runtime.audio = nullptr;

    frame.StepAt(3);
    EXPECT_TRUE(frame.runtime.music.Current().empty());

    frame.runtime.crywolf.ui.dword6c73dc = SuccessBanner;
    frame.StepAt(5);
    EXPECT_EQ(frame.runtime.lastSoundId, std::optional<std::int32_t>(0x244));
}
} // namespace
