#include "Scenes/GameData.h"

#include "Data/Tables/GateRecord.h"
#include "Data/Tables/GateTable.h"
#include "Data/Tables/ItemTable.h"
#include "support/ItemFiles.h"
#include "Data/Text/FilterWords.h"
#include "Data/Text/TextTable.h"
#include "support/SceneDataFiles.h"
#include "support/GateFiles.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <system_error>
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

    const auto data = LoadGameData(folder.Path());

    ASSERT_TRUE(data.HasValue()) << data.Error();
    EXPECT_TRUE(data.Value().filterName.empty());
    EXPECT_TRUE(data.Value().filter.empty());
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
} // namespace
