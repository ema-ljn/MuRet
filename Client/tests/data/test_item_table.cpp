#include "Data/Tables/ItemTable.h"

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

// Client::Data::Tables::LoadItemTable: Item.bmd, con archivos sintéticos.
namespace
{
using Client::Data::DataError;
using Client::Data::Tables::ChecksumParity;
using Client::Data::Tables::ItemChecksumKey;
using Client::Data::Tables::ItemCount;
using Client::Data::Tables::ItemRecordSize;
using Client::Data::Tables::ItemTableSize;
using Client::Data::Tables::LoadItemTable;
using Client::Data::Tables::TableChecksum;
using Client::Tests::BytePattern;
using Client::Tests::EncryptRecords;
using Client::Tests::FirstRecordThatDiffers;
using Client::Tests::WithStoredChecksum;

std::vector<std::byte> PlainRecords()
{
    return BytePattern(ItemTableSize, 37, 11);
}

// Los registros cifrados y, detrás, el checksum que les corresponde más `offBy`: con 0, el bueno.
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

// Q-C01: con un checksum que no coincide la tabla se carga igual, y el resultado lo dice. Uno
// que difiere solo en el bit más alto también cuenta: se comparan los 32 bits.
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

// Un registro cambiado después de calcular el checksum tampoco coincide, y se carga como está.
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

// Con todos los registros y sin los 4 bytes del checksum, se carga igual: el original tampoco lo
// mira (Q-C01).
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

// Q-C24: sin todos los registros es un error y no hay tabla.
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

// Los bytes de más se ignoran, como los que trae la item.bmd del laboratorio después del checksum.
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
