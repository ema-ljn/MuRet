#include "Tables/ItemSetTables.h"

#include "Data/DataError.h"
#include "Data/Tables/TableChecksum.h"
#include "support/BytePattern.h"
#include "support/ItemSetFiles.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

// ItemSetType.bmd and ItemSetOption.bmd with synthetic files (05, "Las tablas de los conjuntos"; 0x40c660 and
// 0x40c850).
namespace
{
using Client::Data::DataError;
using Client::Data::Tables::ChecksumParity;
using Client::Data::Tables::TableChecksum;
using Client::Tables::ItemSetOptionChecksumKey;
using Client::Tables::ItemSetOptionCount;
using Client::Tables::ItemSetOptionRecordSize;
using Client::Tables::ItemSetOptionTableSize;
using Client::Tables::ItemSetTypeChecksumKey;
using Client::Tables::ItemSetTypeCount;
using Client::Tables::ItemSetTypeRecordSize;
using Client::Tables::ItemSetTypeTableSize;
using Client::Tables::LoadItemSetOptionTable;
using Client::Tables::LoadItemSetTypeTable;
using Client::Tests::BytePattern;
using Client::Tests::EncryptRecords;
using Client::Tests::FirstRecordThatDiffers;
using Client::Tests::ItemSetOptionFileBytes;
using Client::Tests::ItemSetTypeFileBytes;
using Client::Tests::WithStoredChecksum;

std::vector<std::byte> PlainTypes()
{
    return BytePattern(ItemSetTypeTableSize, 41, 7);
}

std::vector<std::byte> PlainOptions()
{
    return BytePattern(ItemSetOptionTableSize, 53, 13);
}

// 0x40c77e..0x40c79a and 0x40c977..0x40c9a1: the XOR3 starts again at each record.
TEST(ItemSetTablesTest, LoadsBothTablesInTheClear)
{
    const std::vector<std::byte> types = PlainTypes();
    const std::vector<std::byte> options = PlainOptions();

    const auto loadedTypes = LoadItemSetTypeTable(ItemSetTypeFileBytes(types));
    const auto loadedOptions = LoadItemSetOptionTable(ItemSetOptionFileBytes(options));

    ASSERT_TRUE(loadedTypes.HasValue());
    EXPECT_EQ(loadedTypes.Value().Count(), ItemSetTypeCount);
    EXPECT_EQ(FirstRecordThatDiffers(loadedTypes.Value(), types), std::nullopt);
    ASSERT_TRUE(loadedOptions.HasValue());
    EXPECT_EQ(loadedOptions.Value().Count(), ItemSetOptionCount);
    EXPECT_EQ(FirstRecordThatDiffers(loadedOptions.Value(), options), std::nullopt);
}

// 0x40c6bb..0x40c6fe and 0x40c8ae..0x40c8f1: each table's key and Item.bmd's parity. With another key, the other
// parity or a changed byte, "File corrupted." and no table.
TEST(ItemSetTablesTest, EachTableChecksItsOwnKeyAndTheItemParity)
{
    const std::vector<std::byte> encryptedTypes = EncryptRecords(PlainTypes(), ItemSetTypeRecordSize);
    const std::vector<std::byte> withOptionKey = WithStoredChecksum(
        encryptedTypes, TableChecksum(encryptedTypes, ItemSetOptionChecksumKey, ChecksumParity::XorOddDwords));
    const std::vector<std::byte> withEvenParity = WithStoredChecksum(
        encryptedTypes, TableChecksum(encryptedTypes, ItemSetTypeChecksumKey, ChecksumParity::XorEvenDwords));
    std::vector<std::byte> changed = ItemSetOptionFileBytes(PlainOptions());
    changed[ItemSetOptionTableSize - 1] ^= std::byte{0x01};

    for (const auto& loaded : {LoadItemSetTypeTable(withOptionKey), LoadItemSetTypeTable(withEvenParity)})
    {
        ASSERT_FALSE(loaded.HasValue());
        EXPECT_EQ(loaded.Error(), DataError::ChecksumMismatch);
    }
    const auto changedOptions = LoadItemSetOptionTable(changed);
    ASSERT_FALSE(changedOptions.HasValue());
    EXPECT_EQ(changedOptions.Error(), DataError::ChecksumMismatch);
}

// Q-C24: without the whole checksum or without all the records. The original would read anyway and compare with
// whatever was in memory.
TEST(ItemSetTablesTest, AShortFileIsAnError)
{
    const std::vector<std::byte> types = ItemSetTypeFileBytes(PlainTypes());
    const std::vector<std::byte> options = ItemSetOptionFileBytes(PlainOptions());
    const std::span<const std::byte> wholeTypes(types);
    const std::span<const std::byte> wholeOptions(options);

    for (const auto& loaded : {LoadItemSetTypeTable(wholeTypes.first(wholeTypes.size() - 1)),
                               LoadItemSetTypeTable(wholeTypes.first(ItemSetTypeTableSize - 1)),
                               LoadItemSetTypeTable(std::span<const std::byte>{})})
    {
        ASSERT_FALSE(loaded.HasValue());
        EXPECT_EQ(loaded.Error(), DataError::FileTooShort);
    }
    const auto shortOptions = LoadItemSetOptionTable(wholeOptions.first(ItemSetOptionTableSize + 3));
    ASSERT_FALSE(shortOptions.HasValue());
    EXPECT_EQ(shortOptions.Error(), DataError::FileTooShort);
}

// The fixed-size freads do not look at what follows.
TEST(ItemSetTablesTest, ExtraBytesAreIgnored)
{
    const std::vector<std::byte> plain = PlainOptions();
    std::vector<std::byte> file = ItemSetOptionFileBytes(plain);
    file.resize(file.size() + ItemSetOptionRecordSize, std::byte{0x5a});

    const auto loaded = LoadItemSetOptionTable(file);

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(FirstRecordThatDiffers(loaded.Value(), plain), std::nullopt);
}
} // namespace
