#include "Tables/MonsterSkillTable.h"

#include "Crypto/Xor3.h"
#include "Data/DataError.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// MonsterSkill.bmd (0x54a440): the count in clear and 0x1c records with 0x5496c0's XOR3 from each one's first byte.
// The records are made up.
namespace
{
using Client::Data::DataError;
using Client::Tables::LoadMonsterSkillTable;
using Client::Tables::MonsterSkillRecordSize;
using Client::Tables::MonsterSkills;

void Put(std::vector<std::byte>& bytes, std::int32_t value)
{
    const auto bits = static_cast<std::uint32_t>(value);
    for (std::size_t i = 0; i < 4; ++i)
        bytes.push_back(static_cast<std::byte>((bits >> (8 * i)) & 0xffu));
}

// An encrypted record: the type, the five skills and the sixth int32.
void PutRecord(std::vector<std::byte>& file, std::int32_t kind, const std::array<std::int32_t, 5>& skills,
               std::int32_t last)
{
    std::vector<std::byte> record;
    Put(record, kind);
    for (const std::int32_t skill : skills)
        Put(record, skill);
    Put(record, last);
    Client::Crypto::Xor3(record);
    file.insert(file.end(), record.begin(), record.end());
}

TEST(MonsterSkillTable, ReadsTheRecordsByTheirKind)
{
    std::vector<std::byte> file;
    Put(file, 2);
    PutRecord(file, 0x130, {4, 1, -1, -1, -1}, 0);
    PutRecord(file, 0x15d, {12, 13, -1, -1, -1}, 7);
    file.push_back(std::byte{0x55});

    const auto loaded = LoadMonsterSkillTable(file);
    ASSERT_TRUE(loaded.HasValue());
    const auto& table = loaded.Value();
    EXPECT_EQ(table.records.size(), 2u);
    EXPECT_EQ(table.Of(0x130).skills, (std::array<std::int32_t, 5>{4, 1, -1, -1, -1}));
    EXPECT_EQ(table.Of(0x15d).skills, (std::array<std::int32_t, 5>{12, 13, -1, -1, -1}));
    EXPECT_EQ(table.Of(0x15d).dword14, 7);
}

// 0x54a44f..0x54a461 sets only type 0 to −1; the others the file lacks stay in the .bss, at 0. A repeated record
// overwrites the earlier one.
TEST(MonsterSkillTable, TheMissingKindsKeepTheBssAndTheLastRecordWins)
{
    std::vector<std::byte> file;
    Put(file, 2);
    PutRecord(file, 0x131, {5, -1, -1, -1, -1}, 0);
    PutRecord(file, 0x131, {6, 7, 8, 9, 10}, 11);

    const auto loaded = LoadMonsterSkillTable(file);
    ASSERT_TRUE(loaded.HasValue());
    const auto& table = loaded.Value();
    EXPECT_EQ(table.Of(0x131).skills, (std::array<std::int32_t, 5>{6, 7, 8, 9, 10}));
    EXPECT_EQ(table.Of(0x131).dword14, 11);
    EXPECT_EQ(table.Of(0x132).skills, (std::array<std::int32_t, 5>{}));
    EXPECT_EQ(table.Of(0).skills, (std::array<std::int32_t, 5>{-1, -1, -1, -1, -1}));
    EXPECT_EQ(table.Of(0).dword14, -1);
}

// 0x54a49b..0x54a4a0: the count is signed; at 0 or negative, no record.
TEST(MonsterSkillTable, ANegativeCountReadsNothing)
{
    std::vector<std::byte> file;
    Put(file, -3);
    const auto loaded = LoadMonsterSkillTable(file);
    ASSERT_TRUE(loaded.HasValue());
    EXPECT_TRUE(loaded.Value().records.empty());
}

TEST(MonsterSkillTable, AShortFileOrAKindOutsideTheTableIsAnError)
{
    std::vector<std::byte> empty;
    EXPECT_EQ(LoadMonsterSkillTable(empty).Error(), DataError::FileTooShort);

    std::vector<std::byte> shortFile;
    Put(shortFile, 2);
    PutRecord(shortFile, 0x130, {4, 1, -1, -1, -1}, 0);
    shortFile.resize(shortFile.size() + MonsterSkillRecordSize - 1);
    EXPECT_EQ(LoadMonsterSkillTable(shortFile).Error(), DataError::FileTooShort);

    for (const std::int32_t kind : {-1, 0x10000})
    {
        std::vector<std::byte> outside;
        Put(outside, 1);
        PutRecord(outside, kind, {1, 2, 3, 4, 5}, 0);
        EXPECT_EQ(LoadMonsterSkillTable(outside).Error(), DataError::InvalidData) << kind;
    }
}
} // namespace
