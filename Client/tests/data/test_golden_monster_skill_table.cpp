#include "Data/Tables/MonsterSkillTable.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// MonsterSkill.bmd real (Data\Local, 0x54a440 desde 0x61ccb0): los ocho tipos que miran 0x429270 y 0x437580.
namespace
{
using Client::Data::Tables::LoadMonsterSkillTable;
using Client::Tests::ReadFileBytes;
using GoldenMonsterSkillTable = Client::Tests::GoldenDataTest;
using Skills = std::array<std::int32_t, 5>;

TEST_F(GoldenMonsterSkillTable, TheRealFileHasTheEightSkillMonsters)
{
    const auto path = DataFile("Local\\MonsterSkill.bmd");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->size(), 228u);
    const auto loaded = LoadMonsterSkillTable(std::as_bytes(std::span(*file)));
    ASSERT_TRUE(loaded.HasValue());
    const auto& table = loaded.Value();

    EXPECT_EQ(table.records.size(), 8u);
    EXPECT_EQ(table.Of(0x130).skills, (Skills{4, 1, -1, -1, -1}));
    EXPECT_EQ(table.Of(0x131).skills, (Skills{5, -1, -1, -1, -1}));
    EXPECT_EQ(table.Of(0x135).skills, (Skills{2, 3, -1, -1, -1}));
    EXPECT_EQ(table.Of(0x154).skills, (Skills{6, 7, 8, -1, -1}));
    EXPECT_EQ(table.Of(0x155).skills, (Skills{11, -1, -1, -1, -1}));
    EXPECT_EQ(table.Of(0x158).skills, (Skills{9, -1, -1, -1, -1}));
    EXPECT_EQ(table.Of(0x159).skills, (Skills{10, -1, -1, -1, -1}));
    EXPECT_EQ(table.Of(0x15d).skills, (Skills{12, 13, -1, -1, -1}));
    for (const auto& [kind, skills] : table.records)
        EXPECT_EQ(skills.dword14, 0) << kind;
}
} // namespace
