// Client::Data::Terrain::DecryptK16: el cifrado de los archivos de terreno.

#include "Data/Terrain/K16.h"

#include "support/BytePattern.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace
{

using Client::Data::Terrain::DecryptK16;
using Client::Tests::BytePattern;
using Client::Tests::EncryptK16;

// A mano, con dos bytes cifrados en cero:
//   plano[0] = (D1 ^ 00) - 5E = 73, y el previo pasa a ser 00 + 3D = 3D;
//   plano[1] = (73 ^ 00) - 3D = 36.
// Si el previo saliera del byte en claro (73 + 3D = B0), el segundo daría C3.
TEST(K16Test, TwoBytesByHand)
{
    std::vector<std::byte> bytes(2);

    DecryptK16(bytes);

    EXPECT_EQ(bytes, (std::vector{std::byte{0x73}, std::byte{0x36}}));
}

// La clave vuelve a empezar en el byte 16, pero la semilla no: sobre ceros, el byte 16 es
// D1 - 3D = 94, no el 73 del byte 0.
TEST(K16Test, TheKeyRepeatsEvery16Bytes)
{
    std::vector<std::byte> bytes(17);

    DecryptK16(bytes);

    EXPECT_EQ(bytes[0], std::byte{0x73});
    EXPECT_EQ(bytes[16], std::byte{0x94});
}

// La cabecera cifrada del EncTerrain1.att real, 8B E5 24 92 (005f8390-encterrain-att.md, en el
// repo de research). K16 la deja en FC CE 54 03, que con el XOR3 del .att da 00 01 FF FF.
TEST(K16Test, MatchesTheResearchVector)
{
    std::vector<std::byte> bytes = {std::byte{0x8B}, std::byte{0xE5}, std::byte{0x24}, std::byte{0x92}};

    DecryptK16(bytes);

    EXPECT_EQ(bytes, (std::vector{std::byte{0xFC}, std::byte{0xCE}, std::byte{0x54}, std::byte{0x03}}));
}

TEST(K16Test, GivesBackWhatTheExporterEncrypted)
{
    const std::vector<std::byte> plain = BytePattern(300, 37, 11);
    std::vector<std::byte> bytes = EncryptK16(plain);
    ASSERT_NE(bytes, plain);

    DecryptK16(bytes);

    EXPECT_EQ(bytes, plain);
}

// La segunda mitad descifrada sola, con la semilla del principio, sale mal en su primer byte y
// bien en el resto: desde el segundo, `previo` sale de un byte cifrado que está en el archivo. Y
// como la mitad empieza en un múltiplo de 16, la clave coincide. Si `previo` saliera del byte en
// claro, el error del primero se arrastraría a todos.
TEST(K16Test, ASecondHalfAloneIsWrongOnlyInItsFirstByte)
{
    const std::vector<std::byte> plain = BytePattern(32, 37, 11);
    const std::vector<std::byte> encrypted = EncryptK16(plain);
    std::vector<std::byte> secondHalf(encrypted.begin() + 16, encrypted.end());

    DecryptK16(secondHalf);

    EXPECT_NE(secondHalf[0], plain[16]);
    EXPECT_EQ(std::vector(secondHalf.begin() + 1, secondHalf.end()), std::vector(plain.begin() + 17, plain.end()));
}

} // namespace
