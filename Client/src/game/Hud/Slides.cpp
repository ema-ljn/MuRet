#include "Hud/Slides.h"

#include "Crt/CrtRandom.h"
#include "Models/LogicalAccess.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/SlideBanner.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Client::Hud
{
namespace
{
// 0x46b38c..0x46b391: once, no gap, type 1 (the normal queue with 0) and the color.
constexpr std::int32_t TimedRepeats = 1;
constexpr std::uint16_t TimedGap = 0;
constexpr std::int32_t TimedKind = 1;
constexpr std::uint32_t TimedColor = 0xffc8dce6U;
// 0x46b05e..0x46b067: the period times 1000, in 32 bits.
constexpr std::uint32_t MillisecondsPerSecond = 1000;
// SetTimer: USER_TIMER_MINIMUM and USER_TIMER_MAXIMUM.
constexpr std::uint32_t ShortestTimer = 0xa;
constexpr std::uint32_t LongestTimer = 0x7fffffff;
// 0x4c0ec0: the focus byte's close.
constexpr std::uint32_t FocusCloseRandoms = 1;

std::uint32_t TimerPeriod(std::int32_t seconds)
{
    const std::uint32_t elapse = static_cast<std::uint32_t>(seconds) * MillisecondsPerSecond;
    if (elapse < ShortestTimer)
        return ShortestTimer;
    return elapse > LongestTimer ? LongestTimer : elapse;
}
} // namespace

void QueueATimedSlide(Kernel::WorldState& world, Crt::CrtRandom& random, bool gameScene)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    Ui::SlideBanner& banner = runtime.hud.slides;
    if (!gameScene || !runtime.chat.slidesOn || banner.normal.alpha > 0)
        return;
    // 0x46b115..0x46b372: opens the own record's entry, reads +0xe and closes it.
    std::uint16_t level = 0;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
        level = world.OwnCharacter().level;
    }
    const std::optional<std::string> text = Ui::PickASlide(banner, level, random);
    if (!text)
        return;
    Ui::QueueTheSlide(banner, TimedRepeats, TimedGap, *text, TimedKind, banner.speed, TimedColor);
}

void StartTheSlides(Kernel::WorldState& world, Crt::CrtRandom& random, std::uint32_t now)
{
    Ui::SlideBanner& banner = world.Runtime().hud.slides;
    Ui::ResetTheSlideQueues(banner, now);
    banner.timerPeriod = TimerPeriod(banner.periodSeconds);
    banner.timerDue = now + banner.timerPeriod;
    QueueATimedSlide(world, random, true);
}

void RunTheSlideTimer(Kernel::WorldState& world, Crt::CrtRandom& random, std::uint32_t now, bool gameScene)
{
    Ui::SlideBanner& banner = world.Runtime().hud.slides;
    // The signed difference: the 32-bit ms clock wraps around, like timeGetTime.
    if (!banner.timerDue || static_cast<std::int32_t>(now - *banner.timerDue) < 0)
        return;
    banner.timerDue = now + banner.timerPeriod;
    random.Discard(FocusCloseRandoms);
    QueueATimedSlide(world, random, gameScene);
    random.Discard(FocusCloseRandoms);
}
} // namespace Client::Hud
