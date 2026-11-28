#include "Tables/GameData.h"

#include "Crypto/Xor3.h"
#include "Data/DataError.h"
#include "Data/DataFile.h"
#include "Tables/GateRecord.h"
#include "Tables/GateTable.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "support/ItemFiles.h"
#include "support/ItemSetFiles.h"
#include "Tables/FilterWords.h"
#include "Tables/MoveRequirements.h"
#include "Monsters/NpcNames.h"
#include "Tables/TextTable.h"
#include "support/SceneDataFiles.h"
#include "support/GateFiles.h"
#include "support/TemporaryFolder.h"
#include "support/SkillFiles.h"
#include "support/GoldenDir.h"
#include "Tables/SkillTable.h"
#include "UI/MacroFile.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// What main.cpp reads before the first scene, as WinMain reads Text.bmd at 0x4b9b9f (0x61ee30 and 0x5495e0, with
// "Data\Local\Text.bmd" from 0x6d4370), before the fonts and scene 1. With synthetic files: the real texts do not go
// into the tests.
namespace
{
using Client::Tables::LoadGameData;
using Client::Tests::GateTableBytes;
using Client::Tests::TemporaryFolder;
using Client::Tests::WriteFilterFile;
using Client::Tests::WriteSceneFile;
using Client::Tests::WriteTextTable;

TEST(GameDataTest, ReadsTheTextTable)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {{0, "cara de prueba"}, {456, "versión %s"}});
    WriteSceneFile(folder.Path(), "Gate.bmd", GateTableBytes({}));
    WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
    Client::Tests::WriteEmptyItemSetFiles(folder.Path());
    Client::Tests::WriteEmptySkillFile(folder.Path());

    const auto data = LoadGameData(folder.Path());

    ASSERT_TRUE(data.HasValue()) << data.Error();
    ASSERT_TRUE(data.Value().text.has_value());
    EXPECT_EQ(data.Value().text->Text(0), "cara de prueba");
    EXPECT_EQ(data.Value().text->Text(456), "versión %s");
}

// As on Windows, case-insensitive: "local\TEXT.BMD" is also the file.
TEST(GameDataTest, FindsTheFileInAnyCase)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {{1, "uno"}}, "local/TEXT.BMD");
    WriteSceneFile(folder.Path(), "Gate.bmd", GateTableBytes({}));
    WriteSceneFile(folder.Path(), "local/Item.bmd", Client::Tests::EmptyItemTableBytes());
    Client::Tests::WriteEmptyItemSetFiles(folder.Path(), "local");
    Client::Tests::WriteEmptySkillFile(folder.Path(), "local");

    const auto data = LoadGameData(folder.Path());

    ASSERT_TRUE(data.HasValue()) << data.Error();
    EXPECT_EQ(data.Value().text->Text(1), "uno");
}

TEST(GameDataTest, AMissingTextTableIsNamed)
{
    const TemporaryFolder folder("game-data");

    const auto data = LoadGameData(folder.Path());

    ASSERT_FALSE(data.HasValue());
    EXPECT_NE(data.Error().find("Local\\Text.bmd"), std::string::npos) << data.Error();
}

TEST(GameDataTest, AShortTextTableIsAnError)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {}, "Local/Text.bmd", 100);

    const auto data = LoadGameData(folder.Path());

    ASSERT_FALSE(data.HasValue());
    EXPECT_NE(data.Error().find("Local\\Text.bmd"), std::string::npos) << data.Error();
    EXPECT_NE(data.Error().find("más corto"), std::string::npos) << data.Error();
}
// A file over 1 MiB is not a Text.bmd (the table is 0x96000): it is not read whole.
TEST(GameDataTest, AFileFarLargerThanTheTableIsNotRead)
{
    const TemporaryFolder folder("game-data");
    constexpr std::size_t OverTheCap = 1024 * 1024 + 1;
    WriteTextTable(folder.Path(), {}, "Local/Text.bmd", OverTheCap);

    const auto data = LoadGameData(folder.Path());

    ASSERT_FALSE(data.HasValue());
    EXPECT_NE(data.Error().find("Local\\Text.bmd"), std::string::npos) << data.Error();
    EXPECT_NE(data.Error().find(std::make_error_code(std::errc::file_too_large).message()), std::string::npos)
        << data.Error();
}
// 0x61ccb0 also reads Filter.bmd and FilterName.bmd (Data\Local): the words a new character's name cannot have.
TEST(GameDataTest, ReadsTheTwoFilters)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {{1, "uno"}});
    WriteSceneFile(folder.Path(), "Gate.bmd", GateTableBytes({}));
    WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
    Client::Tests::WriteEmptyItemSetFiles(folder.Path());
    Client::Tests::WriteEmptySkillFile(folder.Path());
    WriteFilterFile(folder.Path(), {"Synth", "Other"}, Client::Tables::FilterNameChecksumKey, "Local/FilterName.bmd");
    WriteFilterFile(folder.Path(), {"word"}, Client::Tables::FilterChecksumKey, "Local/filter.BMD");

    const auto data = LoadGameData(folder.Path());

    ASSERT_TRUE(data.HasValue()) << data.Error();
    EXPECT_EQ(data.Value().filterName, (std::vector<std::string>{"Synth", "Other"}));
    EXPECT_EQ(data.Value().filter, (std::vector<std::string>{"word"}));
}

// Without both, nothing to filter: the original does not start, and MuRet goes on without them.
TEST(GameDataTest, WithoutTheFiltersNothingIsFiltered)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {{1, "uno"}});
    WriteSceneFile(folder.Path(), "Gate.bmd", GateTableBytes({}));
    WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
    Client::Tests::WriteEmptyItemSetFiles(folder.Path());
    Client::Tests::WriteEmptySkillFile(folder.Path());

    const auto data = LoadGameData(folder.Path());

    ASSERT_TRUE(data.HasValue()) << data.Error();
    EXPECT_TRUE(data.Value().filterName.empty());
    EXPECT_TRUE(data.Value().filter.empty());
}

// 0x61ee30 → 0x61c1c0: Macro.txt, after Text.bmd, case-insensitive; without it, empty macros. The macros are made
// up.
TEST(GameDataTest, ReadsTheMacros)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {{1, "uno"}});
    WriteSceneFile(folder.Path(), "Gate.bmd", GateTableBytes({}));
    WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
    Client::Tests::WriteEmptyItemSetFiles(folder.Path());
    Client::Tests::WriteEmptySkillFile(folder.Path());

    const auto without = LoadGameData(folder.Path());
    ASSERT_TRUE(without.HasValue()) << without.Error();
    EXPECT_EQ(without.Value().macros, Client::UI::Macros{});

    const std::string macros = "primera\r\nsegunda\r\n";
    WriteSceneFile(folder.Path(), "MACRO.TXT", std::as_bytes(std::span(macros)));
    const auto data = LoadGameData(folder.Path());
    ASSERT_TRUE(data.HasValue()) << data.Error();
    EXPECT_EQ(data.Value().macros[0], "primera");
    EXPECT_EQ(data.Value().macros[1], "segunda");
    EXPECT_EQ(data.Value().macros[2], "");
}

// Q-C531: a word that does not fit in its place is an error that names the file.
TEST(GameDataTest, AnOverflowingMacroIsNamed)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {{1, "uno"}});
    const std::string macros(Client::UI::MacroSlotBytes, 'x');
    WriteSceneFile(folder.Path(), "Macro.txt", std::as_bytes(std::span(macros)));

    const auto data = LoadGameData(folder.Path());

    ASSERT_FALSE(data.HasValue());
    EXPECT_NE(data.Error().find("Macro.txt"), std::string::npos) << data.Error();
    EXPECT_NE(data.Error().find("0x61c1c0"), std::string::npos) << data.Error();
}

// 0x61ec8f → 0x448440: movereq.bmd case-insensitive; without it, no destination and no error. A short one is named.
// The destination is made up.
TEST(GameDataTest, ReadsTheMoveRequirements)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {{1, "uno"}});
    WriteSceneFile(folder.Path(), "Gate.bmd", GateTableBytes({}));
    WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
    Client::Tests::WriteEmptyItemSetFiles(folder.Path());
    Client::Tests::WriteEmptySkillFile(folder.Path());

    const auto without = LoadGameData(folder.Path());
    ASSERT_TRUE(without.HasValue()) << without.Error();
    EXPECT_TRUE(without.Value().moves.empty());

    std::vector<std::byte> file(4 + Client::Tables::MoveRecordSize);
    file[0] = std::byte{1};
    const std::span<std::byte> record = std::span(file).subspan(4);
    record[0] = std::byte{7};
    record[4] = std::byte{'C'};
    record[5] = std::byte{'a'};
    record[0x44] = std::byte{40};
    Client::Crypto::Xor3(record);
    WriteSceneFile(folder.Path(), "Local/MoveReq.bmd", file);
    const auto data = LoadGameData(folder.Path());
    ASSERT_TRUE(data.HasValue()) << data.Error();
    ASSERT_EQ(data.Value().moves.size(), 1u);
    EXPECT_EQ(data.Value().moves[0].index, 7);
    EXPECT_EQ(data.Value().moves[0].name, "Ca");
    EXPECT_EQ(data.Value().moves[0].level, 40);

    file.pop_back();
    WriteSceneFile(folder.Path(), "Local/MoveReq.bmd", file);
    const auto shortFile = LoadGameData(folder.Path());
    ASSERT_FALSE(shortFile.HasValue());
    EXPECT_NE(shortFile.Error().find("Local\\movereq.bmd"), std::string::npos) << shortFile.Error();
}

// 0x61ed05 → 0x54e7f0: NpcName(Eng).txt case-insensitive; without it, the empty table and no error. main.cpp copies
// the records to the start of WorldRuntime::npcNames' 512 (NpcNameTableOf), where 0x54ebb0 looks for them. One the
// original would not finish reading is named. The names are made up.
TEST(GameDataTest, ReadsTheNpcNames)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {{1, "uno"}});
    WriteSceneFile(folder.Path(), "Gate.bmd", GateTableBytes({}));
    WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
    Client::Tests::WriteEmptyItemSetFiles(folder.Path());
    Client::Tests::WriteEmptySkillFile(folder.Path());

    const auto without = LoadGameData(folder.Path());
    ASSERT_TRUE(without.HasValue()) << without.Error();
    EXPECT_TRUE(without.Value().npcNames.empty());
    const Client::Scenes::NpcNameTable empty = Client::Scenes::NpcNameTableOf(without.Value().npcNames);
    EXPECT_EQ(empty.size(), Client::Scenes::NpcNameCount);
    EXPECT_EQ(Client::Scenes::NpcNameOf(empty, 0), "");
    EXPECT_FALSE(Client::Scenes::NpcNameOf(empty, 7).has_value());

    const auto write = [&folder](std::string_view text)
    {
        const std::span<const std::byte> bytes = std::as_bytes(std::span(text));
        WriteSceneFile(folder.Path(), "Local/NPCNAME(eng).TXT", bytes);
    };
    write("// inventados\r\n 7 1 \"Guardia inventado\"\r\n 9 1 \"Elfa inventada\"\r\n");
    const auto data = LoadGameData(folder.Path());
    ASSERT_TRUE(data.HasValue()) << data.Error();
    ASSERT_EQ(data.Value().npcNames.size(), 2u);
    const Client::Scenes::NpcNameTable table = Client::Scenes::NpcNameTableOf(data.Value().npcNames);
    ASSERT_EQ(table.size(), Client::Scenes::NpcNameCount);
    EXPECT_EQ(Client::Scenes::NpcNameOf(table, 7), "Guardia inventado");
    EXPECT_EQ(Client::Scenes::NpcNameOf(table, 9), "Elfa inventada");
    EXPECT_EQ(table[2].type, 0);
    EXPECT_EQ(Client::Scenes::NpcNameOf(table, 0), "");

    write(" 7 1 \"Guardia inventado\" // sin fin de línea");
    const auto hanging = LoadGameData(folder.Path());
    ASSERT_FALSE(hanging.HasValue());
    EXPECT_NE(hanging.Error().find("Local\\NpcName(Eng).txt"), std::string::npos) << hanging.Error();
}

// 0x54ebb0 returns the pointer to +2 of the record: a name without '\0' in its 0x34 bytes (the one a long name of
// 0x54e7f0 left) goes on through the next record's type (the low byte first) and its name, up to the first '\0'; in
// the last record, up to the end of the table. The names are made up.
TEST(GameDataTest, ANameWithoutItsZeroGoesOnInTheNextRecord)
{
    Client::Scenes::NpcNameTable table = Client::Scenes::NpcNameTableOf({});
    const std::string full(Client::Scenes::NpcNameBytes, 'a');
    table[3].type = 5;
    std::ranges::copy(full, table[3].name.begin());
    table[4].type = 0x6362;
    std::ranges::copy(std::string_view("def"), table[4].name.begin());
    EXPECT_EQ(Client::Scenes::NpcNameOf(table, 5), full + "bcdef");
    table[4].type = 0x0062;
    EXPECT_EQ(Client::Scenes::NpcNameOf(table, 5), full + "b");
    table[4].type = 0x6200;
    EXPECT_EQ(Client::Scenes::NpcNameOf(table, 5), full);

    table.back().type = 8;
    std::ranges::fill(table.back().name, 'z');
    EXPECT_EQ(Client::Scenes::NpcNameOf(table, 8), std::string(Client::Scenes::NpcNameBytes, 'z'));
}

// One with another sum (or the other file's) is an error that names it, like the original's "File corrupted", which
// closes.
TEST(GameDataTest, ACorruptFilterIsNamed)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {{1, "uno"}});
    WriteFilterFile(folder.Path(), {"Synth"}, Client::Tables::FilterChecksumKey, "Local/FilterName.bmd");

    const auto data = LoadGameData(folder.Path());

    ASSERT_FALSE(data.HasValue());
    EXPECT_NE(data.Error().find("Local\\FilterName.bmd"), std::string::npos) << data.Error();
}

TEST(GameDataTest, ReadsTheGateTableFromTheDataRootIgnoringCase)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {});
    const Client::Tables::GateRecord gate{1, 0, 121, 232, 123, 233, 2, 0, 20};
    WriteSceneFile(folder.Path(), "gATE.BMD", GateTableBytes({{1, gate}}));
    WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
    Client::Tests::WriteEmptyItemSetFiles(folder.Path());
    Client::Tests::WriteEmptySkillFile(folder.Path());

    const auto data = LoadGameData(folder.Path());

    ASSERT_TRUE(data.HasValue()) << data.Error();
    ASSERT_TRUE(data.Value().gates.has_value());
    EXPECT_EQ(Client::Tables::ReadGateRecord(*data.Value().gates, 1), gate);
}

TEST(GameDataTest, AMissingGateTableIsNamed)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {});

    const auto data = LoadGameData(folder.Path());

    ASSERT_FALSE(data.HasValue());
    EXPECT_NE(data.Error().find("Gate.bmd"), std::string::npos) << data.Error();
}

TEST(GameDataTest, AGateTableOnlyInLocalDoesNotSatisfyTheRootFile)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {});
    WriteSceneFile(folder.Path(), "Local/Gate.bmd", GateTableBytes({}));

    const auto data = LoadGameData(folder.Path());

    ASSERT_FALSE(data.HasValue());
    EXPECT_NE(data.Error().find("no está Gate.bmd"), std::string::npos) << data.Error();
}

TEST(GameDataTest, AShortGateTableIsNamed)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {});
    const std::vector<std::byte> shortFile(Client::Tables::GateTableSize - 1);
    WriteSceneFile(folder.Path(), "Gate.bmd", shortFile);

    const auto data = LoadGameData(folder.Path());

    ASSERT_FALSE(data.HasValue());
    EXPECT_NE(data.Error().find("Gate.bmd"), std::string::npos) << data.Error();
    EXPECT_NE(data.Error().find("más corto"), std::string::npos) << data.Error();
}

TEST(GameDataTest, AnUnreadableGateTableIsNamed)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {});
    std::filesystem::create_directory(folder.Path() / "Gate.bmd");

    const auto data = LoadGameData(folder.Path());

    ASSERT_FALSE(data.HasValue());
    EXPECT_NE(data.Error().find("no se pudo leer"), std::string::npos) << data.Error();
    EXPECT_NE(data.Error().find("Gate.bmd"), std::string::npos) << data.Error();
}

TEST(GameDataTest, GateTableIgnoresATailLargerThanSixtyFourKiB)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {});
    const Client::Tables::GateRecord gate{1, 0, 121, 232, 123, 233, 2, 0, 20};
    std::vector<std::byte> file = GateTableBytes({{1, gate}});
    constexpr std::size_t TailBytes = 128 * 1024;
    file.resize(file.size() + TailBytes, std::byte{0xff});
    WriteSceneFile(folder.Path(), "Gate.bmd", file);
    WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
    Client::Tests::WriteEmptyItemSetFiles(folder.Path());
    Client::Tests::WriteEmptySkillFile(folder.Path());

    const auto data = LoadGameData(folder.Path());

    ASSERT_TRUE(data.HasValue()) << data.Error();
    ASSERT_TRUE(data.Value().gates.has_value());
    EXPECT_EQ(data.Value().gates->Count(), Client::Tables::GateCount);
    EXPECT_EQ(Client::Tables::ReadGateRecord(*data.Value().gates, 1), gate);
}
// Catches a missing load, a wrong drop of the checksum and an arbitrary cap on the tail.
TEST(GameDataTest, LoadsItemsIgnoringCaseAndAnArbitrarilyLargeTail)
{
    const TemporaryFolder folder("game-data-items");
    WriteTextTable(folder.Path(), {}, "local/TEXT.BMD");
    WriteSceneFile(folder.Path(), "Gate.bmd", GateTableBytes({}));
    auto bytes = Client::Tests::EmptyItemTableBytes();
    bytes.resize(bytes.size() + 2 * 1024 * 1024, std::byte{0xff});
    WriteSceneFile(folder.Path(), "local/ITEM.BMD", bytes);
    Client::Tests::WriteEmptyItemSetFiles(folder.Path(), "local");
    Client::Tests::WriteEmptySkillFile(folder.Path(), "local");
    const auto loaded = LoadGameData(folder.Path());
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_TRUE(loaded.Value().items.has_value());
    EXPECT_EQ(loaded.Value().items->table.Count(), Client::Tables::ItemCount);
    EXPECT_TRUE(loaded.Value().items->checksumMatches);
}

TEST(GameDataTest, KeepsItemsWithAMissingChecksumAndReportsIt)
{
    const TemporaryFolder folder("game-data-items");
    WriteTextTable(folder.Path(), {});
    WriteSceneFile(folder.Path(), "Gate.bmd", GateTableBytes({}));
    auto bytes = Client::Tests::EmptyItemTableBytes();
    bytes.resize(Client::Tables::ItemTableSize);
    WriteSceneFile(folder.Path(), "Local/Item.bmd", bytes);
    Client::Tests::WriteEmptyItemSetFiles(folder.Path());
    Client::Tests::WriteEmptySkillFile(folder.Path());
    const auto loaded = LoadGameData(folder.Path());
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_TRUE(loaded.Value().items.has_value());
    EXPECT_FALSE(loaded.Value().items->checksumMatches);
}

TEST(GameDataTest, NamesMissingAndShortItemTables)
{
    const TemporaryFolder folder("game-data-items");
    WriteTextTable(folder.Path(), {});
    WriteSceneFile(folder.Path(), "Gate.bmd", GateTableBytes({}));
    auto loaded = LoadGameData(folder.Path());
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("Item.bmd"), std::string::npos);
    WriteSceneFile(folder.Path(), "Local/Item.bmd", std::vector<std::byte>(5));
    loaded = LoadGameData(folder.Path());
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("Item.bmd"), std::string::npos);
    EXPECT_NE(loaded.Error().find("más corto"), std::string::npos);
}

// 0x40c600 from 0x61ccb0 (0x61ecd5): ItemSetType.bmd and ItemSetOption.bmd, from Data\Local and case-insensitive on
// the name, with their records in clear.
TEST(GameDataTest, LoadsTheItemSetTables)
{
    const TemporaryFolder folder("game-data-item-sets");
    WriteTextTable(folder.Path(), {});
    WriteSceneFile(folder.Path(), "Gate.bmd", GateTableBytes({}));
    WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
    std::vector<std::byte> types(Client::Tables::ItemSetTypeTableSize, std::byte{0xff});
    types[2 * Client::Tables::ItemSetTypeRecordSize] = std::byte{21};
    std::vector<std::byte> options(Client::Tables::ItemSetOptionTableSize);
    options[3 * Client::Tables::ItemSetOptionRecordSize] = std::byte{'H'};
    WriteSceneFile(folder.Path(), "Local/itemsettype.bmd", Client::Tests::ItemSetTypeFileBytes(types));
    WriteSceneFile(folder.Path(), "Local/ITEMSETOPTION.BMD", Client::Tests::ItemSetOptionFileBytes(options));
    Client::Tests::WriteEmptySkillFile(folder.Path());

    const auto loaded = LoadGameData(folder.Path());

    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_TRUE(loaded.Value().itemSets.has_value());
    EXPECT_EQ((*loaded.Value().itemSets->types.Record(2))[0], std::byte{21});
    EXPECT_EQ((*loaded.Value().itemSets->types.Record(3))[0], std::byte{0xff});
    EXPECT_EQ((*loaded.Value().itemSets->options.Record(3))[0], std::byte{'H'});
}

// Without one of the two, or with one that does not pass its checksum, the original shows "File not exist." or "File
// corrupted." and closes the window (0x40c660 and 0x40c850): an error that names it.
TEST(GameDataTest, NamesMissingShortAndCorruptItemSetTables)
{
    const TemporaryFolder folder("game-data-item-sets");
    WriteTextTable(folder.Path(), {});
    WriteSceneFile(folder.Path(), "Gate.bmd", GateTableBytes({}));
    WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
    auto loaded = LoadGameData(folder.Path());
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("no está Local\\ItemSetType.bmd"), std::string::npos) << loaded.Error();

    Client::Tests::WriteEmptyItemSetFiles(folder.Path());
    Client::Tests::WriteEmptySkillFile(folder.Path());
    std::vector<std::byte> corrupt =
        Client::Tests::ItemSetOptionFileBytes(std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize));
    corrupt[Client::Tables::ItemSetOptionTableSize - 1] ^= std::byte{0x01};
    WriteSceneFile(folder.Path(), "Local/ItemSetOption.bmd", corrupt);
    loaded = LoadGameData(folder.Path());
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("Local\\ItemSetOption.bmd"), std::string::npos) << loaded.Error();
    EXPECT_NE(loaded.Error().find(Client::Data::DescribeDataError(Client::Data::DataError::ChecksumMismatch)),
              std::string::npos)
        << loaded.Error();

    corrupt.resize(Client::Tables::ItemSetOptionTableSize + 3);
    WriteSceneFile(folder.Path(), "Local/ItemSetOption.bmd", corrupt);
    loaded = LoadGameData(folder.Path());
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find(Client::Data::DescribeDataError(Client::Data::DataError::FileTooShort)),
              std::string::npos)
        << loaded.Error();
}
// A complete Data except Skill.bmd: tests that it is required without triggering another load error first.
void WriteGameDataWithoutSkills(const std::filesystem::path& root)
{
    WriteTextTable(root, {});
    WriteSceneFile(root, "Gate.bmd", GateTableBytes({}));
    WriteSceneFile(root, "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
    Client::Tests::WriteEmptyItemSetFiles(root);
}

TEST(GameDataTest, AMissingSkillTableIsNamed)
{
    const TemporaryFolder folder("game-data-skills");
    WriteGameDataWithoutSkills(folder.Path());
    const auto loaded = LoadGameData(folder.Path());
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("Local\\Skill.bmd"), std::string::npos) << loaded.Error();
}

TEST(GameDataTest, LoadsSkillRecordsIgnoringCaseAndAnArbitrarilyLargeTail)
{
    const TemporaryFolder folder("game-data-skills");
    WriteGameDataWithoutSkills(folder.Path());
    std::vector<std::byte> plain(Client::Tables::SkillTableSize);
    constexpr std::size_t Record = 42;
    constexpr std::size_t Offset = 0x26;
    plain[Record * Client::Tables::SkillRecordSize + Offset] = std::byte{0xa5};
    auto file = Client::Tests::SkillFileBytes(std::move(plain));
    file.resize(file.size() + 2 * 1024 * 1024, std::byte{0xff});
    WriteSceneFile(folder.Path(), "Local/sKILL.BMD", file);
    const auto loaded = LoadGameData(folder.Path());
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_TRUE(loaded.Value().skills.has_value());
    EXPECT_EQ(loaded.Value().skills->Count(), Client::Tables::SkillCount);
    ASSERT_TRUE(loaded.Value().skills->Record(Record).has_value());
    EXPECT_EQ((*loaded.Value().skills->Record(Record))[Offset], std::byte{0xa5});
}

TEST(GameDataTest, NamesASkillFileWithAnIncompleteChecksum)
{
    const TemporaryFolder folder("game-data-skills");
    WriteGameDataWithoutSkills(folder.Path());
    auto file = Client::Tests::SkillFileBytes(std::vector<std::byte>(Client::Tables::SkillTableSize));
    file.pop_back();
    WriteSceneFile(folder.Path(), "Local/Skill.bmd", file);
    const auto loaded = LoadGameData(folder.Path());
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("Local\\Skill.bmd"), std::string::npos) << loaded.Error();
    EXPECT_NE(loaded.Error().find(Client::Data::DescribeDataError(Client::Data::DataError::FileTooShort)),
              std::string::npos)
        << loaded.Error();
}

TEST(GameDataTest, NamesACorruptSkillTable)
{
    const TemporaryFolder folder("game-data-skills");
    WriteGameDataWithoutSkills(folder.Path());
    auto file = Client::Tests::SkillFileBytes(std::vector<std::byte>(Client::Tables::SkillTableSize));
    file[Client::Tables::SkillTableSize - 1] ^= std::byte{1};
    WriteSceneFile(folder.Path(), "Local/Skill.bmd", file);
    const auto loaded = LoadGameData(folder.Path());
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("Local\\Skill.bmd"), std::string::npos) << loaded.Error();
    EXPECT_NE(loaded.Error().find(Client::Data::DescribeDataError(Client::Data::DataError::ChecksumMismatch)),
              std::string::npos)
        << loaded.Error();
}

TEST(GameDataTest, NamesAnUnreadableSkillTable)
{
    const TemporaryFolder folder("game-data-skills");
    WriteGameDataWithoutSkills(folder.Path());
    std::filesystem::create_directory(folder.Path() / "Local/Skill.bmd");
    const auto loaded = LoadGameData(folder.Path());
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("Local\\Skill.bmd"), std::string::npos) << loaded.Error();
    EXPECT_NE(loaded.Error().find("no se pudo leer"), std::string::npos) << loaded.Error();
}

using GoldenGameData = Client::Tests::GoldenDataTest;

// Real file data read with XOR3 separately: the byte MoveHero uses lives at +0x26.
TEST_F(GoldenGameData, LoadsTheRealSkillTableWithItsMovementBytes)
{
    const auto loaded = LoadGameData(m_dataRoot);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_TRUE(loaded.Value().skills.has_value());
    EXPECT_EQ(loaded.Value().skills->Count(), 256u);
    EXPECT_EQ((*loaded.Value().skills->Record(1))[0x26], std::byte{6});
    EXPECT_EQ((*loaded.Value().skills->Record(42))[0x26], std::byte{3});
    EXPECT_EQ((*loaded.Value().skills->Record(60))[0x26], std::byte{4});
    EXPECT_EQ((*loaded.Value().skills->Record(255))[0x26], std::byte{0});
}

} // namespace
