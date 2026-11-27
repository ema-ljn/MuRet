#include "Crt/CrtRandom.h"
#include "Kernel/WorldRuntime.h"
#include "Audio/Playback.h"
#include "Scenes/WinMainLoop.h"
#include "UI/ChatInput.h"
#include "UI/OldWindows.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
class RecordingPlayback final : public Audio::IPlayback
{
public:
    void PlaySound(std::int32_t id, std::optional<glm::vec3> origin, bool loop) override
    {
        sounds.push_back(id);
        origins.push_back(origin);
        loops.push_back(loop);
    }
    void StopSound(std::int32_t id, bool rewind) override
    {
        stops.emplace_back(id, rewind);
    }
    void PlayMusic(std::string_view, bool) override {}
    void StopMusic() override {}
    void SetEffectsVolume(std::uint8_t level) override
    {
        volume = level;
    }
    void SetListener(glm::vec3) override {}

    std::vector<std::int32_t> sounds{};
    std::vector<std::optional<glm::vec3>> origins{};
    std::vector<bool> loops{};
    std::vector<std::pair<std::int32_t, bool>> stops{};
    std::optional<std::uint8_t> volume{};
};

TEST(WorldRuntimeAudioTest, DispatchesEverySoundSynchronouslyInOrder)
{
    RecordingPlayback playback;
    Kernel::WorldRuntime runtime;
    runtime.audio = &playback;

    runtime.RequestSound(Client::Audio::SoundId{0x19});
    runtime.RequestSound(Client::Audio::SoundId{0x86}, glm::vec3{1.0f, 2.0f, 3.0f}, true);

    EXPECT_EQ(playback.sounds, (std::vector<std::int32_t>{0x19, 0x86}));
    EXPECT_FALSE(playback.origins[0].has_value());
    ASSERT_TRUE(playback.origins[1].has_value());
    EXPECT_EQ(playback.origins[1]->x, 1.0f);
    EXPECT_EQ(playback.origins[1]->y, 2.0f);
    EXPECT_EQ(playback.origins[1]->z, 3.0f);
    EXPECT_EQ(playback.loops, (std::vector<bool>{false, true}));
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x86});
}

TEST(WorldRuntimeAudioTest, UpdatesDiagnosticWithoutAnAudioSink)
{
    Kernel::WorldRuntime runtime;

    runtime.RequestSound(Client::Audio::SoundId{0x19});
    runtime.SetSoundVolume(0);

    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
}

TEST(WorldRuntimeAudioTest, ForwardsEffectsVolumeIndependently)
{
    RecordingPlayback playback;
    Kernel::WorldRuntime runtime;
    runtime.audio = &playback;

    runtime.SetSoundVolume(9);

    EXPECT_EQ(playback.volume, 9);
}

TEST(WorldRuntimeAudioTest, TheInnStopsRainAndAtlantisStartsOnlyItsOwnAmbience)
{
    RecordingPlayback playback;
    Kernel::WorldRuntime runtime;
    runtime.audio = &playback;
    Crt::CrtRandom random(1);
    runtime.map = 0;
    runtime.heroCellLayer = 4;
    runtime.leafWeather.current = 1;
    StepWorldAmbience(runtime, random);

    EXPECT_TRUE(playback.sounds.empty());
    EXPECT_EQ(playback.stops,
              (std::vector<std::pair<std::int32_t, bool>>{
                  {0, true}, {1, true}, {3, true}, {2, true}, {5, true}, {6, true}, {7, true}, {0x14, true}}));
    playback.stops.clear();
    runtime.map = 7;
    StepWorldAmbience(runtime, random);

    EXPECT_EQ(playback.sounds, std::vector<std::int32_t>{6});
    EXPECT_EQ(playback.loops, std::vector<bool>{true});
    EXPECT_EQ(playback.stops, (std::vector<std::pair<std::int32_t, bool>>{
                                  {0, true}, {1, true}, {3, true}, {2, true}, {5, true}, {7, true}, {0x14, true}}));
}

TEST(WorldRuntimeAudioTest, AmbienceDrawsEvenWithoutAnAudioSink)
{
    Kernel::WorldRuntime runtime;
    Crt::CrtRandom random(1);
    Crt::CrtRandom expected(1);
    runtime.map = 3;
    expected.Discard(1);
    StepWorldAmbience(runtime, random);
    EXPECT_EQ(random.State(), expected.State());

    runtime.map = 10;
    if (expected.Next() % 100 != 0)
        expected.Discard(1);
    StepWorldAmbience(runtime, random);
    EXPECT_EQ(random.State(), expected.State());
}

TEST(WorldRuntimeAudioTest, RoutesNestedWindowSoundsInOrderAndKeepsProjectionLocal)
{
    RecordingPlayback playback;
    Kernel::WorldRuntime runtime;
    runtime.audio = &playback;
    bool leftHeld = false;
    bool leftClick = false;
    bool releasedClick = false;
    std::uint8_t mouseOnUi = 0;
    std::int32_t hovered = 0;
    std::int32_t capture = 0;
    std::int32_t pressedButton = 0;
    UI::ChatFocus keyboardFocus = UI::ChatFocus::Game;
    std::int32_t focusedList = 0;
    std::int32_t wheel = 0;
    bool rightClick = false;
    bool doubleClick = false;
    UI::OldWindowGlobals globals{.leftHeld = leftHeld,
                                 .leftClick = leftClick,
                                 .releasedClick = releasedClick,
                                 .mouseOnUi = mouseOnUi,
                                 .hovered = hovered,
                                 .capture = capture,
                                 .pressedButton = pressedButton,
                                 .keyboardFocus = keyboardFocus,
                                 .sound = runtime.lastSoundId,
                                 .focusedList = focusedList,
                                 .wheel = wheel,
                                 .rightClick = rightClick,
                                 .doubleClick = doubleClick,
                                 .soundRequest = [&runtime](Client::Audio::SoundId sound)
                                 { runtime.RequestSound(sound); }};

    globals.RequestSound(Client::Audio::SoundId{0x19});
    globals.RequestSound(Client::Audio::SoundId{0x1c});

    EXPECT_EQ(playback.sounds, (std::vector<std::int32_t>{0x19, 0x1c}));
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1c});

    std::optional<Client::Audio::SoundId> projectedSound{};
    UI::OldWindowGlobals projected{.leftHeld = leftHeld,
                                   .leftClick = leftClick,
                                   .releasedClick = releasedClick,
                                   .mouseOnUi = mouseOnUi,
                                   .hovered = hovered,
                                   .capture = capture,
                                   .pressedButton = pressedButton,
                                   .keyboardFocus = keyboardFocus,
                                   .sound = projectedSound,
                                   .focusedList = focusedList,
                                   .wheel = wheel,
                                   .rightClick = rightClick,
                                   .doubleClick = doubleClick};
    projected.RequestSound(Client::Audio::SoundId{0x86});

    EXPECT_EQ(playback.sounds, (std::vector<std::int32_t>{0x19, 0x1c}));
    EXPECT_EQ(projectedSound, Client::Audio::SoundId{0x86});
}

} // namespace
} // namespace Client::Scenes
