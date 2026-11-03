#include "support/FileBytes.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <random>
#include <string>
#include <vector>

// ReadFileBytes, el que lee los archivos golden: todo el archivo, o nada si no es un archivo.
namespace
{
namespace fs = std::filesystem;
using Client::Tests::ReadFileBytes;

// Con un sufijo al azar, para que dos corridas a la vez no compartan el archivo.
fs::path TemporaryPath(const std::string& name)
{
    return fs::temp_directory_path() / ("muret-filebytes-" + name + "-" + std::to_string(std::random_device{}()));
}

TEST(FileBytesTest, ReadsTheWholeFile)
{
    const fs::path file = TemporaryPath("archivo");
    {
        std::ofstream stream(file, std::ios::binary);
        stream.write("\x00\xFF\x0D\x0A", 4);
    }

    const auto bytes = ReadFileBytes(file);
    fs::remove(file);

    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(*bytes, (std::vector<char>{'\x00', '\xFF', '\x0D', '\x0A'}));
}

TEST(FileBytesTest, EmptyFileHasNoBytes)
{
    const fs::path file = TemporaryPath("vacio");
    std::ofstream(file, std::ios::binary).close();

    const auto bytes = ReadFileBytes(file);
    fs::remove(file);

    ASSERT_TRUE(bytes.has_value());
    EXPECT_TRUE(bytes->empty());
}

// Una carpeta o algo que no existe no se lee. ResolveDataPath también devuelve carpetas.
TEST(FileBytesTest, AFolderOrNothingIsNotRead)
{
    EXPECT_EQ(ReadFileBytes(fs::temp_directory_path()), std::nullopt);
    EXPECT_EQ(ReadFileBytes(TemporaryPath("no-existe")), std::nullopt);
}
} // namespace
