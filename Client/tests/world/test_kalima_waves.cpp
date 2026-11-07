#include "World/KalimaWaves.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace
{
using Client::World::KalimaWaveCellCount;
using Client::World::KalimaWavePlane;
using Client::World::KalimaWaveSide;
using Client::World::KalimaWaveState;
using Client::World::UpdateKalimaWaves;

constexpr std::size_t Cell(std::size_t row, std::size_t column)
{
    return row * KalimaWaveSide + column;
}

bool IsBorder(std::size_t cell)
{
    return cell < KalimaWaveSide || cell >= KalimaWaveCellCount - KalimaWaveSide || cell % KalimaWaveSide == 0 ||
           cell % KalimaWaveSide == KalimaWaveSide - 1;
}

class KalimaWavesTest : public ::testing::TestWithParam<bool>
{
protected:
    std::unique_ptr<KalimaWaveState> m_state = std::make_unique<KalimaWaveState>();

    void SetUp() override
    {
        m_state->selected = GetParam();
    }

    KalimaWavePlane& Destination()
    {
        return m_state->planes[GetParam() ? 1U : 0U];
    }

    KalimaWavePlane& Source()
    {
        return m_state->planes[GetParam() ? 0U : 1U];
    }

    void ExpectPreserved(const KalimaWaveState& before)
    {
        EXPECT_EQ(m_state->selected, before.selected);
        const std::size_t selected = before.selected ? 1U : 0U;
        EXPECT_EQ(Source(), before.planes[selected ^ 1U]);
        for (std::size_t cell = 0; cell < KalimaWaveCellCount; ++cell)
        {
            if (IsBorder(cell))
            {
                ASSERT_EQ(Destination()[cell], before.planes[selected][cell]) << cell;
            }
        }
    }

    void ExpectDestination(const KalimaWavePlane& expected)
    {
        for (std::size_t cell = 0; cell < KalimaWaveCellCount; ++cell)
        {
            ASSERT_EQ(Destination()[cell], expected[cell]) << cell;
        }
    }
};

// Detecta dirección invertida, escrituras interiores omitidas, toggle y pérdida del destino previo.
TEST_P(KalimaWavesTest, UpdatesEveryInteriorCellWithoutToggling)
{
    Source().fill(2U);
    for (std::size_t cell = 0; cell < KalimaWaveCellCount; ++cell)
    {
        if (IsBorder(cell))
        {
            Destination()[cell] = 0xa5000000U + static_cast<std::uint32_t>(cell);
        }
    }
    const auto before = std::make_unique<KalimaWaveState>(*m_state);
    const auto expected = std::make_unique<KalimaWavePlane>(Destination());
    for (std::size_t cell = 0; cell < KalimaWaveCellCount; ++cell)
    {
        if (!IsBorder(cell))
        {
            (*expected)[cell] = 4U;
        }
    }

    UpdateKalimaWaves(*m_state);
    ExpectDestination(*expected);
    ExpectPreserved(*before);

    for (std::size_t cell = 0; cell < KalimaWaveCellCount; ++cell)
    {
        if (!IsBorder(cell))
        {
            (*expected)[cell] = 0U;
        }
    }
    UpdateKalimaWaves(*m_state);
    ExpectDestination(*expected);
    ExpectPreserved(*before);
}

struct ImpulseCase
{
    std::size_t source;
    std::array<std::size_t, 4> affected;
    std::size_t count;
};

// Expected geométrico manual: no diagonales, centro, wrap espacial ni dependencia in-place.
TEST_P(KalimaWavesTest, SpreadsInteriorImpulsesOnlyToOrthogonalInteriorNeighbors)
{
    constexpr std::array Cases{
        ImpulseCase{Cell(17, 61), {Cell(16, 61), Cell(17, 60), Cell(17, 62), Cell(18, 61)}, 4},
        ImpulseCase{Cell(1, 1), {Cell(1, 2), Cell(2, 1), 0, 0}, 2},
        ImpulseCase{Cell(1, 254), {Cell(1, 253), Cell(2, 254), 0, 0}, 2},
        ImpulseCase{Cell(254, 1), {Cell(253, 1), Cell(254, 2), 0, 0}, 2},
        ImpulseCase{Cell(254, 254), {Cell(253, 254), Cell(254, 253), 0, 0}, 2},
    };
    for (const auto& test : Cases)
    {
        SCOPED_TRACE(test.source);
        Source().fill(0U);
        Destination().fill(0U);
        Source()[test.source] = 32U;
        const auto before = std::make_unique<KalimaWaveState>(*m_state);
        const auto expected = std::make_unique<KalimaWavePlane>();
        for (std::size_t neighbor = 0; neighbor < test.count; ++neighbor)
        {
            (*expected)[test.affected[neighbor]] = 15U;
        }

        UpdateKalimaWaves(*m_state);
        ExpectDestination(*expected);
        ExpectPreserved(*before);
    }
}

// Los bordes fuente alimentan el interior; sus cuatro esquinas nunca se leen.
TEST_P(KalimaWavesTest, UsesEachSourceEdgeWithoutWrappingCorners)
{
    constexpr std::array Cases{
        ImpulseCase{Cell(0, 0), {}, 0},
        ImpulseCase{Cell(0, 255), {}, 0},
        ImpulseCase{Cell(255, 0), {}, 0},
        ImpulseCase{Cell(255, 255), {}, 0},
        ImpulseCase{Cell(0, 1), {Cell(1, 1), 0, 0, 0}, 1},
        ImpulseCase{Cell(0, 254), {Cell(1, 254), 0, 0, 0}, 1},
        ImpulseCase{Cell(255, 1), {Cell(254, 1), 0, 0, 0}, 1},
        ImpulseCase{Cell(255, 254), {Cell(254, 254), 0, 0, 0}, 1},
        ImpulseCase{Cell(1, 0), {Cell(1, 1), 0, 0, 0}, 1},
        ImpulseCase{Cell(254, 0), {Cell(254, 1), 0, 0, 0}, 1},
        ImpulseCase{Cell(1, 255), {Cell(1, 254), 0, 0, 0}, 1},
        ImpulseCase{Cell(254, 255), {Cell(254, 254), 0, 0, 0}, 1},
    };
    for (const auto& test : Cases)
    {
        SCOPED_TRACE(test.source);
        Source().fill(0U);
        Destination().fill(0U);
        Source()[test.source] = 32U;
        const auto before = std::make_unique<KalimaWaveState>(*m_state);
        const auto expected = std::make_unique<KalimaWavePlane>();
        for (std::size_t neighbor = 0; neighbor < test.count; ++neighbor)
        {
            (*expected)[test.affected[neighbor]] = 15U;
        }

        UpdateKalimaWaves(*m_state);
        ExpectDestination(*expected);
        ExpectPreserved(*before);
    }
}

// Fija los cuatro offsets con amplitudes y coordenadas asimétricas, sin replicar el cálculo.
TEST_P(KalimaWavesTest, CombinesFourDistinctNeighborsWithTheCorrectStride)
{
    Source()[Cell(36, 73)] = 2U;
    Source()[Cell(37, 72)] = 4U;
    Source()[Cell(37, 74)] = 8U;
    Source()[Cell(38, 73)] = 16U;
    const auto before = std::make_unique<KalimaWaveState>(*m_state);
    const auto expected = std::make_unique<KalimaWavePlane>();
    (*expected)[Cell(35, 73)] = 1U;
    (*expected)[Cell(36, 72)] = 3U;
    (*expected)[Cell(36, 74)] = 5U;
    (*expected)[Cell(37, 71)] = 2U;
    (*expected)[Cell(37, 73)] = 15U;
    (*expected)[Cell(37, 75)] = 4U;
    (*expected)[Cell(38, 72)] = 10U;
    (*expected)[Cell(38, 74)] = 12U;
    (*expected)[Cell(39, 73)] = 8U;

    UpdateKalimaWaves(*m_state);
    ExpectDestination(*expected);
    ExpectPreserved(*before);
}

// Los literales analíticos distinguen ambos SAR de división y el wrap antes de cada uno.
TEST_P(KalimaWavesTest, PreservesAllDwordPatternsAcrossWrappedArithmeticShifts)
{
    struct ScalarCase
    {
        std::array<std::uint32_t, 4> neighbors;
        std::uint32_t oldValue;
        std::uint32_t expected;
    };
    constexpr std::array Cases{
        ScalarCase{{0U, 0U, 0U, 0U}, 1U, 0U},
        ScalarCase{{0U, 0U, 0U, 0U}, 16U, 0xfffffff1U},
        ScalarCase{{0xffffffffU, 0U, 0U, 0U}, 0xfffffff1U, 14U},
        ScalarCase{{0x7fffffffU, 1U, 0U, 0U}, 0U, 0xc4000000U},
        ScalarCase{{0U, 0U, 0U, 0U}, 0x80000000U, 0x88000000U},
        ScalarCase{{0U, 0U, 0U, 0U}, 0x80000001U, 0x78000000U},
    };
    for (const auto& test : Cases)
    {
        SCOPED_TRACE(test.expected);
        Source().fill(0U);
        Destination().fill(0U);
        Source()[Cell(90, 143)] = test.neighbors[0];
        Source()[Cell(91, 142)] = test.neighbors[1];
        Source()[Cell(91, 144)] = test.neighbors[2];
        Source()[Cell(92, 143)] = test.neighbors[3];
        Destination()[Cell(91, 143)] = test.oldValue;
        const auto before = std::make_unique<KalimaWaveState>(*m_state);

        UpdateKalimaWaves(*m_state);
        EXPECT_EQ(Destination()[Cell(91, 143)], test.expected);
        ExpectPreserved(*before);
    }
}

INSTANTIATE_TEST_SUITE_P(BothPlanes, KalimaWavesTest, ::testing::Bool());
} // namespace
