#include "Scenes/Slides.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/SlideBanner.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x46b38c..0x46b391: una vez, sin salto, el tipo 1 (la cola normal con 0) y el color.
constexpr std::int32_t TimedRepeats = 1;
constexpr std::uint16_t TimedGap = 0;
constexpr std::int32_t TimedKind = 1;
constexpr std::uint32_t TimedColor = 0xffc8dce6U;
// 0x46b05e..0x46b067: el período por 1000, en 32 bits.
constexpr std::uint32_t MillisecondsPerSecond = 1000;
// SetTimer: USER_TIMER_MINIMUM y USER_TIMER_MAXIMUM.
constexpr std::uint32_t ShortestTimer = 0xa;
constexpr std::uint32_t LongestTimer = 0x7fffffff;
// 0x4c0ec0: el cierre del byte del foco.
constexpr std::uint32_t FocusCloseRandoms = 1;

std::uint32_t TimerPeriod(std::int32_t seconds)
{
    const std::uint32_t elapse = static_cast<std::uint32_t>(seconds) * MillisecondsPerSecond;
    if (elapse < ShortestTimer)
        return ShortestTimer;
    return elapse > LongestTimer ? LongestTimer : elapse;
}
} // namespace

void QueueATimedSlide(WorldState& world, Render::Models::CrtRandom& random, bool gameScene)
{
    WorldRuntime& runtime = world.Runtime();
    UI::SlideBanner& banner = runtime.hud.slides;
    if (!gameScene || !runtime.chat.slidesOn || banner.normal.alpha > 0)
        return;
    // 0x46b115..0x46b372: abre la entrada del registro propio, lee +0xe y la cierra.
    std::uint16_t level = 0;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
        level = world.OwnCharacter().level;
    }
    const std::optional<std::string> text = UI::PickASlide(banner, level, random);
    if (!text)
        return;
    UI::QueueTheSlide(banner, TimedRepeats, TimedGap, *text, TimedKind, banner.speed, TimedColor);
}

void StartTheSlides(WorldState& world, Render::Models::CrtRandom& random, std::uint32_t now)
{
    UI::SlideBanner& banner = world.Runtime().hud.slides;
    UI::ResetTheSlideQueues(banner, now);
    banner.timerPeriod = TimerPeriod(banner.periodSeconds);
    banner.timerDue = now + banner.timerPeriod;
    QueueATimedSlide(world, random, true);
}

void RunTheSlideTimer(WorldState& world, Render::Models::CrtRandom& random, std::uint32_t now, bool gameScene)
{
    UI::SlideBanner& banner = world.Runtime().hud.slides;
    // La resta con signo: el reloj en ms de 32 bits da la vuelta, como timeGetTime.
    if (!banner.timerDue || static_cast<std::int32_t>(now - *banner.timerDue) < 0)
        return;
    banner.timerDue = now + banner.timerPeriod;
    random.Discard(FocusCloseRandoms);
    QueueATimedSlide(world, random, gameScene);
    random.Discard(FocusCloseRandoms);
}
} // namespace Client::Scenes
