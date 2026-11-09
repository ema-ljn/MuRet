#include "Render/Models/OriginalRetainedMath.h"
#include "Core/Expected.h"
#include "World/IParticleTrigonometry.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace
{
namespace Math = Client::Render::Models::Detail;
using Math::Retained64;
using Math::RetainedClass;

void ExpectBits(const std::optional<double>& actual, double expected)
{
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(std::bit_cast<std::uint64_t>(*actual), std::bit_cast<std::uint64_t>(expected));
}

TEST(ParticleRetainedMathTest, Retains64BeforeProducts40And20)
{
    // A1/A2: h=1/2+2^-54; producto directo queda 0,625 ulp por encima del entero.
    const Retained64 h{0x8000000000000400ULL, -64, RetainedClass::Finite, false};
    ExpectBits(Math::RetainedProductPC53(h, 40.0f), 0x1.4000000000001p+4);
    ExpectBits(Math::RetainedProductPC53(h, 20.0f), 0x1.4000000000001p+3);
    ExpectBits(Math::RetainedProductPC53({h.significand, h.exponent, h.classification, true}, 40.0f),
               -0x1.4000000000001p+4);
}

TEST(ParticleRetainedMathTest, RoundsProductDirectlyTo53)
{
    // A7: R64 intermedio crea un empate; el producto exacto queda justo por encima.
    ExpectBits(Math::RetainedProductPC53({0x8000000000001ccdULL, -64, RetainedClass::Finite, false}, 40.0f),
               0x1.4000000000005p+4);
}

TEST(ParticleRetainedMathTest, PreservesCanonicalClasses)
{
    for (bool negative : {false, true})
    {
        ExpectBits(Math::RetainedProductPC53({0, 0, RetainedClass::Finite, negative}, 40.0f), negative ? -0.0 : 0.0);
        ExpectBits(Math::RetainedProductPC53({0, 0, RetainedClass::Infinity, negative}, 20.0f),
                   negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity());
    }
    const auto nan = Math::RetainedProductPC53({0, 0, RetainedClass::NaN, false}, 0.5f);
    ASSERT_TRUE(nan.has_value());
    EXPECT_TRUE(std::isnan(*nan));
}

TEST(ParticleRetainedMathTest, RequiresExactDoubleRepresentation)
{
    // Potencias de dos: límites exactos de double normal/subnormal; ningún oráculo trig.
    constexpr std::uint64_t OneBit = 0x8000000000000000ULL;
    ExpectBits(Math::RetainedProductPC53({OneBit, -63, RetainedClass::Finite, false}, 40.0f), 40.0);
    ExpectBits(Math::RetainedProductPC53({OneBit, -1137, RetainedClass::Finite, false}, 1.0f), 0x1p-1074);
    ExpectBits(Math::RetainedProductPC53({OneBit, 960, RetainedClass::Finite, false}, 1.0f), 0x1p1023);
    EXPECT_FALSE(Math::RetainedProductPC53({OneBit, -1138, RetainedClass::Finite, false}, 1.0f));
    EXPECT_FALSE(Math::RetainedProductPC53({0xc000000000000000ULL, -1137, RetainedClass::Finite, false}, 1.0f));
    EXPECT_FALSE(Math::RetainedProductPC53({OneBit, 961, RetainedClass::Finite, false}, 1.0f));
    EXPECT_FALSE(Math::RetainedProductPC53(
        {OneBit, std::numeric_limits<std::int32_t>::min(), RetainedClass::Finite, false}, 1.0f));
    EXPECT_FALSE(Math::RetainedProductPC53(
        {OneBit, std::numeric_limits<std::int32_t>::max(), RetainedClass::Finite, false}, 1.0f));
}

TEST(ParticleRetainedMathTest, RejectsInvalidMultipliersForEveryOperation)
{
    for (float factor :
         {0.0f, -0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        EXPECT_FALSE(Math::RetainedProductPC53({0, 0, RetainedClass::Infinity, false}, factor));
        EXPECT_FALSE(Math::SineProductPC53(0.0, factor));
        EXPECT_FALSE(Math::CosineProductPC53(0.0, factor));
    }
}

TEST(ParticleRetainedMathTest, RejectsNonfiniteAndClosedRangeBoundaryArguments)
{
    for (double argument : {0x1p63, -0x1p63, 0x1p64, -0x1p64, std::numeric_limits<double>::infinity(),
                            -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        EXPECT_FALSE(Math::SineProductPC53(argument, 40.0f));
        EXPECT_FALSE(Math::CosineProductPC53(argument, 40.0f));
    }
}

TEST(ParticleRetainedMathTest, ImplementsZeroIdentitiesAndSigns)
{
    for (float factor : {40.0f, 20.0f, 0.5f})
    {
        ExpectBits(Math::SineProductPC53(0.0, factor), 0.0);
        ExpectBits(Math::SineProductPC53(-0.0, factor), -0.0);
        ExpectBits(Math::CosineProductPC53(0.0, factor), static_cast<double>(factor));
        ExpectBits(Math::CosineProductPC53(-0.0, factor), static_cast<double>(factor));
    }
}

TEST(ParticleRetainedMathTest, PreservesTinySineAndRejectsUnderflow)
{
    // sin(2^-1074) difiere de su argumento por menos de media ulp64.
    ExpectBits(Math::SineProductPC53(0x1p-1074, 1.0f), 0x1p-1074);
    ExpectBits(Math::SineProductPC53(-0x1p-1074, 1.0f), -0x1p-1074);
    EXPECT_FALSE(Math::SineProductPC53(0x1p-1074, 0.5f));
}

TEST(ParticleRetainedMathTest, EvaluatesSmallNonzeroArgumentsAnalytically)
{
    // Taylor: sin(2^-32) está a < 1/6 ulp64 del argumento; cos a < media ulp64 de 1.
    ExpectBits(Math::SineProductPC53(0x1p-32, 40.0f), 0x1.4p-27);
    ExpectBits(Math::SineProductPC53(-0x1p-32, 20.0f), -0x1.4p-28);
    ExpectBits(Math::CosineProductPC53(0x1p-32, 40.0f), 40.0);
}

TEST(ParticleRetainedMathTest, RetainsCosine64BeforeProduct)
{
    // Serie alternante de cos(1): R64 = 0x8a51407da8345c92 * 2^-64.
    // R53(h64*40) termina en 482e; R53(double(cos113)*40), en 482f.
    // Expectativa matemática independiente; no acredita FCOS del original.
    ExpectBits(Math::CosineProductPC53(1.0, 40.0f), 0x1.59cb213a2482ep+4);
}

TEST(PortableParticleTrigonometryTest, ReturnsProductsThroughRequiredInterface)
{
    const Client::World::PortableParticleTrigonometry backend;
    const Client::World::IParticleTrigonometry& trig = backend;
    const auto sine = trig.SineProductPC53(-0.0, 20.0f);
    const auto cosine = trig.CosineProductPC53(0.0, 40.0f);
    ASSERT_TRUE(sine.HasValue());
    ASSERT_TRUE(cosine.HasValue());
    EXPECT_EQ(std::bit_cast<std::uint64_t>(sine.Value()), std::bit_cast<std::uint64_t>(-0.0));
    EXPECT_EQ(std::bit_cast<std::uint64_t>(cosine.Value()), std::bit_cast<std::uint64_t>(40.0));
}

TEST(PortableParticleTrigonometryTest, TranslatesRepresentationFailure)
{
    const Client::World::PortableParticleTrigonometry trig;
    const auto sine = trig.SineProductPC53(0x1p63, 20.0f);
    const auto cosine = trig.CosineProductPC53(0.0, -1.0f);
    ASSERT_FALSE(sine.HasValue());
    ASSERT_FALSE(cosine.HasValue());
    EXPECT_EQ(sine.Error(), Client::World::ParticleError::UnrepresentableTrigonometry);
    EXPECT_EQ(cosine.Error(), Client::World::ParticleError::UnrepresentableTrigonometry);
}
} // namespace
