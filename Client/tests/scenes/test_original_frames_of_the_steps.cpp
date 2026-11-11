#include "Scenes/OriginalFramesOfTheSteps.h"

#include "App/LoopTiming.h"
#include "Core/IClock.h"
#include "Render/Models/Animation.h"

#include <gtest/gtest.h>

#include <chrono>
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

// Los pasos que vio cada frame del original, con el bucle de GameLoop a `fps`: antes de cada dibujo, los
// pasos de 40 ms que debe el reloj, y después el dibujo, que cuenta sus frames del original.
std::vector<int> StepsPerOriginalFrame(double fps, int draws, Count count = Count::ByTheSteps)
{
    OriginalFramesOfTheSteps frames;
    Client::Render::Models::OriginalFrameCarry drawClockFrames;
    const auto drawTime = std::chrono::duration_cast<Client::Core::Duration>(std::chrono::duration<double>(1.0 / fps));
    Client::Core::Duration owed{};
    int stepsSinceTheLast = 0;
    std::vector<int> seen;
    for (int draw = 0; draw < draws; ++draw)
    {
        owed += drawTime;
        while (owed >= Client::App::LogicStep)
        {
            owed -= Client::App::LogicStep;
            frames.Step();
            ++stepsSinceTheLast;
        }
        const int original = count == Count::ByTheSteps
                                 ? frames.FramesAtDraw(OriginalFramePace::ByTheClock)
                                 : drawClockFrames.FramesAt(drawTime * draw, OriginalFramePace::ByTheClock);
        if (original > 0 && draw > 0)
        {
            seen.push_back(stepsSinceTheLast);
        }
        if (original > 0)
        {
            stepsSinceTheLast = 0;
        }
    }
    return seen;
}

// 02 § El ritmo: el original corre los pasos que debe y después dibuja, así que cada frame ve 1 o 2 pasos
// (75 cada 64 frames). Contados por los pasos de la escena, igual a cualquier FPS de MuRet; por el reloj del
// dibujo, a 60 FPS, unos 40 por minuto no veían ninguno y el siguiente veía 2: la capa del que camina daba un
// tirón (#307).
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
        // 11 de cada 64 frames ven 2 pasos.
        EXPECT_NEAR(static_cast<double>(twos) / static_cast<double>(seen.size()), 11.0 / 64.0, 0.01) << fps << " FPS";
    }
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
