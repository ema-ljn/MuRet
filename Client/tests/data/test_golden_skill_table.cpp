#include "Data/Tables/SkillTable.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Skill.bmd real, el de la carpeta golden (Client/tests/support/GoldenDir.h).
//
// No hay test contra la memoria del original: en reposo, el contenedor guardia deja bytes de
// rand() donde estaban los registros (Q-C08), y el volcado s31_skill_live3.bin no es la tabla en
// claro.
namespace
{
using Client::Data::Tables::LoadSkillTable;
using Client::Data::Tables::SkillCount;
using Client::Data::Tables::SkillTableSize;
using Client::Tests::ReadFileBytes;

using GoldenSkillTable = Client::Tests::GoldenDataTest;

TEST_F(GoldenSkillTable, LoadsTheRealFile)
{
    const auto path = DataFile("Local\\Skill.bmd");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->size(), SkillTableSize + sizeof(std::uint32_t)); // 15.364 bytes

    const auto loaded = LoadSkillTable(std::as_bytes(std::span(*file)));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Count(), SkillCount);
}
} // namespace
