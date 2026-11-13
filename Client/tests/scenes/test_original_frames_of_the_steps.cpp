#include "Scenes/OriginalFramesOfTheSteps.h"

#include "App/LoopTiming.h"
#include "Core/IClock.h"
#include "Render/Models/Animation.h"

#include <gtest/gtest.h>

#include <chrono>
#include <utility>
#include <vector>

namespace
{
using Client::Render::Models::OriginalFramePace;
using Client::Scenes::OriginalFramesOfTheSteps;

// Cómo cuenta los frames del original el dibujo: por los pasos de la escena, o por el reloj del dibujo, como
// antes de #307.
enum class Count
{
    ByTheSteps,
    ByTheDrawClock
};

// Los pasos que vio cada frame del original, con el bucle de GameLoop a `fps` (con `jitter` de más o de menos en
// cada dibujo, un patrón fijo): antes de cada dibujo, los pasos de 40 ms que debe el reloj, y después el dibujo,
// que cuenta sus frames del original. Un dibujo que corre más de un frame deja una entrada por frame, y el
// segundo no vio ningún paso.
std::vector<int> StepsPerOriginalFrame(double fps, int draws, Count count = Count::ByTheSteps,
                                       Client::Core::Duration jitter = {})
{
    OriginalFramesOfTheSteps frames;
    Client::Render::Models::OriginalFrameCarry drawClockFrames;
    const auto drawTime = std::chrono::duration_cast<Client::Core::Duration>(std::chrono::duration<double>(1.0 / fps));
    Client::Core::Duration owed{};
    Client::Core::Duration clock{};
    int stepsSinceTheLast = 0;
    std::vector<int> seen;
    for (int draw = 0; draw < draws; ++draw)
    {
        const Client::Core::Duration elapsed = drawTime + (draw % 3 == 0   ? jitter
                                                           : draw % 3 == 1 ? -jitter
                                                                           : jitter * 0);
        owed += elapsed;
        clock += elapsed;
        while (owed >= Client::App::LogicStep)
        {
            owed -= Client::App::LogicStep;
            frames.Step();
            ++stepsSinceTheLast;
        }
        const int original = count == Count::ByTheSteps
                                 ? frames.FramesAtDraw(OriginalFramePace::ByTheClock)
                                 : drawClockFrames.FramesAt(clock, OriginalFramePace::ByTheClock);
        for (int frame = 0; frame < original; ++frame)
        {
            if (draw > 0)
            {
                seen.push_back(stepsSinceTheLast);
            }
            stepsSinceTheLast = 0;
        }
    }
    return seen;
}

// 02 § El ritmo: el original corre los pasos que debe y después dibuja, así que cada frame ve 1 o 2 pasos
// (75 cada 64 frames). Contados por los pasos de la escena, igual a cualquier FPS de MuRet desde 25; por el
// reloj del dibujo, a 60 FPS, unos 40 por minuto no veían ninguno y el siguiente veía 2: la capa del que camina
// daba un tirón (#307).
TEST(OriginalFramesOfTheStepsTest, EachOriginalFrameSeesOneOrTwoStepsAtAnyFrameRate)
{
    for (const double fps : {25.0, 30.0, 59.94, 60.0, 75.0, 144.0, 240.0})
    {
        const std::vector<int> seen = StepsPerOriginalFrame(fps, static_cast<int>(fps * 60));
        ASSERT_FALSE(seen.empty()) << fps << " FPS";
        int twos = 0;
        for (const int steps : seen)
        {
            EXPECT_TRUE(steps == 1 || steps == 2) << fps << " FPS: un frame del original vio " << steps << " pasos";
            twos += steps == 2 ? 1 : 0;
        }
        // 11 de cada 64 frames ven 2 pasos: 220 de los 1.279 de un minuto.
        EXPECT_NEAR(static_cast<double>(twos) / static_cast<double>(seen.size()), 11.0 / 64.0, 0.01) << fps << " FPS";
    }
}

// Por debajo de 25 FPS un dibujo puede deber más de 46,875 ms de pasos. El original nunca corre dos frames en un
// dibujo (0x637791..0x6381a5: los pasos que debe y un dibujo): cada frame ve al menos un paso, y por debajo de
// 21,33 FPS, más de dos, como el original en una máquina lenta. Con un poco de jitter, lo mismo.
TEST(OriginalFramesOfTheStepsTest, BelowTwentyFiveFpsNoOriginalFrameIsLeftWithoutSteps)
{
    using namespace std::chrono_literals;
    for (const auto& [fps, jitter] :
         {std::pair{10.0, Client::Core::Duration{}}, std::pair{15.0, Client::Core::Duration{}},
          std::pair{20.0, Client::Core::Duration{}}, std::pair{21.33, Client::Core::Duration{}},
          std::pair{22.0, Client::Core::Duration{}}, std::pair{24.0, Client::Core::Duration{}},
          std::pair{25.0, Client::Core::Duration{1ms}}, std::pair{60.0, Client::Core::Duration{4ms}}})
    {
        const std::vector<int> seen = StepsPerOriginalFrame(fps, static_cast<int>(fps * 60), Count::ByTheSteps, jitter);
        ASSERT_FALSE(seen.empty()) << fps << " FPS";
        for (const int steps : seen)
        {
            EXPECT_GE(steps, 1) << fps << " FPS: un frame del original sin pasos";
            EXPECT_LE(steps, 3) << fps << " FPS";
        }
    }
}

// Con el reloj del dibujo, a 22 FPS no había frames sin pasos; contados por los pasos sin el tope de uno por
// dibujo, eran 139 por minuto. Con el tope, ninguno, y cada dibujo es como mucho un frame.
TEST(OriginalFramesOfTheStepsTest, ADrawIsAtMostOneOriginalFrame)
{
    OriginalFramesOfTheSteps frames;
    EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::ByTheClock), 1);
    for (int step = 0; step < 5; ++step)
    {
        frames.Step();
    }
    // 200 ms de pasos: el original lo habría dibujado en un solo frame.
    EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::ByTheClock), 1);
    // Lo que sobró no alcanza para otro frame sin pasos nuevos.
    EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::ByTheClock), 0);
    frames.Step();
    EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::ByTheClock), 1) << "lo que sobró del frame más 40 ms";
}

// Lo de antes de #307, por el reloj del dibujo: a 60 FPS hay frames del original que no ven ningún paso.
TEST(OriginalFramesOfTheStepsTest, ByTheDrawClockSomeOriginalFramesSawNoStepAt60Fps)
{
    const std::vector<int> seen = StepsPerOriginalFrame(60.0, 60 * 60, Count::ByTheDrawClock);
    int none = 0;
    for (const int steps : seen)
    {
        none += steps == 0 ? 1 : 0;
    }
    EXPECT_GT(none, 0);
}

// Sin pasos no hay frame del original: los dibujos de en medio son intermedios.
TEST(OriginalFramesOfTheStepsTest, WithoutStepsTheDrawsAreIntermediate)
{
    OriginalFramesOfTheSteps frames;
    EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::ByTheClock), 1) << "el primero corre uno";
    for (int draw = 0; draw < 10; ++draw)
    {
        EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::ByTheClock), 0);
    }
    frames.Step();
    frames.Step();
    EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::ByTheClock), 1) << "80 ms de pasos: un frame de 46,875";
}

// El PrepareFrame del juego pregunta antes del dibujo si va a ser un frame del original, para olvidar el scan de
// 0x505820 solo entonces: la respuesta es la del FramesAtDraw que sigue, a cualquier FPS y en los dos modos, y
// preguntar no cuenta el dibujo. A más de 25 FPS en el desacoplado hay dibujos intermedios; en el fiel, ninguno.
TEST(OriginalFramesOfTheStepsTest, TheQuestionBeforeTheDrawIsWhatTheDrawCounts)
{
    for (const OriginalFramePace pace : {OriginalFramePace::ByTheClock, OriginalFramePace::OncePerDraw})
    {
        for (const double fps : {10.0, 21.33, 25.0, 60.0, 144.0})
        {
            OriginalFramesOfTheSteps frames;
            const auto drawTime =
                std::chrono::duration_cast<Client::Core::Duration>(std::chrono::duration<double>(1.0 / fps));
            Client::Core::Duration owed{};
            int intermediate = 0;
            for (int draw = 0; draw < static_cast<int>(fps * 10.0); ++draw)
            {
                owed += drawTime;
                while (owed >= Client::App::LogicStep)
                {
                    owed -= Client::App::LogicStep;
                    frames.Step();
                }
                const bool asked = frames.NextDrawIsAnOriginalFrame(pace);
                ASSERT_EQ(frames.NextDrawIsAnOriginalFrame(pace), asked) << fps << " FPS, dibujo " << draw;
                const int counted = frames.FramesAtDraw(pace);
                ASSERT_EQ(asked, counted > 0) << fps << " FPS, dibujo " << draw;
                intermediate += counted == 0 ? 1 : 0;
            }
            if (pace == OriginalFramePace::OncePerDraw)
            {
                EXPECT_EQ(intermediate, 0) << fps << " FPS";
            }
            else if (fps > 25.0)
            {
                EXPECT_GT(intermediate, 0) << fps << " FPS";
            }
        }
    }
}

// En el modo fiel, uno por dibujo, como antes: el dibujo fiel ya viene después de sus pasos.
TEST(OriginalFramesOfTheStepsTest, InTheFaithfulModeEachDrawIsOneOriginalFrame)
{
    OriginalFramesOfTheSteps frames;
    for (int draw = 0; draw < 10; ++draw)
    {
        EXPECT_EQ(frames.FramesAtDraw(OriginalFramePace::OncePerDraw), 1);
    }
}
} // namespace
