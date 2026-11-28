#include "Scenes/WinMainLoop.h"

#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Models/Animation.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Events/Crywolf/CrywolfMusic.h"
#include "Hud/Slides.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Audio/SoundId.h"
#include "Ui/GameUiState.h"
#include "Weather/LeafWeather.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace Client::Scenes
{
namespace
{
// 0x412890: closing a protected variable takes one rand() per byte; the counter has 4.
constexpr std::uint16_t CounterCloseRandoms = Models::SmallLogicalCloseRandoms;
// 0x4c0ec0: the focus byte's close.
constexpr std::uint16_t FocusCloseRandoms = 1;
// 0x4bbd44 and 0x4bbed5: both counters compare with 30.
constexpr std::int32_t LastQuietLoop = 0x1e;

void Draw(Crt::CrtRandom& random, std::uint16_t count)
{
    for (std::uint16_t i = 0; i < count; ++i)
        static_cast<void>(random.Next());
}

void StepTheGameMusic(Kernel::WorldState& world, Crt::CrtRandom& random)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    StepWorldAmbience(runtime, random);
    Kernel::ActorState& hero = world.Hero();
    runtime.music.StepWorld(runtime.audio, runtime.map, hero.safeZone != 0, runtime.heroCellLayer,
                            [&hero, &random] { return InTheDeviasChurch(hero, random); });
    Events::Crywolf::StepTheCrywolfMusic(runtime);
}
} // namespace

void StepTheWinMainLoop(WinMainCounters& counters, Crt::CrtRandom& random)
{
    // 0x4bbc57: opens the counter; 0x4bbd35: raises it.
    ++counters.loops;
    if (counters.loops > LastQuietLoop)
    {
        // 0x4bbd51..0x4bbde5: closes the turn's access.
        Draw(random, CounterCloseRandoms);
        // 0x4bbdea..0x4bbecf: opens the second counter and raises it; 0x4bda4d..0x4bdae3: below 31, closes it. With 31
        // or more (0x4bbedc), 0x4bc27d's check, which only the minimized window reaches: the reset below leaves it at 0
        // on each round.
        ++counters.rounds;
        Draw(random, CounterCloseRandoms);
        // 0x4bdaef: with IsIconic(hWnd) at 0, both at 0, each with its open and close (0x4bdbd9, 0x4bdd58). 0x4bddfd:
        // opens the first one again, which the common tail closes.
        counters.rounds = 0;
        Draw(random, CounterCloseRandoms);
        counters.loops = 0;
        Draw(random, CounterCloseRandoms);
    }
    // 0x4bded9: the common tail closes the counter.
    Draw(random, CounterCloseRandoms);
    // 0x4bdf76..0x4bdfdd: opens the focus, reads it and closes it (0x4c0ec0). With focus, the frame.
    Draw(random, FocusCloseRandoms);
}

void CloseTheFrameCounter(Crt::CrtRandom& random)
{
    Draw(random, CounterCloseRandoms);
}

bool InTheDeviasChurch(Kernel::ActorState& hero, Crt::CrtRandom& random)
{
    const auto read = [&random](const std::uint8_t& cell, std::uint8_t& depth, std::uint32_t& left)
    {
        const Models::LogicalAccess access(depth, random, Models::SmallLogicalCloseRandoms, left);
        return cell;
    };
    return read(hero.cellX, hero.cellXAccesses, hero.cellXLeft) >= 205 &&
           read(hero.cellX, hero.cellXAccesses, hero.cellXLeft) <= 214 &&
           read(hero.cellY, hero.cellYAccesses, hero.cellYLeft) >= 13 &&
           read(hero.cellY, hero.cellYAccesses, hero.cellYLeft) <= 31;
}

void StepWorldAmbience(Kernel::WorldRuntime& runtime, Crt::CrtRandom& random)
{
    constexpr Audio::SoundId Wind{0}, Rain{1}, Forest{2}, Dungeon{3}, Tower{5}, Atlans{6}, Tarkan{7}, Icarus{0x14};
    const auto loop = [&runtime](Audio::SoundId id) { runtime.RequestSound(id, std::nullopt, true); };
    const std::int32_t map = runtime.map;
    switch (map)
    {
    case 0:
        if (runtime.heroCellLayer == 4)
        {
            runtime.StopSound(Wind, true);
            runtime.StopSound(Rain, true);
        }
        else
        {
            loop(Wind);
            if (runtime.leafWeather.current > 0)
                loop(Rain);
        }
        break;
    case 1:
        loop(Dungeon);
        break;
    case 2:
        if (runtime.heroCellLayer == 3 || runtime.heroCellLayer >= 10)
            runtime.StopSound(Wind, true);
        else
            loop(Wind);
        break;
    case 3:
        loop(Wind);
        if (random.Next() % 512 == 0)
            runtime.RequestSound(Forest);
        break;
    case 4:
        loop(Tower);
        break;
    case 7:
        loop(Atlans);
        break;
    case 8:
        loop(Tarkan);
        break;
    case 10:
        loop(Icarus);
        if (random.Next() % 100 != 0)
            random.Discard(1);
        break;
    default:
        break;
    }
    if (map != 0 && map != 2 && map != 3)
        runtime.StopSound(Wind, true);
    if (map != 0 && map != 9)
        runtime.StopSound(Rain, true);
    if (map != 1)
        runtime.StopSound(Dungeon, true);
    if (map != 3)
        runtime.StopSound(Forest, true);
    if (map != 4)
        runtime.StopSound(Tower, true);
    if (map != 7)
        runtime.StopSound(Atlans, true);
    if (map != 8)
        runtime.StopSound(Tarkan, true);
    if (map != 10)
        runtime.StopSound(Icarus, true);
}

void FinishTheWinMainLoop(WinMainLoopState& loop, Kernel::WorldState& world, Crt::CrtRandom& random, Core::Duration now,
                          Models::OriginalFramePace pace, bool gameScene)
{
    world.Runtime().ui.enterPressed = false;
    const int frames = loop.frames.FramesAt(now, pace);
    const auto systemTime =
        static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    for (int frame = 0; frame < frames; ++frame)
    {
        if (gameScene)
            StepTheGameMusic(world, random);
        CloseTheFrameCounter(random);
        Hud::RunTheSlideTimer(world, random, systemTime, gameScene);
        StepTheWinMainLoop(loop.counters, random);
    }
}
} // namespace Client::Scenes
