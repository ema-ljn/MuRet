#include "Scenes/WinMainLoop.h"

#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"

#include <gtest/gtest.h>

#include <cstdint>

// Client::Scenes::StepTheWinMainLoop y CloseTheFrameCounter: los rand() de la vuelta de WinMain (P-C61, Q-C461),
// leídos del ASM de 0x4bbc57..0x4be02f y de 0x63950d..0x63b066.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::CloseTheFrameCounter;
using Client::Scenes::InTheDeviasChurch;
using Client::Scenes::StepTheWinMainLoop;
using Client::Scenes::WinMainCounters;

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
} // namespace
