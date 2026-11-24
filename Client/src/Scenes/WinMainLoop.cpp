#include "Scenes/WinMainLoop.h"

#include "Core/IClock.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/CrywolfMusic.h"
#include "Scenes/Slides.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace Client::Scenes
{
namespace
{
// 0x412890: el cierre de una variable protegida saca un rand() por byte; el contador tiene 4.
constexpr std::uint16_t CounterCloseRandoms = Render::Models::SmallLogicalCloseRandoms;
// 0x4c0ec0: el cierre del byte del foco.
constexpr std::uint16_t FocusCloseRandoms = 1;
// 0x4bbd44 y 0x4bbed5: los dos contadores comparan con 30.
constexpr std::int32_t LastQuietLoop = 0x1e;

void Draw(Render::Models::CrtRandom& random, std::uint16_t count)
{
    for (std::uint16_t i = 0; i < count; ++i)
        static_cast<void>(random.Next());
}

void StepTheGameMusic(WorldState& world, Render::Models::CrtRandom& random)
{
    WorldRuntime& runtime = world.Runtime();
    StepWorldAmbience(runtime, random);
    ActorState& hero = world.Actors()[runtime.hero];
    runtime.music.StepWorld(runtime.audio, runtime.map, hero.safeZone != 0, runtime.heroCellLayer,
                            [&hero, &random] { return InTheDeviasChurch(hero, random); });
    StepTheCrywolfMusic(runtime);
}
} // namespace

void StepTheWinMainLoop(WinMainCounters& counters, Render::Models::CrtRandom& random)
{
    // 0x4bbc57: abre el contador; 0x4bbd35: lo sube.
    ++counters.loops;
    if (counters.loops > LastQuietLoop)
    {
        // 0x4bbd51..0x4bbde5: cierra el acceso de la vuelta.
        Draw(random, CounterCloseRandoms);
        // 0x4bbdea..0x4bbecf: abre el segundo contador y lo sube; 0x4bda4d..0x4bdae3: menor que 31, lo cierra. Con
        // 31 o más (0x4bbedc), el chequeo de 0x4bc27d, que solo alcanza la ventana minimizada: el reinicio de abajo
        // lo deja en 0 en cada ronda.
        ++counters.rounds;
        Draw(random, CounterCloseRandoms);
        // 0x4bdaef: con IsIconic(hWnd) en 0, los dos en 0, cada uno con su apertura y su cierre (0x4bdbd9,
        // 0x4bdd58). 0x4bddfd: vuelve a abrir el primero, que cierra la cola común.
        counters.rounds = 0;
        Draw(random, CounterCloseRandoms);
        counters.loops = 0;
        Draw(random, CounterCloseRandoms);
    }
    // 0x4bded9: la cola común cierra el contador.
    Draw(random, CounterCloseRandoms);
    // 0x4bdf76..0x4bdfdd: abre el foco, lo lee y lo cierra (0x4c0ec0). Con foco, el frame.
    Draw(random, FocusCloseRandoms);
}

void CloseTheFrameCounter(Render::Models::CrtRandom& random)
{
    Draw(random, CounterCloseRandoms);
}

bool InTheDeviasChurch(ActorState& hero, Render::Models::CrtRandom& random)
{
    const auto read = [&random](const std::uint8_t& cell, std::uint8_t& depth, std::uint32_t& left)
    {
        const Render::Models::LogicalAccess access(depth, random, Render::Models::SmallLogicalCloseRandoms, left);
        return cell;
    };
    return read(hero.cellX, hero.cellXAccesses, hero.cellXLeft) >= 205 &&
           read(hero.cellX, hero.cellXAccesses, hero.cellXLeft) <= 214 &&
           read(hero.cellY, hero.cellYAccesses, hero.cellYLeft) >= 13 &&
           read(hero.cellY, hero.cellYAccesses, hero.cellYLeft) <= 31;
}

void StepWorldAmbience(WorldRuntime& runtime, Render::Models::CrtRandom& random)
{
    constexpr int Wind = 0, Rain = 1, Forest = 2, Dungeon = 3, Tower = 5, Atlans = 6, Tarkan = 7, Icarus = 0x14;
    const auto loop = [&runtime](std::int32_t id) { runtime.RequestSound(id, std::nullopt, true); };
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

void FinishTheWinMainLoop(WinMainLoopState& loop, WorldState& world, Render::Models::CrtRandom& random,
                          Core::Duration now, Render::Models::OriginalFramePace pace, bool gameScene)
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
        RunTheSlideTimer(world, random, systemTime, gameScene);
        StepTheWinMainLoop(loop.counters, random);
    }
}
} // namespace Client::Scenes
