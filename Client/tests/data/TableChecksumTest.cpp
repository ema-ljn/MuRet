// Client::Data::Tables::TableChecksum: el checksum de Item.bmd y Skill.bmd.
//
// Los valores esperados son los de mu-client/tests/assets/test_bmd_tables.c, que los sacó de
// assets_reference.py del laboratorio. El de dos dwords a mano es la actividad 12 de la guía de la
// CL1.0: queda para que lo agregues vos.

#include "Data/Tables/TableChecksum.h"

#include "support/BytePattern.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <vector>

namespace
{

using Client::Data::Tables::ChecksumParity;
using Client::Data::Tables::ItemChecksumKey;
using Client::Data::Tables::SkillChecksumKey;
using Client::Data::Tables::TableChecksum;
using Client::Tests::BytePattern;

TEST(TableChecksumTest, MatchesTheResearchVectors)
{
    const std::vector<std::byte> sequence = BytePattern(32, 1, 0);
    const std::vector<std::byte> pattern = BytePattern(64, 37, 11);

    EXPECT_EQ(TableChecksum(sequence, ItemChecksumKey, ChecksumParity::XorOddDwords), 0x033426D0u);
    EXPECT_EQ(TableChecksum(sequence, SkillChecksumKey, ChecksumParity::XorEvenDwords), 0x21ABEBAAu);
    EXPECT_EQ(TableChecksum(pattern, ItemChecksumKey, ChecksumParity::XorOddDwords), 0xE36F5FC9u);
    EXPECT_EQ(TableChecksum(pattern, SkillChecksumKey, ChecksumParity::XorEvenDwords), 0xA9B5ED92u);
}

TEST(TableChecksumTest, EmptyGivesTheSeed)
{
    EXPECT_EQ(TableChecksum({}, ItemChecksumKey, ChecksumParity::XorOddDwords), ItemChecksumKey << 9);
    EXPECT_EQ(TableChecksum({}, SkillChecksumKey, ChecksumParity::XorEvenDwords), SkillChecksumKey << 9);
}

// Los bytes que no llegan a un dword no cuentan.
TEST(TableChecksumTest, IgnoresAnIncompleteLastDword)
{
    const std::vector<std::byte> sequence = BytePattern(7, 1, 0);
    const std::span<const std::byte> bytes(sequence);

    EXPECT_EQ(TableChecksum(bytes, ItemChecksumKey, ChecksumParity::XorOddDwords),
              TableChecksum(bytes.first(4), ItemChecksumKey, ChecksumParity::XorOddDwords));
}

// Con la misma clave, la paridad cambia el resultado: no es un detalle que se pueda invertir.
TEST(TableChecksumTest, ParityMatters)
{
    const std::vector<std::byte> sequence = BytePattern(32, 1, 0);

    EXPECT_NE(TableChecksum(sequence, ItemChecksumKey, ChecksumParity::XorOddDwords),
              TableChecksum(sequence, ItemChecksumKey, ChecksumParity::XorEvenDwords));
}

} // namespace
