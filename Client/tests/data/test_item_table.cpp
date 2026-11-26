#include "Tables/ItemTable.h"

#include "Data/DataError.h"
#include "Data/Tables/TableChecksum.h"
#include "support/BytePattern.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
using Client::Data::DataError;
using Client::Data::Tables::ChecksumParity;
using Client::Data::Tables::ItemChecksumKey;
using Client::Data::Tables::TableChecksum;
using Client::Tables::ItemCount;
using Client::Tables::ItemRecordSize;
using Client::Tables::ItemTableSize;
using Client::Tables::LoadItemTable;
using Client::Tests::BytePattern;
using Client::Tests::EncryptRecords;
using Client::Tests::FirstRecordThatDiffers;
using Client::Tests::WithStoredChecksum;

std::vector<std::byte> PlainRecords()
{
    return BytePattern(ItemTableSize, 37, 11);
}

// The encrypted records and, after them, their checksum plus `offBy`: with 0, the right one.
std::vector<std::byte> ItemFile(const std::vector<std::byte>& plain, std::uint32_t offBy = 0)
{
    const std::vector<std::byte> encrypted = EncryptRecords(plain, ItemRecordSize);
    const std::uint32_t checksum = TableChecksum(encrypted, ItemChecksumKey, ChecksumParity::XorOddDwords);
    return WithStoredChecksum(encrypted, checksum + offBy);
}

TEST(ItemTableTest, LoadsTheRecordsAndChecksTheChecksum)
{
    const std::vector<std::byte> plain = PlainRecords();

    const auto loaded = LoadItemTable(ItemFile(plain));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_TRUE(loaded.Value().checksumMatches);
    EXPECT_EQ(loaded.Value().table.Count(), ItemCount);
    EXPECT_EQ(FirstRecordThatDiffers(loaded.Value().table, plain), std::nullopt);
}

// Q-C01: with a checksum that does not match the table loads anyway, and the result says so. One that differs only in
// the top bit also counts: all 32 bits are compared.
TEST(ItemTableTest, AWrongChecksumStillLoads)
{
    const std::vector<std::byte> plain = PlainRecords();

    for (const std::uint32_t offBy : {std::uint32_t{1}, std::uint32_t{0x80000000}})
    {
        const auto loaded = LoadItemTable(ItemFile(plain, offBy));

        ASSERT_TRUE(loaded.HasValue()) << "con el checksum corrido en " << offBy;
        EXPECT_FALSE(loaded.Value().checksumMatches) << "con el checksum corrido en " << offBy;
        EXPECT_EQ(loaded.Value().table.Count(), ItemCount);
        EXPECT_EQ(FirstRecordThatDiffers(loaded.Value().table, plain), std::nullopt);
    }
}

TEST(ItemTableTest, AChangedRecordDoesNotMatchButLoads)
{
    std::vector<std::byte> plain = PlainRecords();
    std::vector<std::byte> file = ItemFile(plain);
    file[ItemTableSize - 1] ^= std::byte{0x01};
    plain[ItemTableSize - 1] ^= std::byte{0x01};

    const auto loaded = LoadItemTable(file);

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_FALSE(loaded.Value().checksumMatches);
    EXPECT_EQ(FirstRecordThatDiffers(loaded.Value().table, plain), std::nullopt);
}

// The original does not look at it either (Q-C01).
TEST(ItemTableTest, WithoutItsChecksumStillLoads)
{
    const std::vector<std::byte> plain = PlainRecords();
    const std::vector<std::byte> file = ItemFile(plain);
    const std::span<const std::byte> whole(file);

    for (const std::size_t size : {ItemTableSize, ItemTableSize + sizeof(std::uint32_t) - 1})
    {
        const auto loaded = LoadItemTable(whole.first(size));

        ASSERT_TRUE(loaded.HasValue()) << "con " << size << " bytes";
        EXPECT_FALSE(loaded.Value().checksumMatches) << "con " << size << " bytes";
        EXPECT_EQ(FirstRecordThatDiffers(loaded.Value().table, plain), std::nullopt);
    }
}

// Q-C24: an error and no table.
TEST(ItemTableTest, ShortFileIsAnError)
{
    const std::vector<std::byte> file = ItemFile(PlainRecords());
    const std::span<const std::byte> whole(file);

    const auto oneByteShort = LoadItemTable(whole.first(ItemTableSize - 1));
    const auto empty = LoadItemTable(std::span<const std::byte>{});

    ASSERT_FALSE(oneByteShort.HasValue());
    EXPECT_EQ(oneByteShort.Error(), DataError::FileTooShort);
    ASSERT_FALSE(empty.HasValue());
    EXPECT_EQ(empty.Error(), DataError::FileTooShort);
}

// Like the ones the lab's item.bmd carries after the checksum.
TEST(ItemTableTest, ExtraBytesAreIgnored)
{
    constexpr std::size_t LabExtraBytes = 0x50;
    constexpr std::byte Filler{0x5A};
    const std::vector<std::byte> plain = PlainRecords();
    std::vector<std::byte> file = ItemFile(plain);
    file.resize(file.size() + LabExtraBytes, Filler);

    const auto loaded = LoadItemTable(file);

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_TRUE(loaded.Value().checksumMatches);
    EXPECT_EQ(loaded.Value().table.Count(), ItemCount);
}
} // namespace
