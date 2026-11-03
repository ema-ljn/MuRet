#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <system_error>

// La regla de la carpeta golden (pregunta 2 del plan CL0): sin variable se saltea, con una
// carpeta que no existe falla.
namespace
{
namespace fs = std::filesystem;
using Client::Tests::CheckDump;
using Client::Tests::CheckGoldenDir;
using Client::Tests::DumpState;
using Client::Tests::GoldenDirFromValue;
using Client::Tests::GoldenDirState;
using Client::Tests::HasExtension;

TEST(GoldenDirTest, UnsetOrEmptyVariableMeansNoGoldenDir)
{
    EXPECT_FALSE(GoldenDirFromValue(std::nullopt).has_value());
    EXPECT_FALSE(GoldenDirFromValue(std::string()).has_value());
    EXPECT_EQ(GoldenDirFromValue(std::string("/datos/Data")), fs::path("/datos/Data"));
}

TEST(GoldenDirTest, WithoutAFolderTheTestIsSkipped)
{
    EXPECT_EQ(CheckGoldenDir(std::nullopt), GoldenDirState::NotSet);
}

TEST(GoldenDirTest, AFolderThatIsNotThereFailsTheTest)
{
    const fs::path temp = fs::temp_directory_path();
    // Con un sufijo al azar, para que dos corridas a la vez no compartan el archivo.
    const fs::path file = temp / ("muret-golden-no-es-carpeta-" + std::to_string(std::random_device{}()) + ".txt");
    std::ofstream(file) << "x";

    EXPECT_EQ(CheckGoldenDir(temp / "muret-golden-no-existe"), GoldenDirState::Missing);
    EXPECT_EQ(CheckGoldenDir(file), GoldenDirState::Missing);
    EXPECT_EQ(CheckGoldenDir(temp), GoldenDirState::Ready);

    std::error_code error;
    fs::remove(file, error);
}

// Un volcado se busca en la carpeta de su captura. Si está en la carpeta de volcados misma, la
// variable apunta a una captura (como antes de la CL1.4) y el test lo dice.
TEST(GoldenDirTest, ADumpIsInTheFolderOfItsCapture)
{
    const fs::path root = fs::temp_directory_path() / ("muret-volcados-" + std::to_string(std::random_device{}()));
    fs::create_directories(root / "captura");
    std::ofstream(root / "captura" / "a.bin") << "x";
    std::ofstream(root / "b.bin") << "x";

    EXPECT_EQ(CheckDump(root, "captura", "a.bin"), DumpState::Found);
    EXPECT_EQ(CheckDump(root, "captura", "b.bin"), DumpState::RootIsACapture);
    EXPECT_EQ(CheckDump(root, "captura", "c.bin"), DumpState::Missing);
    EXPECT_EQ(CheckDump(root, "otra", "a.bin"), DumpState::Missing);

    std::error_code error;
    fs::remove_all(root, error);
}

// La extensión se compara sin mayúsculas, entera y con su punto.
TEST(GoldenDirTest, TheExtensionIgnoresTheCase)
{
    EXPECT_TRUE(HasExtension("Skill/null.OZJ", ".ozj"));
    EXPECT_TRUE(HasExtension("world1/TileGrass01.Ozj", ".ozj"));
    EXPECT_FALSE(HasExtension("Interface/Cursor.OZT", ".ozj"));
    EXPECT_FALSE(HasExtension("Data/ozj", ".ozj"));
    EXPECT_FALSE(HasExtension("a.ozjx", ".ozj"));
}
} // namespace
