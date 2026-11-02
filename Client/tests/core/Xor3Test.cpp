// Client::Core::Xor3: el cifrado de las tablas .bmd y de la cuenta y la clave del login.

#include "Core/Xor3.h"

#include "support/BytePattern.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <vector>

namespace
{

using Client::Core::Xor3;
using Client::Tests::BytePattern;

// Cualquier contenido sirve; este es el "patrón" de mu-client.
std::vector<std::byte> Pattern(std::size_t size)
{
    return BytePattern(size, 37, 11);
}

// Sobre ceros queda la clave misma, repetida: FC CF AB FC. Por eso un registro vacío de Text.bmd
// se ve así en el archivo.
TEST(Xor3Test, ZerosBecomeTheKey)
{
    std::vector<std::byte> bytes(4);

    Xor3(bytes);

    EXPECT_EQ(bytes, (std::vector{std::byte{0xFC}, std::byte{0xCF}, std::byte{0xAB}, std::byte{0xFC}}));
}

// El mismo vector que mu-client/tests/assets/test_bmd_tables.c, con los bytes 00..08.
TEST(Xor3Test, MatchesTheResearchVector)
{
    std::vector<std::byte> bytes = BytePattern(9, 1, 0);

    Xor3(bytes);

    const std::vector<std::byte> expected = {std::byte{0xFC}, std::byte{0xCE}, std::byte{0xA9},
                                             std::byte{0xFF}, std::byte{0xCB}, std::byte{0xAE},
                                             std::byte{0xFA}, std::byte{0xC8}, std::byte{0xA3}};
    EXPECT_EQ(bytes, expected);
}

TEST(Xor3Test, TwiceGivesBackTheOriginal)
{
    const std::vector<std::byte> original = Pattern(64);
    std::vector<std::byte> bytes = original;

    Xor3(bytes);
    EXPECT_NE(bytes, original);
    Xor3(bytes);

    EXPECT_EQ(bytes, original);
}

// La posición en la clave se cuenta desde el principio de lo que se pasa. Por registros de 300
// bytes da lo mismo que de una vez, porque 300 es múltiplo de 3; cortando en 301, no.
TEST(Xor3Test, ByRecordsEqualsAllAtOnceWhenRecordsAreMultiplesOfThree)
{
    std::vector<std::byte> wholeFile = Pattern(600);
    std::vector<std::byte> byRecords = wholeFile;
    std::vector<std::byte> badSplit = wholeFile;
    const std::span<std::byte> records(byRecords);
    const std::span<std::byte> split(badSplit);

    Xor3(wholeFile);
    Xor3(records.first(300));
    Xor3(records.subspan(300));
    Xor3(split.first(301));
    Xor3(split.subspan(301));

    EXPECT_EQ(byRecords, wholeFile);
    EXPECT_NE(badSplit, wholeFile);
}

} // namespace
