#include "Scenes/GameData.h"

#include "Data/Text/TextTable.h"
#include "support/SceneDataFiles.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>

// Client::Scenes::LoadGameData: lo que main.cpp lee antes de la primera escena, como WinMain lee
// Text.bmd en 0x4b9b9f (0x61ee30 y 0x5495e0, con "Data\Local\Text.bmd" de 0x6d4370), antes de las
// fuentes y de la escena 1. Con archivos sintéticos: los textos de verdad no van a los tests.
namespace
{
using Client::Scenes::LoadGameData;
using Client::Tests::TemporaryFolder;
using Client::Tests::WriteTextTable;

TEST(GameDataTest, ReadsTheTextTable)
{
    const TemporaryFolder folder("game-data");
    WriteTextTable(folder.Path(), {{0, "cara de prueba"}, {456, "versión %s"}});

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
} // namespace
