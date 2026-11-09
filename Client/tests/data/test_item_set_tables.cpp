#include "Data/Tables/ItemSetTables.h"

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

// Client::Data::Tables::LoadItemSetTypeTable y LoadItemSetOptionTable: ItemSetType.bmd e
// ItemSetOption.bmd con archivos sintéticos (05, "Las tablas de los conjuntos"; 0x40c660 y 0x40c850).
namespace
{
using Client::Data::DataError;
using Client::Data::Tables::ChecksumParity;
using Client::Data::Tables::ItemSetOptionChecksumKey;
using Client::Data::Tables::ItemSetOptionCount;
using Client::Data::Tables::ItemSetOptionRecordSize;
using Client::Data::Tables::ItemSetOptionTableSize;
using Client::Data::Tables::ItemSetTypeChecksumKey;
using Client::Data::Tables::ItemSetTypeCount;
using Client::Data::Tables::ItemSetTypeRecordSize;
using Client::Data::Tables::ItemSetTypeTableSize;
using Client::Data::Tables::LoadItemSetOptionTable;
using Client::Data::Tables::LoadItemSetTypeTable;
using Client::Data::Tables::TableChecksum;
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

// 0x40c77e..0x40c79a y 0x40c977..0x40c9a1: el XOR3 vuelve a empezar en cada registro.
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

// 0x40c6bb..0x40c6fe y 0x40c8ae..0x40c8f1: la clave de cada tabla y la paridad de Item.bmd. Con otra
// clave, la otra paridad o un byte cambiado, "File corrupted." y no hay tabla.
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

// Q-C24: sin todo el checksum o sin todos los registros, error y no hay tabla. El original leería igual
// y compararía con lo que hubiera en la memoria.
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

// Los fread de tamaño fijo no miran lo que sigue.
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
