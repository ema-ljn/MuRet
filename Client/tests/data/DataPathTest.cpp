// Client::Data::ResolveDataPath contra un Data\ armado en una carpeta temporal.

#include "Data/DataPath.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{

namespace fs = std::filesystem;

class DataPathTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const std::string testName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        root = fs::temp_directory_path() / ("muret-datapath-" + testName);
        fs::remove_all(root);
        fs::create_directories(root);
    }

    void TearDown() override
    {
        std::error_code error;
        fs::remove_all(root, error);
    }

    // No se llama CreateFile: en Windows <windows.h> la define como macro (CreateFileW).
    void WriteDataFile(const fs::path& relative) const
    {
        fs::create_directories((root / relative).parent_path());
        std::ofstream(root / relative) << "x";
    }

    fs::path root;
};

TEST_F(DataPathTest, FindsAnExactPath)
{
    WriteDataFile("Player/Player.bmd");

    const auto path = Client::Data::ResolveDataPath(root, "Player/Player.bmd");

    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, root / "Player" / "Player.bmd");
}

TEST_F(DataPathTest, AcceptsBackslashesAndIgnoresCaseLikeWindows)
{
    // Como en el Data\ del laboratorio: la carpeta en minúscula y la extensión en mayúscula.
    WriteDataFile("player/Player.BMD");

    const auto path = Client::Data::ResolveDataPath(root, "Player\\player.bmd");

    ASSERT_TRUE(path.has_value());
    ASSERT_TRUE(fs::is_regular_file(*path));
    // Es el mismo archivo. El nombre devuelto no se compara: en Linux es el del disco
    // ("Player.BMD"), pero en Windows el primer intento ya lo encuentra con la grafía pedida.
    EXPECT_TRUE(fs::equivalent(*path, root / "player" / "Player.BMD"));
}

TEST_F(DataPathTest, ReturnsNulloptWhenTheFileIsMissing)
{
    WriteDataFile("Player/Player.bmd");

    EXPECT_FALSE(Client::Data::ResolveDataPath(root, "Player\\Player02.bmd").has_value());
    EXPECT_FALSE(Client::Data::ResolveDataPath(root, "Monster\\Monster50.bmd").has_value());
}

TEST_F(DataPathTest, RejectsPathsThatLeaveTheDataFolder)
{
    WriteDataFile("Enc1.dat");
    // Player\ existe, así que "Player\..\.." falla por el "..", no porque falte la carpeta.
    WriteDataFile("Player/Player.bmd");

    EXPECT_FALSE(Client::Data::ResolveDataPath(root, "").has_value());
    EXPECT_FALSE(Client::Data::ResolveDataPath(root, "..\\Enc1.dat").has_value());
    EXPECT_FALSE(Client::Data::ResolveDataPath(root, "Player\\..\\..\\Enc1.dat").has_value());
    EXPECT_FALSE(Client::Data::ResolveDataPath(root, "\\Enc1.dat").has_value());
    EXPECT_FALSE(Client::Data::ResolveDataPath(root, "C:\\Enc1.dat").has_value());
}

TEST_F(DataPathTest, PrefersTheExactNameWhenBothCasesExist)
{
    WriteDataFile("Item/a.bmd");
    std::error_code error;
    if (fs::exists(root / "item" / "a.bmd", error))
    {
        GTEST_SKIP() << "este sistema de archivos no distingue mayúsculas";
    }
    WriteDataFile("item/b.bmd");

    EXPECT_EQ(Client::Data::ResolveDataPath(root, "Item\\a.bmd"), root / "Item" / "a.bmd");
    EXPECT_EQ(Client::Data::ResolveDataPath(root, "item\\b.bmd"), root / "item" / "b.bmd");
    // Sin coincidencia exacta, la primera en orden de bytes: "Item" < "item".
    EXPECT_EQ(Client::Data::ResolveDataPath(root, "ITEM\\a.bmd"), root / "Item" / "a.bmd");
}

} // namespace
