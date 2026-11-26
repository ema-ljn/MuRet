#include "support/FileBytes.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using Client::Tests::ReadFileBytes;

// With a random suffix, so that two runs at once do not share the file.
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

// ResolveDataPath also returns folders.
TEST(FileBytesTest, AFolderOrNothingIsNotRead)
{
    EXPECT_EQ(ReadFileBytes(fs::temp_directory_path()), std::nullopt);
    EXPECT_EQ(ReadFileBytes(TemporaryPath("no-existe")), std::nullopt);
}
} // namespace
