// La regla de la carpeta golden (pregunta 2 del plan CL0): sin variable se saltea, con una
// carpeta que no existe falla.

#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>

namespace
{

namespace fs = std::filesystem;
using Client::Tests::CheckGoldenDir;
using Client::Tests::GoldenDirFromValue;
using Client::Tests::GoldenDirState;

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

} // namespace
