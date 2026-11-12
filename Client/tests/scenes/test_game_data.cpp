#include "Scenes/GameData.h"

#include "Core/Xor3.h"
#include "Data/DataError.h"
#include "Data/DataFile.h"
#include "Data/Tables/GateRecord.h"
#include "Data/Tables/GateTable.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "support/ItemFiles.h"
#include "support/ItemSetFiles.h"
#include "Data/Text/FilterWords.h"
#include "Data/Text/MoveRequirements.h"
#include "Data/Text/TextTable.h"
#include "support/SceneDataFiles.h"
#include "support/GateFiles.h"
#include "support/TemporaryFolder.h"
#include "support/SkillFiles.h"
#include "support/GoldenDir.h"
#include "Data/Tables/SkillTable.h"
#include "UI/MacroFile.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// Client::Scenes::LoadGameData: lo que main.cpp lee antes de la primera escena, como WinMain lee
// Text.bmd en 0x4b9b9f (0x61ee30 y 0x5495e0, con "Data\Local\Text.bmd" de 0x6d4370), antes de las
// fuentes y de la escena 1. Con archivos sintéticos: los textos de verdad no van a los tests.
namespace
{
using Client::Scenes::LoadGameData;
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

// Como en Windows, sin mirar mayúsculas: "local\TEXT.BMD" también es el archivo.
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
// Un archivo de más de 1 MiB no es un Text.bmd (la tabla mide 0x96000): no se lee entero.
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
// 0x61ccb0 lee también Filter.bmd y FilterName.bmd (Data\Local): las palabras que no puede tener
// el nombre de un personaje nuevo.
TEST(GameDataTest, ReadsTheTwoFilters)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {{1, "uno"}});
    WriteSceneFile(folder.Path(), "Gate.bmd", GateTableBytes({}));
    WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
    Client::Tests::WriteEmptyItemSetFiles(folder.Path());
    Client::Tests::WriteEmptySkillFile(folder.Path());
    WriteFilterFile(folder.Path(), {"Synth", "Other"}, Client::Data::Text::FilterNameChecksumKey,
                    "Local/FilterName.bmd");
    WriteFilterFile(folder.Path(), {"word"}, Client::Data::Text::FilterChecksumKey, "Local/filter.BMD");

    const auto data = LoadGameData(folder.Path());

    ASSERT_TRUE(data.HasValue()) << data.Error();
    EXPECT_EQ(data.Value().filterName, (std::vector<std::string>{"Synth", "Other"}));
    EXPECT_EQ(data.Value().filter, (std::vector<std::string>{"word"}));
}

// Sin los dos, nada que filtrar: el original no arranca, y MuRet sigue sin ellos.
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

// 0x61ee30 → 0x61c1c0: Macro.txt, detrás de Text.bmd, sin mirar mayúsculas; sin él, las macros vacías. Las macros
// son inventadas.
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

// Q-C531: una palabra que no entra en su lugar es un error que nombra el archivo.
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

// 0x61ec8f → 0x448440: movereq.bmd sin mirar mayúsculas; sin él, ningún destino y sin error. Uno corto lo nombra. El
// destino es inventado.
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

    std::vector<std::byte> file(4 + Client::Data::Text::MoveRecordSize);
    file[0] = std::byte{1};
    const std::span<std::byte> record = std::span(file).subspan(4);
    record[0] = std::byte{7};
    record[4] = std::byte{'C'};
    record[5] = std::byte{'a'};
    record[0x44] = std::byte{40};
    Client::Core::Xor3(record);
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

// Uno con otra suma (o la del otro archivo) es un error que lo nombra, como el "File corrupted" del
// original, que se cierra.
TEST(GameDataTest, ACorruptFilterIsNamed)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {{1, "uno"}});
    WriteFilterFile(folder.Path(), {"Synth"}, Client::Data::Text::FilterChecksumKey, "Local/FilterName.bmd");

    const auto data = LoadGameData(folder.Path());

    ASSERT_FALSE(data.HasValue());
    EXPECT_NE(data.Error().find("Local\\FilterName.bmd"), std::string::npos) << data.Error();
}

TEST(GameDataTest, ReadsTheGateTableFromTheDataRootIgnoringCase)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {});
    const Client::Data::Tables::GateRecord gate{1, 0, 121, 232, 123, 233, 2, 0, 20};
    WriteSceneFile(folder.Path(), "gATE.BMD", GateTableBytes({{1, gate}}));
    WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
    Client::Tests::WriteEmptyItemSetFiles(folder.Path());
    Client::Tests::WriteEmptySkillFile(folder.Path());

    const auto data = LoadGameData(folder.Path());

    ASSERT_TRUE(data.HasValue()) << data.Error();
    ASSERT_TRUE(data.Value().gates.has_value());
    EXPECT_EQ(Client::Data::Tables::ReadGateRecord(*data.Value().gates, 1), gate);
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
    const std::vector<std::byte> shortFile(Client::Data::Tables::GateTableSize - 1);
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
    const Client::Data::Tables::GateRecord gate{1, 0, 121, 232, 123, 233, 2, 0, 20};
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
    EXPECT_EQ(data.Value().gates->Count(), Client::Data::Tables::GateCount);
    EXPECT_EQ(Client::Data::Tables::ReadGateRecord(*data.Value().gates, 1), gate);
}
// Detecta ausencia de carga, descarte incorrecto del checksum y un techo arbitrario a la cola.
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
    EXPECT_EQ(loaded.Value().items->table.Count(), Client::Data::Tables::ItemCount);
    EXPECT_TRUE(loaded.Value().items->checksumMatches);
}

TEST(GameDataTest, KeepsItemsWithAMissingChecksumAndReportsIt)
{
    const TemporaryFolder folder("game-data-items");
    WriteTextTable(folder.Path(), {});
    WriteSceneFile(folder.Path(), "Gate.bmd", GateTableBytes({}));
    auto bytes = Client::Tests::EmptyItemTableBytes();
    bytes.resize(Client::Data::Tables::ItemTableSize);
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

// 0x40c600 desde 0x61ccb0 (0x61ecd5): ItemSetType.bmd e ItemSetOption.bmd, de Data\Local y sin mirar
// las mayúsculas del nombre, con sus registros en claro.
TEST(GameDataTest, LoadsTheItemSetTables)
{
    const TemporaryFolder folder("game-data-item-sets");
    WriteTextTable(folder.Path(), {});
    WriteSceneFile(folder.Path(), "Gate.bmd", GateTableBytes({}));
    WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
    std::vector<std::byte> types(Client::Data::Tables::ItemSetTypeTableSize, std::byte{0xff});
    types[2 * Client::Data::Tables::ItemSetTypeRecordSize] = std::byte{21};
    std::vector<std::byte> options(Client::Data::Tables::ItemSetOptionTableSize);
    options[3 * Client::Data::Tables::ItemSetOptionRecordSize] = std::byte{'H'};
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

// Sin uno de los dos, o con uno que no pasa su checksum, el original muestra "File not exist." o "File
// corrupted." y cierra la ventana (0x40c660 y 0x40c850): un error que lo nombra.
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
        Client::Tests::ItemSetOptionFileBytes(std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize));
    corrupt[Client::Data::Tables::ItemSetOptionTableSize - 1] ^= std::byte{0x01};
    WriteSceneFile(folder.Path(), "Local/ItemSetOption.bmd", corrupt);
    loaded = LoadGameData(folder.Path());
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("Local\\ItemSetOption.bmd"), std::string::npos) << loaded.Error();
    EXPECT_NE(loaded.Error().find(Client::Data::DescribeDataError(Client::Data::DataError::ChecksumMismatch)),
              std::string::npos)
        << loaded.Error();

    corrupt.resize(Client::Data::Tables::ItemSetOptionTableSize + 3);
    WriteSceneFile(folder.Path(), "Local/ItemSetOption.bmd", corrupt);
    loaded = LoadGameData(folder.Path());
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find(Client::Data::DescribeDataError(Client::Data::DataError::FileTooShort)),
              std::string::npos)
        << loaded.Error();
}
// Un Data completo salvo Skill.bmd: permite probar su obligación sin adelantar otro error de carga.
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
    std::vector<std::byte> plain(Client::Data::Tables::SkillTableSize);
    constexpr std::size_t Record = 42;
    constexpr std::size_t Offset = 0x26;
    plain[Record * Client::Data::Tables::SkillRecordSize + Offset] = std::byte{0xa5};
    auto file = Client::Tests::SkillFileBytes(std::move(plain));
    file.resize(file.size() + 2 * 1024 * 1024, std::byte{0xff});
    WriteSceneFile(folder.Path(), "Local/sKILL.BMD", file);
    const auto loaded = LoadGameData(folder.Path());
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_TRUE(loaded.Value().skills.has_value());
    EXPECT_EQ(loaded.Value().skills->Count(), Client::Data::Tables::SkillCount);
    ASSERT_TRUE(loaded.Value().skills->Record(Record).has_value());
    EXPECT_EQ((*loaded.Value().skills->Record(Record))[Offset], std::byte{0xa5});
}

TEST(GameDataTest, NamesASkillFileWithAnIncompleteChecksum)
{
    const TemporaryFolder folder("game-data-skills");
    WriteGameDataWithoutSkills(folder.Path());
    auto file = Client::Tests::SkillFileBytes(std::vector<std::byte>(Client::Data::Tables::SkillTableSize));
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
    auto file = Client::Tests::SkillFileBytes(std::vector<std::byte>(Client::Data::Tables::SkillTableSize));
    file[Client::Data::Tables::SkillTableSize - 1] ^= std::byte{1};
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

// Datos del archivo real leídos con XOR3 por separado: el byte usado por MoveHero vive en +0x26.
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
