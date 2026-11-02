#include "App/GameLoop.h"

#include "App/LoopTiming.h"
#include "App/PacingMode.h"
#include "App/WindowState.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "support/FakeClock.h"
#include "support/RecordingClient.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

// Client::App::GameLoop en el modo normal (Decoupled): el paso fijo con el reloj falso.
namespace
{
using namespace std::chrono_literals;
using Client::App::GameLoop;
using Client::App::PacingMode;
using Client::App::WindowState;
using Client::Core::Duration;
using Client::Input::InputEvent;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Tests::FakeClock;
using Client::Tests::RecordingClient;

// La tolerancia de los alfas que fija el plan (1e-6).
constexpr double AlphaTolerance = 1e-6;

// Lo que avanza el frame `frame` para que `framesPerSecond` frames sumen 1 s exacto.
Duration FrameTime(std::int64_t frame, std::int64_t framesPerSecond)
{
    const std::int64_t second = Duration{1s}.count();
    return Duration{(frame + 1) * second / framesPerSecond - frame * second / framesPerSecond};
}

class GameLoopTest : public ::testing::TestWithParam<int>
{
};

// En 4 s simulados, 100 pasos con cualquier tasa de frames. El primer frame solo fija el inicio.
// Además, el frame que más pasos corre: 1 con 25 FPS o más, 25 con 1 FPS (como largestStep en
// test_reference_frame_steps.cpp de MuMain). Sin eso, un bucle que corriera 2 pasos cada dos
// frames daría el mismo total.
TEST_P(GameLoopTest, TwentyFiveStepsPerSecondAtAnyFrameRate)
{
    const std::int64_t framesPerSecond = GetParam();
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);

    const std::int64_t frames = 4 * framesPerSecond;
    int largestStep = 0;
    for (std::int64_t frame = 0; frame < frames; ++frame)
    {
        clock.Advance(FrameTime(frame, framesPerSecond));
        largestStep = std::max(largestStep, loop.RunFrame(WindowState{}, input));
    }

    EXPECT_EQ(clock.Now(), 4s);
    EXPECT_EQ(client.steps, 100u);
    EXPECT_EQ(loop.Stats().steps, 100u);
    EXPECT_EQ(loop.Stats().frames, static_cast<std::uint64_t>(frames + 1));
    EXPECT_EQ(loop.Stats().clampedFrames, 0u);
    EXPECT_EQ(largestStep, framesPerSecond >= 25 ? 1 : 25 / framesPerSecond);
}

INSTANTIATE_TEST_SUITE_P(FrameRates, GameLoopTest, ::testing::Values(1, 25, 60, 144, 240));

TEST(GameLoopTest, ASlowFrameCatchesUpWithThreeSteps)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    client.renderCost = 130ms;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);
    client.renderCost = 0ms;

    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 3);
    EXPECT_NEAR(loop.LastAlpha(), 0.25, AlphaTolerance);
    ASSERT_EQ(client.alphas.size(), 2u);
    EXPECT_NEAR(client.alphas.back(), 0.25, AlphaTolerance);
}

// El breakpoint: un dibujo de 10 s corre 25 pasos (MaxFrameTime) y no 250 como el original.
TEST(GameLoopTest, AFrameLongerThanTheCapRunsOnlyTheCap)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    client.renderCost = 10s;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);
    client.renderCost = 0ms;

    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 25);
    EXPECT_EQ(loop.Stats().clampedFrames, 1u);
    for (int frame = 0; frame < 5; ++frame)
    {
        clock.Advance(40ms);
        EXPECT_EQ(loop.RunFrame(WindowState{}, input), 1) << "frame " << frame;
    }
    EXPECT_EQ(loop.Stats().clampedFrames, 1u);
}

TEST(GameLoopTest, AClockThatGoesBackRunsNoSteps)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    // Lejos de 0: Now() no baja de 0 (IClock).
    clock.Advance(100ms);
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);

    clock.Advance(-5ms);
    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 0);
    EXPECT_EQ(loop.Stats().accumulated, 0ns);
    EXPECT_NEAR(loop.LastAlpha(), 0.0, AlphaTolerance);

    // El siguiente se mide desde el valor nuevo.
    clock.Advance(40ms);
    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 1);
}

// Con VSync apagado, dos frames pueden leer el mismo instante.
TEST(GameLoopTest, AClockThatDoesNotMoveRunsNoSteps)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);
    clock.Advance(10ms);

    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 0);
    const float alpha = loop.LastAlpha();
    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 0);

    EXPECT_FALSE(std::isnan(loop.LastAlpha()));
    EXPECT_EQ(loop.LastAlpha(), alpha);
    EXPECT_NEAR(alpha, 0.25, AlphaTolerance);
    EXPECT_EQ(client.steps, 0u);
}

// Minimizada, la lógica sigue y el dibujo no: cada frame duerme lo que falta para el próximo
// paso, así que el bucle duerme en vez de girar.
TEST(GameLoopTest, AMinimizedWindowStepsWithoutDrawing)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    WindowState minimized;
    minimized.minimized = true;

    for (int frame = 0; frame < 26; ++frame)
    {
        loop.RunFrame(minimized, input);
    }

    EXPECT_EQ(client.steps, 25u);
    EXPECT_TRUE(client.alphas.empty());
    const auto sleeps = clock.Sleeps();
    EXPECT_EQ(std::accumulate(sleeps.begin(), sleeps.end(), Duration::zero()), 1040ms);
}

// Minimizada, duerme lo que falta del paso: 10 ms acumulados son 30 ms de sueño, no 40.
TEST(GameLoopTest, AMinimizedWindowSleepsWhatIsLeftOfTheStep)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    client.renderCost = 10ms;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);
    WindowState minimized;
    minimized.minimized = true;

    EXPECT_EQ(loop.RunFrame(minimized, input), 0);
    ASSERT_EQ(clock.Sleeps().size(), 1u);
    EXPECT_EQ(clock.Sleeps()[0], 30ms);
    EXPECT_EQ(loop.RunFrame(minimized, input), 1);
}

// El primer frame solo fija el inicio: con SdlClock, el primer Now() llega después de crear la
// ventana, y lo que tardó eso no son pasos.
TEST(GameLoopTest, TheFirstFrameOnlySetsTheStart)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    clock.Advance(5s);

    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 0);
    EXPECT_EQ(loop.Stats().clampedFrames, 0u);
    EXPECT_EQ(client.steps, 0u);
    clock.Advance(40ms);
    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 1);
}

TEST(GameLoopTest, AlphaGrowsBetweenSteps)
{
    constexpr std::int64_t FramesPerSecond = 240;
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);

    float previous = loop.LastAlpha();
    int drops = 0;
    for (std::int64_t frame = 0; frame < FramesPerSecond; ++frame)
    {
        clock.Advance(FrameTime(frame, FramesPerSecond));
        const int steps = loop.RunFrame(WindowState{}, input);
        const float alpha = loop.LastAlpha();
        EXPECT_GE(alpha, 0.0f) << "frame " << frame;
        EXPECT_LT(alpha, 1.0f) << "frame " << frame;
        if (steps == 0)
        {
            EXPECT_GT(alpha, previous) << "frame " << frame;
        }
        else
        {
            EXPECT_LT(alpha, previous) << "frame " << frame;
            ++drops;
        }
        previous = alpha;
    }
    EXPECT_EQ(drops, 25);
    EXPECT_EQ(client.alphas.size(), static_cast<std::size_t>(FramesPerSecond + 1));
}

// Un frame de 80 ms corre 2 pasos con un apretón: lo ve el primero, y el segundo no (Q-C45). La
// tecla sigue abajo: ConsumeEdges borra los flancos y no el estado.
TEST(GameLoopTest, APressIsSeenByExactlyOneStep)
{
    FakeClock clock;
    RecordingClient client{clock};
    client.watchedKey = Key::A;
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);

    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = Key::A});
    clock.Advance(80ms);

    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 2);
    EXPECT_EQ(client.sawPress, (std::vector<bool>{true, false}));
    EXPECT_TRUE(input.IsDown(Key::A));
    EXPECT_FALSE(input.WasPressed(Key::A));
}

// A 240 FPS, un apretón y su soltada en un frame sin pasos: el frame no toca el input, y el
// apretón lo ve el primer paso que venga, aunque la tecla ya esté arriba. El paso siguiente, no.
TEST(GameLoopTest, APressInAFrameWithoutStepsWaitsForTheNextStep)
{
    constexpr std::int64_t FramesPerSecond = 240;
    FakeClock clock;
    RecordingClient client{clock};
    client.watchedKey = Key::Space;
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);

    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = Key::Space});
    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyUp, .key = Key::Space});
    clock.Advance(FrameTime(0, FramesPerSecond));
    ASSERT_EQ(loop.RunFrame(WindowState{}, input), 0);
    EXPECT_TRUE(input.WasPressed(Key::Space));

    std::int64_t frame = 1;
    while (client.steps < 2 && frame < FramesPerSecond)
    {
        clock.Advance(FrameTime(frame, FramesPerSecond));
        loop.RunFrame(WindowState{}, input);
        ++frame;
    }

    EXPECT_EQ(client.sawPress, (std::vector<bool>{true, false}));
    EXPECT_FALSE(input.IsDown(Key::Space));
}

// El tope de FPS (MaxFps, el FpsCap de MuMain): con 100, un frame que dibuja en 3 ms duerme los
// 7 que le faltan para durar 10 ms desde su Now(). En 1 s simulado corren 100 frames y la lógica
// sigue a 25 pasos.
TEST(GameLoopTest, AnFpsCapSleepsWhatIsLeftOfTheFrame)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;
    client.renderCost = 3ms;

    // El primer frame fija el inicio; los 100 siguientes miden 10 ms cada uno.
    for (int frame = 0; frame <= 100; ++frame)
    {
        loop.RunFrame(WindowState{}, input);
    }

    EXPECT_EQ(client.steps, 25u);
    EXPECT_EQ(client.alphas.size(), 101u);
    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 101u);
    EXPECT_TRUE(std::ranges::all_of(sleeps, [](Duration sleep) { return sleep == 7ms; }));
    EXPECT_EQ(clock.Now(), 1010ms);
}

// Lo que tardan los pasos también es parte del frame: con 50 (20 ms) y pasos de 5 ms, el frame
// que corre un paso duerme 15 ms.
TEST(GameLoopTest, TheFpsCapCountsTheStepsToo)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 50};
    InputState input;
    client.stepCost = 5ms;

    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 0);
    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 0);
    EXPECT_EQ(loop.RunFrame(WindowState{}, input), 1);

    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 3u);
    EXPECT_EQ(sleeps[0], 20ms);
    EXPECT_EQ(sleeps[1], 20ms);
    EXPECT_EQ(sleeps[2], 15ms);
}

// Un frame que ya tardó más que el tope no duerme, y sin tope (0, el valor por defecto) tampoco.
TEST(GameLoopTest, AFrameLongerThanTheFpsCapDoesNotSleep)
{
    for (const int maxFps : {100, 0})
    {
        FakeClock clock;
        RecordingClient client{clock};
        GameLoop loop{clock, client, PacingMode::Decoupled, maxFps};
        InputState input;
        client.renderCost = maxFps == 0 ? 3ms : 15ms;

        for (int frame = 0; frame < 10; ++frame)
        {
            loop.RunFrame(WindowState{}, input);
        }

        EXPECT_TRUE(clock.Sleeps().empty()) << "maxFps " << maxFps;
    }
}

// Minimizada, el tope no se usa: duerme lo que falta para el próximo paso, como sin tope.
TEST(GameLoopTest, AMinimizedWindowIgnoresTheFpsCap)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;
    WindowState minimized;
    minimized.minimized = true;

    for (int frame = 0; frame < 26; ++frame)
    {
        loop.RunFrame(minimized, input);
    }

    EXPECT_EQ(client.steps, 25u);
    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 26u);
    EXPECT_TRUE(std::ranges::all_of(sleeps, [](Duration sleep) { return sleep == 40ms; }));
}

// Si el reloj vuelve durante el frame (solo el FakeClock), el tope no duerme: la resta daría más
// que un frame entero.
TEST(GameLoopTest, AClockThatGoesBackDuringTheFrameDoesNotSleep)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;
    client.renderCost = -5ms;

    loop.RunFrame(WindowState{}, input);

    EXPECT_TRUE(clock.Sleeps().empty());
}
} // namespace
