#include "Data/Tables/TableChecksum.h"

#include "support/BytePattern.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

// The expected values are mu-client/tests/assets/test_bmd_tables.c's, taken from the lab's assets_reference.py.
namespace
{
using Client::Data::Tables::ChecksumParity;
using Client::Data::Tables::ItemChecksumKey;
using Client::Data::Tables::SkillChecksumKey;
using Client::Data::Tables::StoredChecksum;
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

// Little-endian. Bytes after it do not count.
TEST(TableChecksumTest, StoredChecksumFollowsTheRecords)
{
    std::vector<std::byte> file = BytePattern(8, 1, 0);
    file.insert(file.end(), {std::byte{0xF1}, std::byte{0xE2}, std::byte{0x00}, std::byte{0x00}, std::byte{0x99}});

    EXPECT_EQ(StoredChecksum(file, 8), 0xE2F1u);
}

TEST(TableChecksumTest, NoStoredChecksumWithoutItsFourBytes)
{
    const std::vector<std::byte> file = BytePattern(11, 1, 0);

    EXPECT_EQ(StoredChecksum(file, 8), std::nullopt);
    EXPECT_EQ(StoredChecksum(file, 12), std::nullopt);
    EXPECT_EQ(StoredChecksum({}, 0), std::nullopt);
}

TEST(TableChecksumTest, EmptyGivesTheSeed)
{
    EXPECT_EQ(TableChecksum({}, ItemChecksumKey, ChecksumParity::XorOddDwords), ItemChecksumKey << 9);
    EXPECT_EQ(TableChecksum({}, SkillChecksumKey, ChecksumParity::XorEvenDwords), SkillChecksumKey << 9);
}

TEST(TableChecksumTest, IgnoresAnIncompleteLastDword)
{
    const std::vector<std::byte> sequence = BytePattern(7, 1, 0);
    const std::span<const std::byte> bytes(sequence);

    EXPECT_EQ(TableChecksum(bytes, ItemChecksumKey, ChecksumParity::XorOddDwords),
              TableChecksum(bytes.first(4), ItemChecksumKey, ChecksumParity::XorOddDwords));
}

// With the same key, the parity changes the result: it cannot be inverted.
TEST(TableChecksumTest, ParityMatters)
{
    const std::vector<std::byte> sequence = BytePattern(32, 1, 0);

    EXPECT_NE(TableChecksum(sequence, ItemChecksumKey, ChecksumParity::XorOddDwords),
              TableChecksum(sequence, ItemChecksumKey, ChecksumParity::XorEvenDwords));
}
} // namespace
