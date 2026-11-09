#include "Core/MillisecondTime.h"

#include "Core/Expected.h"
#include "Core/IMillisecondClock32.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cfenv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>

namespace
{
using Client::Core::Expected;
using Client::Core::IMillisecondClock32;
using Client::Core::Milliseconds32ToFloatRn;
using Client::Core::Unexpected;

// Dobles sólo para el contrato de inyección: ninguna muestra queda cacheada por la interfaz.
class ScriptedMillisecondClock32 final : public IMillisecondClock32
{
public:
    explicit ScriptedMillisecondClock32(std::span<const std::uint32_t> samples) : m_samples(samples) {}

    Expected<std::uint32_t, std::error_code> Sample() override
    {
        if (m_next == m_samples.size())
        {
            return Unexpected(std::make_error_code(std::errc::io_error));
        }
        return m_samples[m_next++];
    }

private:
    std::span<const std::uint32_t> m_samples;
    std::size_t m_next = 0;
};

// Esperados analíticos literales: espaciados 1, 2, 4, 128 y 256; un empate va al
// significando par. No se generan con un cast ni con el helper bajo prueba.
TEST(MillisecondTimeTest, PublishesUnsignedValuesWithNearestEvenBits)
{
    struct Case
    {
        std::uint32_t sample;
        std::uint32_t bits;
    };
    constexpr std::array Cases{
        Case{0U, 0x00000000U},          Case{1U, 0x3f800000U},          Case{16777215U, 0x4b7fffffU},
        Case{16777216U, 0x4b800000U},   Case{16777217U, 0x4b800000U},   Case{16777218U, 0x4b800001U},
        Case{16777219U, 0x4b800002U},   Case{16777220U, 0x4b800002U},   Case{16777221U, 0x4b800002U},
        Case{33554431U, 0x4c000000U},   Case{33554432U, 0x4c000000U},   Case{33554433U, 0x4c000000U},
        Case{33554434U, 0x4c000000U},   Case{33554435U, 0x4c000001U},   Case{33554438U, 0x4c000002U},
        Case{2147483519U, 0x4effffffU}, Case{2147483520U, 0x4effffffU}, Case{2147483583U, 0x4effffffU},
        Case{2147483584U, 0x4f000000U}, Case{2147483647U, 0x4f000000U}, Case{2147483648U, 0x4f000000U},
        Case{2147483775U, 0x4f000000U}, Case{2147483776U, 0x4f000000U}, Case{2147483777U, 0x4f000001U},
        Case{2147483904U, 0x4f000001U}, Case{2147484032U, 0x4f000002U}, Case{4294967040U, 0x4f7fffffU},
        Case{4294967167U, 0x4f7fffffU}, Case{4294967168U, 0x4f800000U}, Case{4294967169U, 0x4f800000U},
        Case{4294967295U, 0x4f800000U},
    };
    for (const auto& test : Cases)
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(Milliseconds32ToFloatRn(test.sample)), test.bits)
            << "raw32=" << test.sample;
    }
}

// Una resta de origen, conversión signed32 o reparación de wrap rompería estos resultados.
TEST(MillisecondTimeTest, PublishesRepeatedAndWrappedSamplesWithoutAnOrigin)
{
    constexpr std::array<std::uint32_t, 5> Samples{4294967294U, 4294967295U, 0U, 0U, 1U};
    constexpr std::array<std::uint32_t, 5> Bits{0x4f800000U, 0x4f800000U, 0U, 0U, 0x3f800000U};
    ScriptedMillisecondClock32 clock(Samples);
    IMillisecondClock32& source = clock;
    for (const auto bits : Bits)
    {
        const auto sampled = source.Sample();
        ASSERT_TRUE(sampled.HasValue());
        EXPECT_EQ(std::bit_cast<std::uint32_t>(Milliseconds32ToFloatRn(sampled.Value())), bits);
    }
    const auto failed = source.Sample();
    ASSERT_FALSE(failed.HasValue());
    EXPECT_EQ(failed.Error(), std::make_error_code(std::errc::io_error));
}

// qT se captura antes de otra llamada: convertirlo después conserva esa muestra, sin usar qD.
TEST(MillisecondTimeTest, KeepsTheCapturedSampleWhenTheSourceChanges)
{
    constexpr std::array<std::uint32_t, 2> Samples{16777217U, 2147483777U};
    ScriptedMillisecondClock32 clock(Samples);
    IMillisecondClock32& source = clock;
    const auto qT = source.Sample();
    const auto qD = source.Sample();
    ASSERT_TRUE(qT.HasValue());
    ASSERT_TRUE(qD.HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(Milliseconds32ToFloatRn(qT.Value())), 0x4b800000U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(Milliseconds32ToFloatRn(qD.Value())), 0x4f000001U);
}

TEST(MillisecondTimeTest, LeavesTheRoundingModeUnchanged)
{
    const int roundingMode = std::fegetround();
    EXPECT_EQ(std::bit_cast<std::uint32_t>(Milliseconds32ToFloatRn(4294967168U)), 0x4f800000U);
    EXPECT_EQ(std::fegetround(), roundingMode);
}
} // namespace
