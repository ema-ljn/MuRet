// Labs::ModularInverse: uno a mano, los que no tienen inverso, y una propiedad con módulos de 18
// bits como los de SimpleModulus.

#include "ModularInverse.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <numeric>
#include <optional>
#include <random>

namespace
{

using Labs::ModularInverse;

// A mano: 3 × 5 = 15 = 2 × 7 + 1.
TEST(ModularInverseTest, ThreeModuloSevenIsFive)
{
    EXPECT_EQ(ModularInverse(3, 7), 5u);
}

// a se toma módulo m: 10 es 3 módulo 7, y tiene el mismo inverso.
TEST(ModularInverseTest, ReducesABeforeInverting)
{
    EXPECT_EQ(ModularInverse(10, 7), 5u);
}

// 6 y 9 comparten el 3: 6x mod 9 es siempre 0, 3 o 6, nunca 1. Y el 0 no tiene inverso.
TEST(ModularInverseTest, NoInverseWhenADivisorIsShared)
{
    EXPECT_EQ(ModularInverse(6, 9), std::nullopt);
    EXPECT_EQ(ModularInverse(0, 7), std::nullopt);
    EXPECT_EQ(ModularInverse(14, 7), std::nullopt);
}

// Módulo 1 todo es 0, y módulo 0 no hay aritmética: no hay inverso que devolver.
TEST(ModularInverseTest, NoInverseModuloZeroOrOne)
{
    EXPECT_EQ(ModularInverse(1, 0), std::nullopt);
    EXPECT_EQ(ModularInverse(1, 1), std::nullopt);
}

// El módulo más grande que entra: 2 × 2^31 = 2^32, que es 1 módulo 2^32 - 1. Los coeficientes de
// Euclides llegan cerca de m, así que este caso prueba que el tipo donde se guardan alcanza.
TEST(ModularInverseTest, WorksWithTheLargestModulus)
{
    // El uint32 más grande, y la mitad de 2^32.
    constexpr std::uint32_t Largest = 0xFFFFFFFF;
    constexpr std::uint32_t HalfOf2To32 = 0x80000000;

    EXPECT_EQ(ModularInverse(2, Largest), HalfOf2To32);
    EXPECT_EQ(ModularInverse(HalfOf2To32, Largest), 2u);
}

// Propiedad, para un módulo de 18 bits: cada a al azar tiene inverso y (a × inverso) mod m da 1, o
// no tiene y su mcd con m no es 1. El producto va en std::uint64_t: dos números de 18 bits dan
// hasta 36. Se toma la salida del motor directamente (ver FrameCutterTest.cpp).
void CheckRandomInverses(std::uint32_t modulus, int& withInverse, int& withoutInverse)
{
    // Semilla fija y cuántos a se prueban.
    constexpr std::mt19937::result_type Seed = 20261102;
    constexpr int Samples = 2000;

    std::mt19937 engine(Seed);
    for (int k = 0; k < Samples; ++k)
    {
        const auto a = static_cast<std::uint32_t>(engine() % modulus);
        const std::optional<std::uint32_t> inverse = ModularInverse(a, modulus);
        if (inverse.has_value())
        {
            ++withInverse;
            ASSERT_LT(*inverse, modulus);
            EXPECT_EQ(std::uint64_t{a} * *inverse % modulus, 1u) << "a = " << a;
        }
        else
        {
            ++withoutInverse;
            EXPECT_NE(std::gcd(a, modulus), 1u) << "a = " << a;
        }
    }
}

// 0x3FFFB es primo (el módulo del vector de la CL2.3): todo a que no es 0 tiene inverso.
TEST(ModularInverseTest, EighteenBitPrimeModulus)
{
    // El primo de 18 bits del vector del orden de los bits (plan de la CL2, CL2.3).
    constexpr std::uint32_t PrimeModulus = 0x3FFFB;

    int withInverse = 0;
    int withoutInverse = 0;
    CheckRandomInverses(PrimeModulus, withInverse, withoutInverse);

    EXPECT_GT(withInverse, 0);
}

// 0x3FFFF = 3³ × 7 × 19 × 73: muchos a comparten un divisor, así que salen los dos casos.
TEST(ModularInverseTest, EighteenBitCompositeModulus)
{
    // El número de 18 bits más grande, que no es primo.
    constexpr std::uint32_t CompositeModulus = 0x3FFFF;

    int withInverse = 0;
    int withoutInverse = 0;
    CheckRandomInverses(CompositeModulus, withInverse, withoutInverse);

    EXPECT_GT(withInverse, 0);
    EXPECT_GT(withoutInverse, 0);
}

} // namespace
