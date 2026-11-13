#include "Data/Tables/MonsterSkillTable.h"

#include "Core/Xor3.h"
#include "Data/DataError.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Client::Data::Tables::LoadMonsterSkillTable: MonsterSkill.bmd (0x54a440), la cuenta en claro y registros de 0x1c con
// el XOR3 de 0x5496c0 desde el primer byte de cada uno. Los registros son inventados.
namespace
{
using Client::Data::DataError;
using Client::Data::Tables::LoadMonsterSkillTable;
using Client::Data::Tables::MonsterSkillRecordSize;
using Client::Data::Tables::MonsterSkills;

void Put(std::vector<std::byte>& bytes, std::int32_t value)
{
    const auto bits = static_cast<std::uint32_t>(value);
    for (std::size_t i = 0; i < 4; ++i)
        bytes.push_back(static_cast<std::byte>((bits >> (8 * i)) & 0xffu));
}

// Un registro cifrado: el tipo, las cinco skills y el sexto int32.
void PutRecord(std::vector<std::byte>& file, std::int32_t kind, const std::array<std::int32_t, 5>& skills,
               std::int32_t last)
{
    std::vector<std::byte> record;
    Put(record, kind);
    for (const std::int32_t skill : skills)
        Put(record, skill);
    Put(record, last);
    Client::Core::Xor3(record);
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

// 0x54a44f..0x54a461 pone en −1 solo el tipo 0; los demás que el archivo no trae quedan en la .bss, en 0. Un registro
// que se repite pisa al de antes.
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

// 0x54a49b..0x54a4a0: la cuenta con signo; en 0 o negativa, ningún registro.
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
