#include "Scenes/WinMainLoop.h"

#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Models/Animation.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "UI/SlideBanner.h"
#include "Audio/SoundId.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

// Client::Scenes::StepTheWinMainLoop y CloseTheFrameCounter: los rand() de la vuelta de WinMain (P-C61, Q-C461),
// leídos del ASM de 0x4bbc57..0x4be02f y de 0x63950d..0x63b066.
namespace
{
using Client::Core::Duration;
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::Models::OriginalFramePace;
using Client::Models::OriginalFrameTime;
using Client::Scenes::CloseTheFrameCounter;
using Client::Scenes::FinishTheWinMainLoop;
using Client::Scenes::InTheDeviasChurch;
using Client::Scenes::StepTheWinMainLoop;
using Client::Scenes::WinMainCounters;
using Client::Scenes::WinMainLoopState;

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
        Client::Kernel::ActorState hero;
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
    Client::Kernel::ActorState hero;
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

        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{sound});
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

    EXPECT_EQ(tail.world->Runtime().lastSoundId, Client::Audio::SoundId{ForestSound});
}

TEST(WinMainLoopTest, TheCrywolfSoundNeedsItsMapAndItsStage)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t stage;
    };
    constexpr std::int32_t AidaMap = 33;
    for (const auto [map, stage] : {Case{CrywolfMap, 4}, Case{CrywolfMap, 6}, Case{AidaMap, CrywolfResultStage}})
    {
        LoopTail tail;
        auto& runtime = tail.world->Runtime();
        runtime.map = map;
        runtime.crywolf.stage = stage;

        tail.FinishAt(FirstTurn, OriginalFramePace::ByTheClock, true);

        EXPECT_FALSE(runtime.lastSoundId.has_value());
    }
}

// In the safe zone of Devias the map's music asks whether the hero is in the church: four reads of its cell, each
// closed with 4 rand() of the loop's CRT.
TEST(WinMainLoopTest, TheMapMusicReadsTheDeviasChurchWithTheLoopsRandom)
{
    struct Case
    {
        std::uint8_t safeZone;
        int draws;
    };
    constexpr std::int32_t DeviasMap = 2;
    for (const auto [safeZone, draws] : {Case{1, 16 + 9}, Case{0, 9}})
    {
        LoopTail tail;
        auto& runtime = tail.world->Runtime();
        runtime.map = DeviasMap;
        auto& hero = tail.world->Hero();
        hero.safeZone = safeZone;
        hero.cellX = 205;
        hero.cellY = 13;

        tail.FinishAt(FirstTurn, OriginalFramePace::ByTheClock, true);

        EXPECT_EQ(tail.random.State(), After(draws).State());
    }
}

// P-C80: a due timer runs once, after the frame counter of the frame it fell due in and before the next turn, with
// the loop's time in ms. In the game it reads the focus (1), the own level (OwnRecordCloseRandoms), picks a text (1)
// and reads the focus again (1).
TEST(WinMainLoopTest, TheSlideTimerRunsBetweenTheFrameCounterAndTheNextTurn)
{
    constexpr int Texts = 16;
    constexpr std::uint32_t Period = 60'000;
    constexpr int UntilThePick = 9 + 4 + 1 + Client::Models::OwnRecordCloseRandoms;
    LoopTail tail;
    auto& banner = tail.world->Runtime().hud.slides;
    banner.maxLevels[0] = 15;
    banner.counts[0] = Texts;
    for (int text = 0; text < Texts; ++text)
        banner.texts[0].push_back(std::to_string(text));
    banner.timerPeriod = Period;
    banner.timerDue = std::uint32_t{90};
    tail.FinishAt(FirstTurn, OriginalFramePace::ByTheClock, true);
    ASSERT_EQ(tail.random.State(), After(9).State());

    // 93 ms: the timer fell due in the first of the two frames.
    tail.FinishAt(2 * OriginalFrameTime, OriginalFramePace::ByTheClock, true);

    EXPECT_EQ(tail.random.State(), After(UntilThePick + 1 + 1 + 5 + 9).State());
    ASSERT_EQ(banner.normal.entries.size(), 1U);
    EXPECT_EQ(banner.normal.entries.begin()->second.text, std::to_string(After(UntilThePick).Next() % Texts));
    EXPECT_EQ(banner.timerDue, std::optional<std::uint32_t>(93 + Period));
}

TEST(WinMainLoopTest, OutsideTheGameADueSlideTimerOnlyReadsTheFocus)
{
    LoopTail tail;
    auto& banner = tail.world->Runtime().hud.slides;
    banner.maxLevels[0] = 15;
    banner.counts[0] = 1;
    banner.texts[0].emplace_back("text");
    banner.timerPeriod = 60'000;
    banner.timerDue = std::uint32_t{0};

    tail.FinishAt(FirstTurn);

    EXPECT_EQ(tail.random.State(), After(4 + 2 + 5).State());
    EXPECT_TRUE(banner.normal.entries.empty());
}
} // namespace
