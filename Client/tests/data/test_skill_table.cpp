#include "Data/Tables/SkillTable.h"

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

// Client::Data::Tables::LoadSkillTable: Skill.bmd, con archivos sintéticos.
namespace
{
using Client::Data::DataError;
using Client::Data::Tables::ChecksumParity;
using Client::Data::Tables::LoadSkillTable;
using Client::Data::Tables::SkillChecksumKey;
using Client::Data::Tables::SkillCount;
using Client::Data::Tables::SkillRecordSize;
using Client::Data::Tables::SkillTableSize;
using Client::Data::Tables::TableChecksum;
using Client::Tests::BytePattern;
using Client::Tests::EncryptRecords;
using Client::Tests::FirstRecordThatDiffers;
using Client::Tests::WithStoredChecksum;

std::vector<std::byte> PlainRecords()
{
    return BytePattern(SkillTableSize, 37, 11);
}

// Los registros cifrados y, detrás, el checksum que les corresponde más `offBy`: con 0, el bueno.
std::vector<std::byte> SkillFile(const std::vector<std::byte>& plain, std::uint32_t offBy = 0)
{
    const std::vector<std::byte> encrypted = EncryptRecords(plain, SkillRecordSize);
    const std::uint32_t checksum = TableChecksum(encrypted, SkillChecksumKey, ChecksumParity::XorEvenDwords);
    return WithStoredChecksum(encrypted, checksum + offBy);
}

TEST(SkillTableTest, LoadsTheRecordsInTheClear)
{
    const std::vector<std::byte> plain = PlainRecords();

    const auto loaded = LoadSkillTable(SkillFile(plain));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Count(), SkillCount);
    EXPECT_EQ(FirstRecordThatDiffers(loaded.Value(), plain), std::nullopt);
}

// Con un checksum que no coincide no hay tabla, como en el original ("File corrupted.").
TEST(SkillTableTest, AWrongChecksumIsAnError)
{
    const auto loaded = LoadSkillTable(SkillFile(PlainRecords(), 1));

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(loaded.Error(), DataError::ChecksumMismatch);
}

// Un registro cambiado después de calcular el checksum también es un error.
TEST(SkillTableTest, AChangedRecordIsAnError)
{
    std::vector<std::byte> file = SkillFile(PlainRecords());
    file[SkillTableSize - 1] ^= std::byte{0x01};

    const auto loaded = LoadSkillTable(file);

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(loaded.Error(), DataError::ChecksumMismatch);
}

// Q-C24: sin los 4 bytes del checksum, o sin todos los registros, es un error y no hay tabla. El
// original compararía con un checksum que no leyó, y casi siempre terminaría en "File corrupted.".
TEST(SkillTableTest, ShortFileIsAnError)
{
    const std::vector<std::byte> file = SkillFile(PlainRecords());
    const std::span<const std::byte> whole(file);

    const auto withoutAllOfItsChecksum = LoadSkillTable(whole.first(whole.size() - 1));
    const auto withoutAllItsRecords = LoadSkillTable(whole.first(SkillTableSize - 1));

    ASSERT_FALSE(withoutAllOfItsChecksum.HasValue());
    EXPECT_EQ(withoutAllOfItsChecksum.Error(), DataError::FileTooShort);
    ASSERT_FALSE(withoutAllItsRecords.HasValue());
    EXPECT_EQ(withoutAllItsRecords.Error(), DataError::FileTooShort);
}

TEST(SkillTableTest, ExtraBytesAreIgnored)
{
    const std::vector<std::byte> plain = PlainRecords();
    constexpr std::byte Filler{0x5A};
    std::vector<std::byte> file = SkillFile(plain);
    file.resize(file.size() + SkillRecordSize, Filler);

    const auto loaded = LoadSkillTable(file);

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Count(), SkillCount);
    EXPECT_EQ(FirstRecordThatDiffers(loaded.Value(), plain), std::nullopt);
}
} // namespace
