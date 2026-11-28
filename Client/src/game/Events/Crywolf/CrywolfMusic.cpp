#include "Events/Crywolf/CrywolfMusic.h"

#include "Audio/MusicPlayer.h"
#include "Maps/MapId.h"
#include "Protocol/Events/Events.h"
#include "Kernel/WorldRuntime.h"
#include "Audio/SoundId.h"

#include <cstdint>
#include <string_view>

namespace Client::Events::Crywolf
{
namespace
{
namespace Events = Protocol::Events;

// 0x430250: the map it tests, the four tracks it plays and stops, the failure banner it looks for in 0x6c73dc and
// the two sounds of the result (CW_win.wav and CW_lose.wav).
using Maps::CrywolfMap;
constexpr std::string_view MapTrack = "Music\\crywolf1st.mp3";
constexpr std::string_view SecondNoticeTrack = "Music\\crywolf_before-01.ogg";
constexpr std::string_view ReadyTrack = "Music\\crywolf_ready-02.ogg";
constexpr std::string_view StartTrack = "Music\\crywolf_back-03.ogg";
constexpr std::int32_t FailureBanner = 0xa;
constexpr std::int32_t WinSound = 0x244;
constexpr std::int32_t LoseSound = 0x245;

// The progress is read after the play, as the original's frame does.
void PlayToItsEnd(Audio::MusicPlayer& music, std::string_view track)
{
    music.Play(track);
    if (music.ReachedTheEnd())
        music.Stop(track);
}

// A stage with a track of its own stops only the track of the stage before it.
void StepTheStageTrack(Audio::MusicPlayer& music, std::uint8_t stage)
{
    switch (stage)
    {
    case Events::CrywolfSecondNoticeStage:
        music.Stop(MapTrack);
        PlayToItsEnd(music, SecondNoticeTrack);
        break;
    case Events::CrywolfReadyStage:
        music.Stop(SecondNoticeTrack);
        PlayToItsEnd(music, ReadyTrack);
        break;
    case Events::CrywolfStartStage:
        music.Stop(ReadyTrack);
        PlayToItsEnd(music, StartTrack);
        break;
    case Events::CrywolfEndStage:
        music.Stop(ReadyTrack);
        music.Stop(StartTrack);
        break;
    default:
        PlayToItsEnd(music, MapTrack);
        break;
    }
}
} // namespace

void StepTheCrywolfMusic(Kernel::WorldRuntime& runtime)
{
    if (runtime.map == CrywolfMap)
    {
        StepTheStageTrack(runtime.music, runtime.crywolf.stage);
        if (runtime.crywolf.stage == Events::CrywolfEndStage)
            runtime.RequestSound(
                Audio::SoundId{runtime.crywolf.ui.dword6c73dc == FailureBanner ? LoseSound : WinSound});
    }
    else
    {
        runtime.music.Stop(MapTrack);
    }
}
} // namespace Client::Events::Crywolf
