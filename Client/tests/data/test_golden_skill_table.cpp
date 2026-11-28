#include "Tables/SkillTable.h"

#include "Tables/SkillId.h"
#include "Tables/SkillRecord.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Skill.bmd real, el de la carpeta golden (Client/tests/support/GoldenDir.h).
//
// No hay test contra la memoria del original: en reposo, el contenedor guardia deja bytes de
// rand() donde estaban los registros (Q-C08), y el volcado s31_skill_live3.bin no es la tabla en
// claro.
namespace
{
using Client::Tables::LoadSkillTable;
using Client::Tables::SkillCount;
using Client::Tables::SkillTableSize;
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

// Little-endian, as the file stores them; each one widened to 32 bits.
std::uint32_t ByteAt(std::span<const std::byte, Client::Tables::SkillRecordSize> record, std::size_t offset)
{
    return std::to_integer<std::uint32_t>(record[offset]);
}

std::uint32_t WordAt(std::span<const std::byte, Client::Tables::SkillRecordSize> record, std::size_t offset)
{
    return ByteAt(record, offset) | (ByteAt(record, offset + 1) << 8);
}

std::uint32_t DwordAt(std::span<const std::byte, Client::Tables::SkillRecordSize> record, std::size_t offset)
{
    return WordAt(record, offset) | (WordAt(record, offset + 2) << 16);
}

std::string NameOf(std::span<const std::byte, Client::Tables::SkillRecordSize> record)
{
    std::string name;
    constexpr std::size_t NameBytes = 32;
    for (const std::byte letter : record.first<NameBytes>())
    {
        if (letter == std::byte{0})
            break;
        name.push_back(static_cast<char>(letter));
    }
    return name;
}

// The rows SkillId.h names are the file's, and SkillRecord.h's offsets read their fields.
TEST_F(GoldenSkillTable, TheNamedRowsAndTheirFieldsAreTheFilesOwn)
{
    namespace Layout = Client::Tables::SkillRecordLayout;
    const auto path = DataFile("Local\\Skill.bmd");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    const auto loaded = LoadSkillTable(std::as_bytes(std::span(*file)));
    ASSERT_TRUE(loaded.HasValue());
    const auto& table = loaded.Value();

    const auto triple = table.Record(Client::Tables::TripleShotSkill);
    ASSERT_TRUE(triple.has_value());
    EXPECT_EQ(NameOf(*triple), "Triple Shot");
    EXPECT_EQ(ByteAt(*triple, Layout::Attribute), 0xffu);

    const auto nova = table.Record(Client::Tables::NovaSkill);
    ASSERT_TRUE(nova.has_value());
    EXPECT_EQ(NameOf(*nova), "Nova");
    EXPECT_EQ(ByteAt(*nova, Layout::Level), 100u);
    EXPECT_EQ(WordAt(*nova, Layout::Mana), 180u);
    EXPECT_EQ(WordAt(*nova, Layout::Stamina), 45u);
    EXPECT_EQ(WordAt(*nova, Layout::Range), 6u);
    EXPECT_EQ(ByteAt(*nova, Layout::Energy), 2u);

    const auto penetration = table.Record(Client::Tables::PenetrationSkill);
    ASSERT_TRUE(penetration.has_value());
    EXPECT_EQ(NameOf(*penetration), "Penetration");
    EXPECT_EQ(ByteAt(*penetration, Layout::Damage), 70u);

    const auto earthshake = table.Record(Client::Tables::EarthshakeSkill);
    ASSERT_TRUE(earthshake.has_value());
    EXPECT_EQ(NameOf(*earthshake), "Earthshake");
    EXPECT_EQ(ByteAt(*earthshake, Layout::Damage), 150u);
    EXPECT_EQ(DwordAt(*earthshake, Layout::Delay), 10000u);
    EXPECT_EQ(WordAt(*earthshake, Layout::Leadership), 0u);

    // The siege skills come from the guild war (+0x31 at 1), with their kill counts (+0x33).
    for (const auto& [skill, kills] :
         {std::pair{Client::Tables::FirstSiegeSkill, 4u}, std::pair{Client::Tables::LastSiegeSkill, 8u}})
    {
        const auto siege = table.Record(skill);
        ASSERT_TRUE(siege.has_value());
        EXPECT_EQ(ByteAt(*siege, Layout::Origin), 1u);
        EXPECT_EQ(ByteAt(*siege, Layout::Giver), 0u);
        EXPECT_EQ(ByteAt(*siege, Layout::KillCount), kills);
    }
}
} // namespace
