#include "Render/Models/LogicalAccess.h"

#include "Render/Models/Cloth.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

// Client::Render::Models::LogicalAccess: un acceso a una entrada de la tabla de protección del
// original (06 § La tabla de protección, Q-C125 y Q-C183). Los rand() que se esperan son los de la
// lectura estática del research: 4 al cerrar una variable de 4 bytes (0x412890, y en línea en
// 0x40b570 y 0x40b800) y 3.848 al cerrar el registro propio (0x40b4d0, uno por cada byte de los 0xf08). La
// entrada lleva en un byte cuántos accesos la tienen abierta, y gasta el cierre que la deja en 0.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Render::Models::LogicalAccess;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Render::Models::SmallLogicalCloseRandoms;

constexpr std::uint32_t Seed = 0x2f1d;
// Lo que gasta cada cierre según el research, escrito aparte de las constantes que se prueban.
constexpr int SmallVariableRandoms = 4;
constexpr int OwnRecordRandoms = 3848;

// Cada clase de cierre: la constante que recibe el acceso y lo que fija el research.
struct CloseCost
{
    std::uint16_t closeRandoms;
    int researchRandoms;
};
constexpr std::array CloseCosts{CloseCost{SmallLogicalCloseRandoms, SmallVariableRandoms},
                                CloseCost{OwnRecordCloseRandoms, OwnRecordRandoms}};

// El estado de la CRT después de `count` rand() desde Seed.
std::uint32_t StateAfter(int count)
{
    CrtRandom random(Seed);
    for (int i = 0; i < count; ++i)
    {
        static_cast<void>(random.Next());
    }
    return random.State();
}

TEST(LogicalAccessTest, TheLastCloseSpendsItsRandoms)
{
    EXPECT_EQ(SmallLogicalCloseRandoms, SmallVariableRandoms);
    EXPECT_EQ(OwnRecordCloseRandoms, OwnRecordRandoms);
    for (const CloseCost& cost : CloseCosts)
    {
        SCOPED_TRACE(cost.researchRandoms);
        std::uint8_t depth = 0;
        CrtRandom random(Seed);
        {
            const LogicalAccess access(depth, random, cost.closeRandoms);
            // Abrir no gasta: el research cuenta por cada abrir y cerrar solo el cierre.
            EXPECT_EQ(depth, 1);
            EXPECT_EQ(random.State(), Seed);
        }
        EXPECT_EQ(depth, 0);
        EXPECT_EQ(random.State(), StateAfter(cost.researchRandoms));
    }
}

// Un cierre que no deja la cuenta en 0 no vuelve a cifrar: el registro propio adentro de otro acceso
// suyo, y una variable que otro ya tenía abierta (la tela con el contador abierto, Q-C125).
TEST(LogicalAccessTest, NestedClosesSpendNothing)
{
    std::uint8_t recordDepth = 0;
    CrtRandom random(Seed);
    {
        const LogicalAccess outer(recordDepth, random, OwnRecordCloseRandoms);
        {
            const LogicalAccess inner(recordDepth, random, OwnRecordCloseRandoms);
            EXPECT_EQ(recordDepth, 2);
        }
        EXPECT_EQ(recordDepth, 1);
        EXPECT_EQ(random.State(), Seed);
    }
    EXPECT_EQ(recordDepth, 0);
    EXPECT_EQ(random.State(), StateAfter(OwnRecordRandoms));

    std::uint8_t alreadyOpen = 1;
    CrtRandom untouched(Seed);
    {
        const LogicalAccess access(alreadyOpen, untouched, SmallLogicalCloseRandoms);
        EXPECT_EQ(alreadyOpen, 2);
    }
    EXPECT_EQ(alreadyOpen, 1);
    EXPECT_EQ(untouched.State(), Seed);
}

// La cuenta es un byte: con 255, abrir la deja en 0 y cerrar la vuelve a 255 sin gastar. Gasta el
// cierre que la deja en 0, aunque no sea el de más afuera.
TEST(LogicalAccessTest, TheDepthByteWraps)
{
    std::uint8_t depth = 255;
    CrtRandom random(Seed);
    {
        const LogicalAccess outer(depth, random, SmallLogicalCloseRandoms);
        EXPECT_EQ(depth, 0);
        {
            const LogicalAccess inner(depth, random, SmallLogicalCloseRandoms);
            EXPECT_EQ(depth, 1);
            EXPECT_EQ(random.State(), Seed);
        }
        EXPECT_EQ(depth, 0);
        EXPECT_EQ(random.State(), StateAfter(SmallVariableRandoms));
    }
    EXPECT_EQ(depth, 255);
    EXPECT_EQ(random.State(), StateAfter(SmallVariableRandoms));
}

// 0x4128e2..0x4128f9 (CL9 hito 2): el cierre que deja la cuenta en 0 pone en cada byte de la variable el rand() & 0xff
// de su vuelta, el primero abajo; uno que no la deja en 0 la deja en claro.
TEST(LogicalAccessTest, TheLastCloseLeavesItsRandomBytes)
{
    std::uint8_t depth = 0;
    std::uint32_t left = 0x5a;
    CrtRandom random(Seed);
    {
        const LogicalAccess access(depth, random, SmallLogicalCloseRandoms, left);
        EXPECT_EQ(left, 0x5au);
    }
    CrtRandom expected(Seed);
    std::uint32_t bytes = 0;
    for (std::uint32_t index = 0; index < 4; ++index)
        bytes |= (static_cast<std::uint32_t>(expected.Next()) & 0xffu) << (8u * index);
    EXPECT_EQ(left, bytes);
    EXPECT_EQ(random.State(), expected.State());

    std::uint8_t alreadyOpen = 1;
    std::uint32_t plain = 0x3e;
    {
        const LogicalAccess access(alreadyOpen, random, SmallLogicalCloseRandoms, plain);
    }
    EXPECT_EQ(plain, 0x3eu);
    EXPECT_EQ(random.State(), expected.State());
}
} // namespace
