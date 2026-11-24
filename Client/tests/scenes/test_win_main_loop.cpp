#include "Scenes/WinMainLoop.h"

#include "Core/IClock.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

// Client::Scenes::StepTheWinMainLoop y CloseTheFrameCounter: los rand() de la vuelta de WinMain (P-C61, Q-C461),
// leídos del ASM de 0x4bbc57..0x4be02f y de 0x63950d..0x63b066.
namespace
{
using Client::Core::Duration;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OriginalFramePace;
using Client::Render::Models::OriginalFrameTime;
using Client::Scenes::CloseTheFrameCounter;
using Client::Scenes::FinishTheWinMainLoop;
using Client::Scenes::InTheDeviasChurch;
using Client::Scenes::StepTheWinMainLoop;
using Client::Scenes::WinMainCounters;
using Client::Scenes::WinMainLoopState;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 0x1234;

// La CRT con `draws` rand() sacados desde Seed.
CrtRandom After(int draws)
{
    CrtRandom random(Seed);
    for (int i = 0; i < draws; ++i)
        static_cast<void>(random.Next());
    return random;
}

std::uint32_t PackedAfter(int draws)
{
    auto random = After(draws);
    std::uint32_t packed = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        packed |= (static_cast<std::uint32_t>(random.Next()) & 0xffu) << shift;
    return packed;
}

// Cada vuelta abre, sube y cierra el contador (4) y lee el byte del foco (1).
TEST(WinMainLoopTest, AQuietLoopDrawsFiveAndCountsIt)
{
    WinMainCounters counters;
    CrtRandom random(Seed);

    StepTheWinMainLoop(counters, random);

    EXPECT_EQ(random.State(), After(5).State());
    EXPECT_EQ(counters.loops, 1);
    EXPECT_EQ(counters.rounds, 0);
}

// 0x4bbd44: la vuelta 31 cierra el primero, abre, sube y cierra el segundo, y con la ventana sin minimizar
// (0x4bdaef) deja los dos en 0, cada uno con su cierre, antes de la cola común: 4 * 5 + 1.
TEST(WinMainLoopTest, TheThirtyFirstLoopResetsBothCounters)
{
    WinMainCounters counters;
    CrtRandom random(Seed);
    for (int loop = 0; loop < 30; ++loop)
        StepTheWinMainLoop(counters, random);
    ASSERT_EQ(random.State(), After(30 * 5).State());
    ASSERT_EQ(counters.loops, 30);

    StepTheWinMainLoop(counters, random);

    EXPECT_EQ(random.State(), After(30 * 5 + 21).State());
    EXPECT_EQ(counters.loops, 0);
    EXPECT_EQ(counters.rounds, 0);

    // Y vuelve a empezar: la vuelta siguiente es la 1.
    StepTheWinMainLoop(counters, random);
    EXPECT_EQ(random.State(), After(30 * 5 + 21 + 5).State());
    EXPECT_EQ(counters.loops, 1);
}

// 0x63945d, después de 0x637700: abre y cierra el contador.
TEST(WinMainLoopTest, TheFrameClosesTheCounterWithFour)
{
    CrtRandom random(Seed);

    CloseTheFrameCounter(random);

    EXPECT_EQ(random.State(), After(4).State());
}

TEST(WinMainLoopTest, DeviasClosesEachReachedCoordinateRead)
{
    struct Case
    {
        std::uint8_t x;
        std::uint8_t y;
        int draws;
        bool church;
    };
    for (const auto [x, y, draws, church] :
         {Case{204, 13, 4, false}, Case{215, 13, 8, false}, Case{205, 12, 12, false}, Case{205, 13, 16, true},
          Case{214, 31, 16, true}, Case{214, 32, 16, false}})
    {
        Client::Scenes::ActorState hero;
        hero.cellX = x;
        hero.cellY = y;
        CrtRandom random(Seed);
        EXPECT_EQ(InTheDeviasChurch(hero, random), church);
        EXPECT_EQ(random.State(), After(draws).State());
        EXPECT_EQ(hero.cellXAccesses, 0);
        EXPECT_EQ(hero.cellYAccesses, 0);
        EXPECT_EQ(hero.cellXLeft, PackedAfter(draws < 8 ? 0 : 4));
        if (draws > 8)
        {
            EXPECT_EQ(hero.cellYLeft, PackedAfter(draws - 4));
        }
    }
    Client::Scenes::ActorState hero;
    hero.cellX = 205;
    hero.cellY = 13;
    hero.cellXAccesses = 1;
    hero.cellXLeft = 0x12345678;
    CrtRandom random(Seed);
    EXPECT_TRUE(InTheDeviasChurch(hero, random));
    EXPECT_EQ(hero.cellXAccesses, 1);
    EXPECT_EQ(hero.cellYAccesses, 0);
    EXPECT_EQ(random.State(), After(8).State());
    EXPECT_EQ(hero.cellXLeft, 0x12345678u);
    EXPECT_EQ(hero.cellYLeft, PackedAfter(4));
}

// Una vuelta entera con su frame: 9, y 25 en la de las rondas.
TEST(WinMainLoopTest, ThirtyOneLoopsWithTheirFramesDrawTwoHundredNinetyFive)
{
    WinMainCounters counters;
    CrtRandom random(Seed);
    for (int loop = 0; loop < 31; ++loop)
    {
        StepTheWinMainLoop(counters, random);
        CloseTheFrameCounter(random);
    }

    EXPECT_EQ(random.State(), After(30 * 9 + 25).State());
}

// What FinishTheWinMainLoop works on, from WinMain's start.
struct LoopTail
{
    explicit LoopTail(std::uint32_t seed = Seed) : random(seed) {}

    void FinishAt(Duration now, OriginalFramePace pace = OriginalFramePace::ByTheClock, bool gameScene = false)
    {
        FinishTheWinMainLoop(loop, *world, random, now, pace, gameScene);
    }

    WinMainLoopState loop;
    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random;
};

constexpr Duration FirstTurn{};
constexpr std::int32_t CrywolfMap = 34;
constexpr std::uint8_t CrywolfResultStage = 5;
constexpr std::int32_t FailureBanner = 0xa;
constexpr std::int32_t SuccessBanner = 0xb;

// 4 to close the frame counter of 0x63945d, 5 for the quiet turn that follows.
TEST(WinMainLoopTest, ATurnOutsideTheGameClosesTheFrameAndStartsTheNextTurn)
{
    LoopTail tail;
    tail.world->Runtime().ui.enterPressed = true;

    tail.FinishAt(FirstTurn);

    EXPECT_EQ(tail.random.State(), After(9).State());
    EXPECT_EQ(tail.loop.counters.loops, 1);
    EXPECT_FALSE(tail.world->Runtime().ui.enterPressed);
}

TEST(WinMainLoopTest, ATurnShorterThanAnOriginalFrameRunsNoTail)
{
    LoopTail tail;
    tail.FinishAt(FirstTurn);
    tail.world->Runtime().ui.enterPressed = true;

    tail.FinishAt(std::chrono::milliseconds(10));

    EXPECT_EQ(tail.random.State(), After(9).State());
    EXPECT_EQ(tail.loop.counters.loops, 1);
    EXPECT_FALSE(tail.world->Runtime().ui.enterPressed);
}

TEST(WinMainLoopTest, ATurnOfTwoOriginalFramesRunsTheTailTwice)
{
    LoopTail tail;
    tail.FinishAt(FirstTurn);

    tail.FinishAt(2 * OriginalFrameTime);

    EXPECT_EQ(tail.random.State(), After(27).State());
    EXPECT_EQ(tail.loop.counters.loops, 3);
}

// By the clock, 200 ms are four original frames.
TEST(WinMainLoopTest, TheFaithfulPaceRunsOneTailPerTurn)
{
    LoopTail tail;
    tail.FinishAt(FirstTurn, OriginalFramePace::OncePerDraw);

    tail.FinishAt(std::chrono::milliseconds(200), OriginalFramePace::OncePerDraw);

    EXPECT_EQ(tail.random.State(), After(18).State());
    EXPECT_EQ(tail.loop.counters.loops, 2);
}

// 0x430250 at stage 5: 0x244 (CW_win.wav) or, with the failure banner in 0x6c73dc, 0x245 (CW_lose.wav).
TEST(WinMainLoopTest, CrywolfAtStageFiveRequestsItsSound)
{
    struct Case
    {
        std::int32_t banner;
        std::int32_t sound;
    };
    for (const auto [banner, sound] : {Case{SuccessBanner, 0x244}, Case{FailureBanner, 0x245}})
    {
        LoopTail tail;
        auto& runtime = tail.world->Runtime();
        runtime.map = CrywolfMap;
        runtime.crywolf.stage = CrywolfResultStage;
        runtime.crywolf.ui.dword6c73dc = banner;

        tail.FinishAt(FirstTurn, OriginalFramePace::ByTheClock, true);

        EXPECT_EQ(runtime.lastSoundId, std::optional<std::int32_t>(sound));
    }
}

TEST(WinMainLoopTest, OutsideTheGameSceneTheMusicDoesNotStep)
{
    LoopTail tail;
    auto& runtime = tail.world->Runtime();
    runtime.map = CrywolfMap;
    runtime.crywolf.stage = CrywolfResultStage;
    runtime.crywolf.ui.dword6c73dc = SuccessBanner;

    tail.FinishAt(FirstTurn, OriginalFramePace::ByTheClock, false);

    EXPECT_FALSE(runtime.lastSoundId.has_value());
}

// Noria's ambience requests the forest sound when its rand() is a multiple of 512. With this seed the first rand()
// is 512 and the fifth, the one the ambience would get after the frame counter's four, is not a multiple.
TEST(WinMainLoopTest, TheGameMusicStepsBeforeTheFrameCounterCloses)
{
    constexpr std::uint32_t SeedOfAForestSound = 0x91;
    constexpr std::int32_t NoriaMap = 3;
    constexpr std::int32_t ForestSound = 2;
    LoopTail tail(SeedOfAForestSound);
    tail.world->Runtime().map = NoriaMap;

    tail.FinishAt(FirstTurn, OriginalFramePace::ByTheClock, true);

    EXPECT_EQ(tail.world->Runtime().lastSoundId, std::optional<std::int32_t>(ForestSound));
}
} // namespace
