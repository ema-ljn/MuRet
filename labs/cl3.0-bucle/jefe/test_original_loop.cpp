#include "FixedStep.h"
#include "TickGrid.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

// Jefe de la CL3.0 (docs/learning/ola-cl3.0-el-bucle-de-juego.md, actividad 12): el bucle de
// 0x637700 sobre una TickGrid, el ritmo que sale de él, y la espiral que no tiene.
//
// OriginalLoop es un modelo chico del bucle del original, escrito desde el decompilado
// (client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/00637700_FUN_00637700.asm) y la
// tabla "Lo que dice el binario del bucle" del plan de la CL3. No es código para el cliente: deja
// afuera la escena, la red y el otro delta (0x459b60), que no tocan el acumulador.
namespace
{
using Labs::FixedStep;
using Labs::TenMsTickUs;
using Labs::TickGrid;
using Labs::XpTickUs;

// La espera de 0x6385fd..0x638624: CMP [ebp-0x168],0x28 + JGE en 0x63860f, en los ms de
// GetTickCount.
constexpr std::int32_t WaitMs = 40;

// El paso que resta 0x638167, el mismo 0x28 que compara 0x637791.
constexpr std::int32_t StepMs = Labs::StepMs;

// Lo que vale DAT_006d5ee4 en el .data del binario, antes del primer frame (carril R).
constexpr std::int32_t BinaryInitialAccumulatorMs = 40;

class OriginalLoop
{
public:
    // `stepCostUs` es lo que tarda cada paso de la lógica, en µs; el dibujo no tarda nada.
    OriginalLoop(TickGrid grid, std::int64_t stepCostUs, std::int32_t initialAccumulatorMs = 0)
        : m_grid(grid), m_stepCostUs(stepCostUs), m_accumulatorMs(initialAccumulatorMs)
    {
    }

    // Un frame de 0x637700. Devuelve cuántos pasos corrió.
    int RunFrame()
    {
        int steps = 0;
        // CMP [0x6d5ee4],0x28 + JL en 0x637798: el acumulador es un int con signo y no tiene tope.
        while (m_accumulatorMs >= StepMs)
        {
            m_accumulatorMs -= StepMs; // SUB EAX,0x28 en 0x638167
            m_nowUs += m_stepCostUs;   // el paso tarda, pero nadie lo mide
            ++steps;
        }
        const std::uint32_t start = m_grid.Ms(m_nowUs); // 0x63852b, después de los pasos
        // La espera activa: la resta es de DWORD y la comparación, con signo.
        while (static_cast<std::int32_t>(m_grid.Ms(m_nowUs) - start) < WaitMs)
        {
            ++m_nowUs;
        }
        m_accumulatorMs += static_cast<std::int32_t>(m_grid.Ms(m_nowUs) - start); // ADD en 0x638632
        return steps;
    }

    [[nodiscard]] std::int64_t NowUs() const
    {
        return m_nowUs;
    }

    [[nodiscard]] std::int32_t AccumulatorMs() const
    {
        return m_accumulatorMs;
    }

private:
    TickGrid m_grid;
    std::int64_t m_stepCostUs;
    std::int32_t m_accumulatorMs;
    std::int64_t m_nowUs = 0;
};

// Los pasos de cada uno de los primeros `frames` frames.
std::vector<int> StepsPerFrame(OriginalLoop& loop, int frames)
{
    std::vector<int> steps;
    for (int frame = 0; frame < frames; ++frame)
    {
        steps.push_back(loop.RunFrame());
    }
    return steps;
}

int Sum(const std::vector<int>& steps, std::size_t first, std::size_t count)
{
    const auto begin = steps.begin() + static_cast<std::ptrdiff_t>(first);
    return std::accumulate(begin, begin + static_cast<std::ptrdiff_t>(count), 0);
}

// Cada frame espera 3 ticks: 64 frames son 192 ticks, 3 s justos (21,33 FPS). Los pasos no tardan.
TEST(OriginalLoopTest, SixtyFourFramesTakeThreeSeconds)
{
    OriginalLoop loop{TickGrid{XpTickUs}, 0};
    const std::vector<int> steps = StepsPerFrame(loop, 64);
    EXPECT_EQ(loop.NowUs(), 3'000'000);
    // 3 s son 75 pasos: 73 corridos y los 80 ms que esperan al frame 65.
    EXPECT_EQ(Sum(steps, 0, steps.size()), 73);
    EXPECT_EQ(loop.AccumulatorMs(), 80);
}

TEST(OriginalLoopTest, TheFirstThirteenFrames)
{
    OriginalLoop loop{TickGrid{XpTickUs}, 0};
    EXPECT_EQ(StepsPerFrame(loop, 13), (std::vector<int>{0, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2}));
}

// "7 pasos cada 6 frames" no es exacto: desde el frame 2, cada 64 seguidos suman 75, con 11 frames
// de 2 pasos, y cada 6 seguidos suman 7 u 8. Los frames 31 a 36 dan 2, 1, 1, 1, 1, 2.
TEST(OriginalLoopTest, SeventyFiveStepsEverySixtyFourFrames)
{
    constexpr std::size_t Frames = 300;
    OriginalLoop loop{TickGrid{XpTickUs}, 0};
    const std::vector<int> steps = StepsPerFrame(loop, static_cast<int>(Frames));
    for (std::size_t first = 1; first + 64 <= Frames; ++first)
    {
        const auto begin = steps.begin() + static_cast<std::ptrdiff_t>(first);
        EXPECT_EQ(Sum(steps, first, 64), 75) << "desde el frame " << first + 1;
        EXPECT_EQ(std::count(begin, begin + 64, 2), 11) << "desde el frame " << first + 1;
    }
    for (std::size_t first = 1; first + 6 <= Frames; ++first)
    {
        const int sum = Sum(steps, first, 6);
        EXPECT_TRUE(sum == 7 || sum == 8) << "desde el frame " << first + 1 << ": " << sum;
    }
    EXPECT_EQ(std::vector<int>(steps.begin() + 30, steps.begin() + 36), (std::vector<int>{2, 1, 1, 1, 1, 2}));
}

// Con un tick de 10 ms la espera termina a los 40 justos: 25 frames por segundo y, desde el
// segundo frame, 1 paso en cada uno.
TEST(OriginalLoopTest, WithTenMsTicksTwentyFiveFramesPerSecond)
{
    OriginalLoop loop{TickGrid{TenMsTickUs}, 0};
    const std::vector<int> steps = StepsPerFrame(loop, 25);
    EXPECT_EQ(loop.NowUs(), 1'000'000);
    EXPECT_EQ(steps.front(), 0);
    EXPECT_EQ(std::count(steps.begin() + 1, steps.end(), 1), 24);
}

// La espiral que no pasa: con pasos de 50 ms, más que su paso, el original no mide lo que tardan,
// así que cada frame sigue sumando 46 o 47 ms y el juego se frena. En 10 s, 100 frames y 116 pasos,
// no los 250 que tocan.
TEST(OriginalLoopTest, SlowStepsSlowTheGameDownInsteadOfSpiralling)
{
    constexpr std::int64_t SlowStepUs = 50'000;
    constexpr std::int64_t TenSecondsUs = 10'000'000;
    OriginalLoop loop{TickGrid{XpTickUs}, SlowStepUs};
    int frames = 0;
    int steps = 0;
    while (loop.NowUs() < TenSecondsUs)
    {
        steps += loop.RunFrame();
        ++frames;
    }
    EXPECT_EQ(frames, 100);
    EXPECT_EQ(steps, 116);
}

// Al lado, un FixedStep que mide el frame entero y no tiene tope: el primer frame mide 40 ms, y
// cada uno de los siguientes, lo que tardaron los pasos del anterior. La deuda crece sola.
TEST(OriginalLoopTest, AFixedStepThatMeasuresTheStepsSpirals)
{
    constexpr int SlowStepMs = 50;
    FixedStep step;
    std::vector<int> steps;
    int frameMs = StepMs;
    for (int frame = 0; frame < 15; ++frame)
    {
        steps.push_back(step.Advance(frameMs));
        frameMs = steps.back() * SlowStepMs;
    }
    EXPECT_EQ(steps, (std::vector<int>{1, 1, 1, 1, 2, 2, 3, 3, 4, 5, 6, 8, 10, 12, 15}));
}

// Carril R: en el binario el acumulador no arranca en 0 sino en 40 (.data, 0x6d5ee4 = 0x28). El
// primer frame corre 1 paso; desde ahí el estado es el mismo que arrancando en 0 después del
// frame 1, así que solo cambia el primero, y 64 frames siguen tardando 3 s.
TEST(OriginalLoopTest, TheBinaryStartsTheAccumulatorAtForty)
{
    OriginalLoop loop{TickGrid{XpTickUs}, 0, BinaryInitialAccumulatorMs};
    const std::vector<int> steps = StepsPerFrame(loop, 64);
    EXPECT_EQ(std::vector<int>(steps.begin(), steps.begin() + 13),
              (std::vector<int>{1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2}));
    EXPECT_EQ(Sum(steps, 0, steps.size()), 74);
    EXPECT_EQ(loop.NowUs(), 3'000'000);
}
} // namespace
