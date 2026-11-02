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

// La espera de 0x6385fd..0x638624: CMP [ebp-0x168],0x28 en 0x63860f, JGE en 0x638616, en los ms
// de GetTickCount.
constexpr std::int32_t WaitMs = 40;

// El paso que resta 0x638167, el mismo 0x28 que compara 0x637791.
constexpr std::int32_t StepMs = Labs::StepMs;

// Lo que vale DAT_006d5ee4 en el .data del binario, antes del primer frame (carril R): el modelo
// arranca ahí, como el original.
constexpr std::int32_t BinaryInitialAccumulatorMs = 40;

// Una red del test, no del original: una espera sana dura como mucho 3 ticks de 15,625 ms. Si un
// modelo roto no sale nunca de la espera (una resta que no da la vuelta), el test falla en vez de
// colgarse.
constexpr std::int64_t WaitGuardUs = 1'000'000;

class OriginalLoop
{
public:
    // `stepCostUs` es lo que tarda cada paso de la lógica, en µs, y `startUs`, la hora del reloj al
    // empezar: 0, o cerca de la vuelta del DWORD.
    explicit OriginalLoop(TickGrid grid, std::int64_t stepCostUs = 0,
                          std::int32_t initialAccumulatorMs = BinaryInitialAccumulatorMs, std::int64_t startUs = 0)
        : m_grid(grid), m_stepCostUs(stepCostUs), m_accumulatorMs(initialAccumulatorMs), m_nowUs(startUs)
    {
    }

    // Un frame de 0x637700, con un dibujo que tarda `drawCostUs`. Devuelve cuántos pasos corrió.
    // Deja afuera la salida de 0x6381aa: si WM_DESTROY ya marcó el cierre, el frame vuelve antes de
    // medir, y eso solo pasa al salir.
    int RunFrame(std::int64_t drawCostUs = 0)
    {
        int steps = 0;
        // CMP [0x6d5ee4],0x28 en 0x637791, JL en 0x637798: el acumulador es un int con signo y no
        // tiene tope.
        while (m_accumulatorMs >= StepMs)
        {
            m_accumulatorMs -= StepMs; // SUB EAX,0x28 en 0x638167
            m_nowUs += m_stepCostUs;   // el paso tarda, pero nadie lo mide
            ++steps;
        }
        // 0x63852b, después de los pasos y del glClear y el viewport de 0x638516 y 0x63851c.
        const std::uint32_t start = m_grid.Ms(m_nowUs);
        m_nowUs += drawCostUs; // la escena, glFlush y SwapBuffers: esto sí se mide
        // La espera activa: la resta es de DWORD y la comparación, con signo.
        const std::int64_t waitStartUs = m_nowUs;
        while (static_cast<std::int32_t>(m_grid.Ms(m_nowUs) - start) < WaitMs)
        {
            ++m_nowUs;
            if (m_nowUs - waitStartUs > WaitGuardUs)
            {
                ADD_FAILURE() << "la espera no termina, en el µs " << m_nowUs;
                break;
            }
        }
        m_lastMeasuredMs = static_cast<std::int32_t>(m_grid.Ms(m_nowUs) - start);
        m_accumulatorMs += m_lastMeasuredMs; // ADD en 0x638632, sin recortar
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

    // Lo que sumó al acumulador el último frame.
    [[nodiscard]] std::int32_t LastMeasuredMs() const
    {
        return m_lastMeasuredMs;
    }

private:
    TickGrid m_grid;
    std::int64_t m_stepCostUs;
    std::int32_t m_accumulatorMs;
    std::int64_t m_nowUs;
    std::int32_t m_lastMeasuredMs = 0;
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
    OriginalLoop loop{TickGrid{XpTickUs}};
    const std::vector<int> steps = StepsPerFrame(loop, 64);
    EXPECT_EQ(loop.NowUs(), 3'000'000);
    // 3 s son 75 pasos, y el valor inicial, uno más: 74 corridos y 80 ms que esperan al frame 65.
    EXPECT_EQ(Sum(steps, 0, steps.size()), 74);
    EXPECT_EQ(loop.AccumulatorMs(), 80);
}

TEST(OriginalLoopTest, TheFirstThirteenFrames)
{
    OriginalLoop loop{TickGrid{XpTickUs}};
    EXPECT_EQ(StepsPerFrame(loop, 13), (std::vector<int>{1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2}));
}

// Cada frame termina justo en su tercer tick, y mide 46 una vez y 47 siete veces cada 8 frames:
// 8 frames son 24 ticks, 375 ms, y 375 / 8 = 46,875 de media.
TEST(OriginalLoopTest, EachFrameWaitsThreeTicksAndMeasures46Or47)
{
    constexpr int FramesPerCycle = 8;
    OriginalLoop loop{TickGrid{XpTickUs}};
    for (int frame = 0; frame < 64; ++frame)
    {
        static_cast<void>(loop.RunFrame());
        EXPECT_EQ(loop.NowUs(), (frame + 1) * 3 * XpTickUs) << "frame " << frame + 1;
        EXPECT_EQ(loop.LastMeasuredMs(), frame % FramesPerCycle == 0 ? 46 : 47) << "frame " << frame + 1;
    }
}

// "7 pasos cada 6 frames" no es exacto: desde el frame 2, cada 64 seguidos suman 75, con 11 frames
// de 2 pasos, y cada 6 seguidos suman 7 u 8. Los frames 31 a 36 dan 2, 1, 1, 1, 1, 2.
TEST(OriginalLoopTest, SeventyFiveStepsEverySixtyFourFrames)
{
    constexpr std::size_t Frames = 300;
    OriginalLoop loop{TickGrid{XpTickUs}};
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

// Con un tick de 10 ms la espera termina a los 40 justos: 25 frames por segundo y 1 paso en cada
// uno, desde el primero.
TEST(OriginalLoopTest, WithTenMsTicksTwentyFiveFramesPerSecond)
{
    OriginalLoop loop{TickGrid{TenMsTickUs}};
    const std::vector<int> steps = StepsPerFrame(loop, 25);
    EXPECT_EQ(loop.NowUs(), 1'000'000);
    EXPECT_EQ(std::count(steps.begin(), steps.end(), 1), 25);
}

// Con un reloj que cambia cada 1 ms, la espera mide 40 exactos: ni 39 ni 41. Con el tick de XP o el
// de 10 ms no se ve si la espera es de 40 o de 33: las dos terminan en el mismo tick.
TEST(OriginalLoopTest, WithOneMsTicksEachFrameMeasuresFortyExactly)
{
    constexpr std::int64_t OneMsTickUs = 1'000; // un reloj de 1 ms, como modelo
    OriginalLoop loop{TickGrid{OneMsTickUs}};
    for (int frame = 0; frame < 25; ++frame)
    {
        EXPECT_EQ(loop.RunFrame(), 1) << "frame " << frame + 1;
        EXPECT_EQ(loop.LastMeasuredMs(), 40) << "frame " << frame + 1;
    }
    EXPECT_EQ(loop.NowUs(), 1'000'000);
}

// Sin tope: un breakpoint de 10 s en el dibujo cae adentro de la medición (entre 0x63852b y
// 0x638632), el acumulador suma los 10.000 ms enteros y el frame siguiente corre 250 pasos.
TEST(OriginalLoopTest, ATenSecondDrawRunsTwoHundredFiftyStepsNextFrame)
{
    constexpr std::int64_t TenSecondsUs = 10'000'000;
    OriginalLoop loop{TickGrid{XpTickUs}};
    EXPECT_EQ(loop.RunFrame(TenSecondsUs), 1);
    EXPECT_EQ(loop.LastMeasuredMs(), 10'000);
    EXPECT_EQ(loop.RunFrame(), 250);
}

// Lo que tardan los pasos no se mide: con pasos de 10 ms, frames mientras el reloj no llegue a 3 s,
// corren 61 frames y 71 pasos, y el reloj termina en 3,015625 s. Midiendo desde antes de los pasos
// serían 64 frames y 74 pasos: 71 y no 74.
TEST(OriginalLoopTest, TimeSpentInStepsIsNotAccumulated)
{
    constexpr std::int64_t TenMsStepUs = 10'000;
    constexpr std::int64_t ThreeSecondsUs = 3'000'000;
    OriginalLoop loop{TickGrid{XpTickUs}, TenMsStepUs};
    int frames = 0;
    int steps = 0;
    while (loop.NowUs() < ThreeSecondsUs)
    {
        steps += loop.RunFrame();
        ++frames;
    }
    EXPECT_EQ(frames, 61);
    EXPECT_EQ(steps, 71);
    EXPECT_EQ(loop.NowUs(), 3'015'625);
}

// La espiral que no pasa: con pasos de 50 ms, más que su paso, el original no mide lo que tardan,
// así que cada frame sigue sumando 46 o 47 ms y el juego se frena. En 10 s, 99 frames y 115 pasos,
// no los 250 que tocan. Con el acumulador en 0 al empezar serían 100 y 116: el valor inicial mueve
// la cuenta en uno, no la conclusión.
TEST(OriginalLoopTest, SlowStepsSlowTheGameDownInsteadOfSpiralling)
{
    constexpr std::int64_t SlowStepUs = 50'000;
    constexpr std::int64_t TenSecondsUs = 10'000'000;
    for (const std::int32_t initialMs : {BinaryInitialAccumulatorMs, 0})
    {
        OriginalLoop loop{TickGrid{XpTickUs}, SlowStepUs, initialMs};
        int frames = 0;
        int steps = 0;
        while (loop.NowUs() < TenSecondsUs)
        {
            steps += loop.RunFrame();
            ++frames;
        }
        EXPECT_EQ(frames, initialMs == 0 ? 100 : 99) << "arrancando en " << initialMs;
        EXPECT_EQ(steps, initialMs == 0 ? 116 : 115) << "arrancando en " << initialMs;
    }
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

// Carril R: el modelo del plan arrancaba en 0, y el binario en 40 (.data, 0x6d5ee4 = 0x28).
// Arrancando en 0, el primer frame corre 0 pasos y 64 frames, 73; después del frame 1 los dos
// quedan igual (el reloj en el tick 3 y 46 ms en el acumulador), así que solo cambia el primero.
TEST(OriginalLoopTest, StartingAtZeroOnlyChangesTheFirstFrame)
{
    OriginalLoop fromForty{TickGrid{XpTickUs}};
    OriginalLoop fromZero{TickGrid{XpTickUs}, 0, 0};
    EXPECT_EQ(fromForty.RunFrame(), 1);
    EXPECT_EQ(fromZero.RunFrame(), 0);
    EXPECT_EQ(fromZero.NowUs(), fromForty.NowUs());
    EXPECT_EQ(fromZero.AccumulatorMs(), fromForty.AccumulatorMs());
    EXPECT_EQ(StepsPerFrame(fromZero, 63), StepsPerFrame(fromForty, 63));
    EXPECT_EQ(fromZero.NowUs(), 3'000'000);
}

// La vuelta de GetTickCount a los 2^32 ms no cambia nada, porque la espera resta en DWORD: 64
// frames que la cruzan siguen tardando 3 s y corren los mismos pasos que desde 0.
TEST(OriginalLoopTest, TheGetTickCountWrapChangesNothing)
{
    // 67 ticks (1,05 s) antes del primero que lee 0 después de la vuelta, el 274.877.907, y
    // múltiplo de 8: cada 8 ticks son 125 ms justos, así que cada frame mide lo mismo que desde 0.
    constexpr std::int64_t StartTick = 274'877'840;
    constexpr std::int64_t StartUs = StartTick * XpTickUs;
    OriginalLoop nearTheWrap{TickGrid{XpTickUs}, 0, BinaryInitialAccumulatorMs, StartUs};
    OriginalLoop fromZero{TickGrid{XpTickUs}};
    EXPECT_EQ(StepsPerFrame(nearTheWrap, 64), StepsPerFrame(fromZero, 64));
    EXPECT_EQ(nearTheWrap.NowUs() - StartUs, 3'000'000);
    EXPECT_EQ(nearTheWrap.AccumulatorMs(), 80);
}
} // namespace
