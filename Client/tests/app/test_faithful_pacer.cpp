#include "App/FaithfulPacer.h"

#include "App/GameLoop.h"
#include "App/LoopTiming.h"
#include "App/PacingMode.h"
#include "App/WindowState.h"
#include "Input/InputState.h"
#include "support/FakeClock.h"
#include "support/RecordingClient.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

// Client::App::FaithfulPacer y el modo Faithful de GameLoop: el ritmo de 0x637700 con el reloj de
// 64 ticks por segundo. Los números son los del modelo del lab (labs/cl3.0-bucle/jefe).
namespace
{
using namespace std::chrono_literals;
using Client::App::FaithfulTick;
using Client::App::FaithfulWaitMs;
using Client::App::GameLoop;
using Client::App::PacingMode;
using Client::App::TickCountMs;
using Client::App::WindowState;
using Client::Tests::FakeClock;
using Client::Tests::RecordingClient;

// En la grilla de 15,625 ms nunca se mide 40: con una espera de 32 a 46 ms pasarían los mismos
// tests, y esto la fija.
static_assert(FaithfulWaitMs == 40);

// Un FakeClock en t = 0, un cliente con dibujos de 0 ms y el bucle en el modo fiel.
struct FaithfulLoop
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Faithful};
    // Vacío: estos tests no miran el input.
    Client::Input::InputState input;

    std::vector<int> StepsPerFrame(int frames, const WindowState& state = WindowState{})
    {
        std::vector<int> steps;
        for (int frame = 0; frame < frames; ++frame)
        {
            steps.push_back(loop.RunFrame(state, input));
        }
        return steps;
    }
};

int Sum(const std::vector<int>& steps, std::size_t first, std::size_t count)
{
    const auto begin = steps.begin() + static_cast<std::ptrdiff_t>(first);
    return std::accumulate(begin, begin + static_cast<std::ptrdiff_t>(count), 0);
}

TEST(FaithfulPacerTest, TickCountMsFollowsTheGrid)
{
    const std::vector<std::uint32_t> expected{0, 15, 31, 46, 62};
    for (std::size_t tick = 0; tick < expected.size(); ++tick)
    {
        EXPECT_EQ(TickCountMs(static_cast<std::int64_t>(tick) * FaithfulTick), expected[tick]) << "tick " << tick;
    }
    // Entre dos ticks lee el anterior.
    EXPECT_EQ(TickCountMs(FaithfulTick - 1ns), 0u);
}

// Cada frame espera 3 ticks: 64 frames son 192 ticks, 3 s justos (21,33 FPS).
TEST(FaithfulPacerTest, SixtyFourFramesEveryThreeSeconds)
{
    FaithfulLoop faithful;
    faithful.StepsPerFrame(64);

    EXPECT_EQ(faithful.clock.Now(), 3s);
    EXPECT_EQ(faithful.clock.Now(), 192 * FaithfulTick);
    // 3 sueños por frame, cada uno de un tick: de borde a borde.
    const auto sleeps = faithful.clock.Sleeps();
    EXPECT_EQ(sleeps.size(), 192u);
    EXPECT_TRUE(std::all_of(sleeps.begin(), sleeps.end(), [](auto sleep) { return sleep == FaithfulTick; }));
    // Los 75 pasos de 3 s a 25 por segundo, más el del valor inicial: 74 corridos y 80 ms.
    EXPECT_EQ(faithful.loop.Stats().steps, 74u);
    EXPECT_EQ(faithful.loop.Stats().accumulated, 80ms);
    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 2);
}

TEST(FaithfulPacerTest, SeventyFiveStepsEverySixtyFourFrames)
{
    FaithfulLoop faithful;
    const std::vector<int> steps = faithful.StepsPerFrame(300);

    const std::vector<int> firstThirteen{1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2};
    EXPECT_EQ(std::vector<int>(steps.begin(), steps.begin() + 13), firstThirteen);

    // Desde el frame 2 (el índice 1): el primero corre el paso del valor inicial.
    for (std::size_t first = 1; first + 64 <= steps.size(); ++first)
    {
        const auto begin = steps.begin() + static_cast<std::ptrdiff_t>(first);
        EXPECT_EQ(Sum(steps, first, 64), 75) << "desde el frame " << first + 1;
        EXPECT_EQ(std::count(begin, begin + 64, 2), 11) << "desde el frame " << first + 1;
    }
    for (std::size_t first = 1; first + 6 <= steps.size(); ++first)
    {
        const int six = Sum(steps, first, 6);
        EXPECT_TRUE(six == 7 || six == 8) << six << " desde el frame " << first + 1;
    }
    // Los frames 31 a 36.
    const std::vector<int> thirtyOneToThirtySix{2, 1, 1, 1, 1, 2};
    EXPECT_EQ(std::vector<int>(steps.begin() + 30, steps.begin() + 36), thirtyOneToThirtySix);
}

TEST(FaithfulPacerTest, EachFrameMeasuresThreeTicks)
{
    FaithfulLoop faithful;
    for (int frame = 0; frame < 300; ++frame)
    {
        faithful.loop.RunFrame(WindowState{}, faithful.input);
        const std::int32_t measured = faithful.loop.Stats().lastMeasuredMs;
        EXPECT_TRUE(measured == 46 || measured == 47) << measured << " en el frame " << frame + 1;
    }
}

// Un dibujo que ya pasó los 40 ms no espera: suma lo que midió GetTickCount, de a ticks.
TEST(FaithfulPacerTest, ADrawLongerThanTheWaitAddsItWhole)
{
    FaithfulLoop faithful;
    faithful.client.renderCost = 130ms;

    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 1);
    EXPECT_EQ(faithful.loop.Stats().lastMeasuredMs, 125); // 8 ticks
    EXPECT_TRUE(faithful.clock.Sleeps().empty());

    faithful.client.renderCost = 0ms;
    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 3);
}

// La medición empieza después de los pasos (0x63852b): lo que tardan se pierde. Midiendo desde
// antes de los pasos serían 64 frames y 74 pasos, no 61 y 71.
TEST(FaithfulPacerTest, TimeSpentInStepsIsNotAccumulated)
{
    FaithfulLoop faithful;
    faithful.client.stepCost = 10ms;

    std::uint64_t frames = 0;
    while (faithful.clock.Now() < 3s)
    {
        faithful.loop.RunFrame(WindowState{}, faithful.input);
        ++frames;
    }

    EXPECT_EQ(frames, 61u);
    EXPECT_EQ(faithful.client.steps, 71u);
    EXPECT_EQ(faithful.clock.Now(), 3'015'625us);
}

// El original no tiene tope: un breakpoint de 10 s son 250 pasos seguidos.
TEST(FaithfulPacerTest, HasNoCap)
{
    FaithfulLoop faithful;
    faithful.client.renderCost = 10s;

    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 1);
    EXPECT_EQ(faithful.loop.Stats().lastMeasuredMs, 10'000);

    faithful.client.renderCost = 0ms;
    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 250);
}

// Minimizada, la espera de GetTickCount sigue: sin ella el acumulador no suma.
TEST(FaithfulPacerTest, MinimizedKeepsTheWait)
{
    FaithfulLoop faithful;
    WindowState minimized;
    minimized.minimized = true;
    faithful.StepsPerFrame(64, minimized);

    EXPECT_EQ(faithful.clock.Now(), 3s);
    EXPECT_EQ(faithful.loop.Stats().steps, 74u);
    EXPECT_EQ(faithful.loop.RunFrame(minimized, faithful.input), 2);
    EXPECT_TRUE(faithful.client.alphas.empty());
}

// El dibujo del modo fiel no se interpola: el estado después de los pasos.
TEST(FaithfulPacerTest, DrawsWithAlphaZero)
{
    FaithfulLoop faithful;
    faithful.StepsPerFrame(10);

    ASSERT_EQ(faithful.client.alphas.size(), 10u);
    for (const float alpha : faithful.client.alphas)
    {
        EXPECT_EQ(alpha, 0.0f);
    }
}

// La vuelta del DWORD (49,7 días) no cambia nada: la resta da la vuelta igual que la hora.
// Arranca 67 ticks antes del primero que lee 0 después de la vuelta, el 274.877.907, y en un
// múltiplo de 8 ticks (125 ms justos), así que cada frame mide lo mismo que desde 0 (como el lab,
// labs/cl3.0-bucle/jefe).
TEST(FaithfulPacerTest, TheGetTickCountWrapChangesNothing)
{
    const auto start = 274'877'840 * FaithfulTick;
    FaithfulLoop nearTheWrap;
    nearTheWrap.clock.Advance(start);
    FaithfulLoop fromZero;

    EXPECT_EQ(nearTheWrap.StepsPerFrame(64), fromZero.StepsPerFrame(64));
    EXPECT_EQ(nearTheWrap.clock.Now() - start, 3s);
    EXPECT_EQ(nearTheWrap.loop.Stats().accumulated, 80ms);
}

// La comparación de la espera es con signo (JGE en 0x638616): si el reloj vuelve durante el
// dibujo, la resta da negativa y la espera sigue hasta 40 ms después del inicio. Sin signo, la
// resta daría la vuelta, la espera saldría enseguida y sumaría -31.
TEST(FaithfulPacerTest, AClockThatGoesBackKeepsWaiting)
{
    FaithfulLoop faithful;
    faithful.clock.Advance(10 * FaithfulTick); // el inicio lee 156
    faithful.client.renderCost = -20ms;        // termina en el tick 8, que lee 125

    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 1);
    EXPECT_EQ(faithful.loop.Stats().lastMeasuredMs, 47); // el tick 13 lee 203
    EXPECT_EQ(faithful.clock.Now(), 13 * FaithfulTick);
}

// El acumulador suma como el ADD de x86: un dibujo de INT32_MAX ms lo hace dar la vuelta, sin UB,
// y queda negativo: como el original, no corre pasos hasta que vuelva a 40.
TEST(FaithfulPacerTest, TheAccumulatorWrapsLikeTheAdd)
{
    FaithfulLoop faithful;
    ASSERT_EQ(faithful.StepsPerFrame(2), (std::vector<int>{1, 1}));
    faithful.client.renderCost = std::chrono::milliseconds{std::numeric_limits<std::int32_t>::max()};

    // El frame 3 corre su paso y deja 13; el dibujo va del tick 6 (93 ms) a 2.147.483.734 ms.
    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 1);
    EXPECT_EQ(faithful.loop.Stats().lastMeasuredMs, 2'147'483'641);
    // 13 + 2.147.483.641 = 2.147.483.654, que en int32 es 2.147.483.654 - 2^32.
    EXPECT_EQ(faithful.loop.Stats().accumulated, std::chrono::milliseconds{-2'147'483'642});

    faithful.client.renderCost = 0ms;
    EXPECT_EQ(faithful.loop.RunFrame(WindowState{}, faithful.input), 0);
}
} // namespace
