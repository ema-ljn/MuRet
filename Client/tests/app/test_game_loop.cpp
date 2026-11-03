#include "App/GameLoop.h"

#include "App/PacingMode.h"
#include "App/WindowState.h"
#include "Core/IClock.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "support/FakeClock.h"
#include "support/RecordingClient.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

// Client::App::GameLoop en el modo normal (Decoupled): el paso fijo con el reloj falso. Y el Poll
// de la red en los dos modos (CL4.1).
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
using Client::Tests::LoopCall;
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

// Con el frame retenido (el connect del login, que en el original bloquea antes del dibujo,
// Q-C85), como minimizada: la red y los pasos siguen, no dibuja y duerme hasta el próximo paso.
// Cuando lo suelta, dibuja otra vez.
TEST(GameLoopTest, AHeldFrameStepsWithoutDrawing)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    client.holdsTheLastFrame = true;

    for (int frame = 0; frame < 26; ++frame)
    {
        loop.RunFrame(WindowState{}, input);
    }

    EXPECT_EQ(client.polls, 26u);
    EXPECT_EQ(client.steps, 25u);
    EXPECT_TRUE(client.alphas.empty());
    const auto sleeps = clock.Sleeps();
    EXPECT_EQ(std::accumulate(sleeps.begin(), sleeps.end(), Duration::zero()), 1040ms);

    client.holdsTheLastFrame = false;
    loop.RunFrame(WindowState{}, input);
    EXPECT_EQ(client.alphas.size(), 1u);
}

// El frame retenido se mira después de los pasos: el paso que empieza el connect (el que entra al
// login) ya no deja dibujar su frame. Mirado antes, ese frame sería el login sin el aviso.
TEST(GameLoopTest, AHoldFromAStepSkipsThatFramesDrawing)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled};
    InputState input;
    client.holdsAfterAStep = true;

    loop.RunFrame(WindowState{}, input);
    clock.Advance(40ms);
    loop.RunFrame(WindowState{}, input);

    EXPECT_EQ(client.calls, (std::vector<LoopCall>{LoopCall::Poll, LoopCall::Render, LoopCall::Poll, LoopCall::Step}));
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
// 7 que le faltan para llegar a su meta, 10 ms después de la anterior. En 1 s simulado corren 100
// frames y la lógica sigue a 25 pasos.
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

// Minimizada, el tope no se usa: duerme lo que falta para el próximo paso, como sin tope. Y al
// volver, la meta arranca de nuevo desde el Now() del frame. Con 40 (25 ms): el frame visible
// duerme 25; el minimizado, 15, hasta el paso de los 40 ms y no hasta la meta de los 50; y el
// visible que sigue, 25, hasta los 65 y no hasta los 50 de la meta vieja.
TEST(GameLoopTest, AMinimizedWindowIgnoresTheFpsCap)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 40};
    InputState input;
    WindowState minimized;
    minimized.minimized = true;

    loop.RunFrame(WindowState{}, input);
    loop.RunFrame(minimized, input);
    loop.RunFrame(WindowState{}, input);

    EXPECT_EQ(client.steps, 1u);
    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 3u);
    EXPECT_EQ(sleeps[0], 25ms);
    EXPECT_EQ(sleeps[1], 15ms);
    EXPECT_EQ(sleeps[2], 25ms);
}

// Si el reloj vuelve durante el frame (solo el FakeClock, sin bajar de 0), el tope no duerme: la
// resta daría más que un frame entero.
TEST(GameLoopTest, AClockThatGoesBackDuringTheFrameDoesNotSleep)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;
    clock.Advance(100ms);
    client.renderCost = -5ms;

    loop.RunFrame(WindowState{}, input);

    EXPECT_TRUE(clock.Sleeps().empty());
}

// Lo que corre entre dos RunFrame (los eventos, --stats) no se suma: la meta de cada frame es la
// del anterior más 10 ms. Con 1 ms afuera y 2 de dibujo, el primer frame duerme 8 y los otros 7,
// y 100 frames siguen siendo 1 s, no 1,1.
TEST(GameLoopTest, TheFpsCapDoesNotAddTheTimeBetweenFrames)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;
    client.renderCost = 2ms;

    for (int frame = 0; frame < 100; ++frame)
    {
        loop.RunFrame(WindowState{}, input);
        clock.Advance(1ms);
    }

    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 100u);
    EXPECT_EQ(sleeps[0], 8ms);
    EXPECT_TRUE(std::all_of(sleeps.begin() + 1, sleeps.end(), [](Duration sleep) { return sleep == 7ms; }));
    EXPECT_EQ(clock.Now(), 1001ms);
}

// Si un frame se pasa de la meta, la próxima cuenta desde donde terminó, y no desde el Now() del
// frame que sigue: lo que corre entre los dos tampoco se suma. Con 100 (10 ms), 6 ms de dibujo y 5
// afuera, cada frame dura 11: solo el primero duerme (4 ms), y 6 frames son 70 ms, los 66 de sin
// tope más esos 4. Contando desde el Now(), uno de cada dos dormiría 4 y serían 78: el tope daría
// menos FPS que no tener tope.
TEST(GameLoopTest, AfterAMissedDeadlineTheTimeBetweenFramesStillCounts)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;
    client.renderCost = 6ms;

    for (int frame = 0; frame < 6; ++frame)
    {
        loop.RunFrame(WindowState{}, input);
        clock.Advance(5ms);
    }

    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 1u);
    EXPECT_EQ(sleeps[0], 4ms);
    EXPECT_EQ(clock.Now(), 70ms);
}

// Un hueco entre frames más largo que el tope también cuenta: el frame que sigue ya llega tarde a su
// meta y no duerme, y el otro vuelve al tope. Con 100, 2 ms de dibujo y 15 ms afuera después del
// primero.
TEST(GameLoopTest, ATimeBetweenFramesLongerThanTheFpsCapCountsToo)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;
    client.renderCost = 2ms;

    // Dibuja hasta los 2 ms y duerme hasta la meta, a los 10.
    loop.RunFrame(WindowState{}, input);
    clock.Advance(15ms);
    // La meta era a los 20, y el frame empieza a los 25: dibuja hasta los 27 sin dormir.
    loop.RunFrame(WindowState{}, input);
    // La meta, 10 ms después de los 27: dibuja hasta los 29 y duerme 8.
    loop.RunFrame(WindowState{}, input);

    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 2u);
    EXPECT_EQ(sleeps[0], 8ms);
    EXPECT_EQ(sleeps[1], 8ms);
    EXPECT_EQ(clock.Now(), 37ms);
}

// Si el reloj vuelve entre dos frames (solo el FakeClock), la meta anterior queda adelante y no
// sirve: el frame cuenta desde su Now(). Con 100 y 2 ms de dibujo, después de volver 5 ms duerme 8,
// no los 13 que faltarían para la meta vieja más 10.
TEST(GameLoopTest, AClockThatGoesBackBetweenFramesStartsTheFpsCapOver)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;
    client.renderCost = 2ms;

    loop.RunFrame(WindowState{}, input);
    clock.Advance(-5ms);
    loop.RunFrame(WindowState{}, input);

    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 2u);
    EXPECT_EQ(sleeps[0], 8ms);
    EXPECT_EQ(sleeps[1], 8ms);
}

// Después de un frame más largo que el tope, el siguiente cuenta desde donde terminó el largo:
// duerme los 7 ms enteros y no se pone al día con un frame más corto.
TEST(GameLoopTest, AfterALongFrameTheFpsCapStartsOver)
{
    FakeClock clock;
    RecordingClient client{clock};
    GameLoop loop{clock, client, PacingMode::Decoupled, 100};
    InputState input;

    client.renderCost = 3ms;
    loop.RunFrame(WindowState{}, input);
    client.renderCost = 15ms;
    loop.RunFrame(WindowState{}, input);
    client.renderCost = 3ms;
    loop.RunFrame(WindowState{}, input);

    const auto sleeps = clock.Sleeps();
    ASSERT_EQ(sleeps.size(), 2u);
    EXPECT_EQ(sleeps[0], 7ms);
    EXPECT_EQ(sleeps[1], 7ms);
}

// En Faithful el tope no se usa (Resolve tampoco lo deja): espera como el original. Con 1, que
// fuera de Faithful dormiría casi 1 s por frame, los sueños son los mismos que sin tope.
TEST(GameLoopTest, AFaithfulLoopIgnoresTheFpsCap)
{
    std::vector<std::vector<Duration>> sleeps;
    for (const int maxFps : {0, 1})
    {
        FakeClock clock;
        RecordingClient client{clock};
        GameLoop loop{clock, client, PacingMode::Faithful, maxFps};
        InputState input;
        client.renderCost = 3ms;
        for (int frame = 0; frame < 10; ++frame)
        {
            loop.RunFrame(WindowState{}, input);
        }
        sleeps.emplace_back(clock.Sleeps().begin(), clock.Sleeps().end());
    }

    EXPECT_FALSE(sleeps[0].empty());
    EXPECT_EQ(sleeps[0], sleeps[1]);
}
// El orden de un frame (CL4.1): la red una vez, antes de los pasos, y el dibujo al final. El
// segundo frame corre dos pasos: en Decoupled, 80 ms después del primero, que solo fija el inicio;
// en Faithful, con un primer dibujo de 100 ms, que mide 6 ticks (93 ms) y deja el acumulador en 93
// después del paso del valor inicial. Un Poll después de los pasos o uno por paso darían otro
// orden; en el modo fiel, FaithfulDoesNotMeasureThePoll no los distingue, y este sí.
TEST(GameLoopTest, PollsOncePerFrameBeforeTheSteps)
{
    struct Case
    {
        PacingMode mode;
        const char* name;
        Duration firstRender;
        Duration betweenFrames;
    };
    for (const Case& mode :
         {Case{PacingMode::Decoupled, "Decoupled", 0ms, 80ms}, Case{PacingMode::Faithful, "Faithful", 100ms, 0ms}})
    {
        FakeClock clock;
        RecordingClient client{clock};
        GameLoop loop{clock, client, mode.mode};
        InputState input;
        client.renderCost = mode.firstRender;
        loop.RunFrame(WindowState{}, input);
        EXPECT_EQ(client.polls, 1u) << mode.name;
        ASSERT_FALSE(client.calls.empty()) << mode.name;
        EXPECT_EQ(client.calls.front(), LoopCall::Poll) << mode.name;

        client.renderCost = 0ms;
        client.calls.clear();
        clock.Advance(mode.betweenFrames);

        EXPECT_EQ(loop.RunFrame(WindowState{}, input), 2) << mode.name;
        EXPECT_EQ(client.calls,
                  (std::vector<LoopCall>{LoopCall::Poll, LoopCall::Step, LoopCall::Step, LoopCall::Render}))
            << mode.name;
    }
}

// Minimizada no se dibuja, pero la red sigue, como en el original (0x4e1d50 corre en cada vuelta
// del bucle de WinMain): 26 frames son 26 polls, con los 25 pasos de
// AMinimizedWindowStepsWithoutDrawing. En el modo fiel, también uno por frame.
TEST(GameLoopTest, PollsWhileMinimized)
{
    WindowState minimized;
    minimized.minimized = true;
    {
        FakeClock clock;
        RecordingClient client{clock};
        GameLoop loop{clock, client, PacingMode::Decoupled};
        InputState input;
        for (int frame = 0; frame < 26; ++frame)
        {
            loop.RunFrame(minimized, input);
        }

        EXPECT_EQ(client.polls, 26u);
        EXPECT_EQ(client.steps, 25u);
        EXPECT_TRUE(client.alphas.empty());
    }
    {
        FakeClock clock;
        RecordingClient client{clock};
        GameLoop loop{clock, client, PacingMode::Faithful};
        InputState input;
        for (int frame = 0; frame < 26; ++frame)
        {
            loop.RunFrame(minimized, input);
        }

        EXPECT_EQ(client.polls, 26u);
        EXPECT_TRUE(client.alphas.empty());
    }
}

// En el modo fiel el Poll va antes de BeginFrame, fuera de la medición, como el bucle de mensajes
// del original: lo que tarda se pierde de a ticks enteros, como el tiempo de los pasos (Q-C44).
// Con polls de 20 ms, que siempre cruzan el borde de un tick, cada frame dura 4 ticks y no 3: en
// 3 s, 48 frames y 56 pasos. Si el poll quedara adentro de la medición, la espera de 40 ms lo
// taparía y serían 64 y 74, como sin poll. Con polls de 10 ms, que no cruzan ninguno, no se pierde
// nada: 64 y 74.
TEST(GameLoopTest, FaithfulDoesNotMeasureThePoll)
{
    struct Case
    {
        Duration pollCost;
        std::uint64_t frames;
        std::uint64_t steps;
    };
    for (const Case& expected : {Case{20ms, 48, 56}, Case{10ms, 64, 74}})
    {
        FakeClock clock;
        RecordingClient client{clock};
        GameLoop loop{clock, client, PacingMode::Faithful};
        InputState input;
        client.pollCost = expected.pollCost;

        std::uint64_t frames = 0;
        while (clock.Now() < 3s)
        {
            loop.RunFrame(WindowState{}, input);
            ++frames;
        }

        const auto pollMs = std::chrono::duration_cast<std::chrono::milliseconds>(expected.pollCost).count();
        EXPECT_EQ(frames, expected.frames) << "polls de " << pollMs << " ms";
        EXPECT_EQ(client.steps, expected.steps) << "polls de " << pollMs << " ms";
        EXPECT_EQ(client.polls, frames) << "polls de " << pollMs << " ms";
        EXPECT_EQ(clock.Now(), 3s) << "polls de " << pollMs << " ms";
    }
}
} // namespace
