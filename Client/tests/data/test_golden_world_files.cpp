#include "Data/Terrain/TerrainMappingFile.h"

#include "Core/Ascii.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using Client::Core::EqualsIgnoringAsciiCase;
using Client::Data::Terrain::ReadTerrainMappingFile;
using Client::Data::Terrain::WriteTerrainMappingFile;
using Client::Tests::HasExtension;
using Client::Tests::ReadFileBytes;
using Client::Tests::SameBytes;

using GoldenWorldFiles = Client::Tests::GoldenDataTest;

bool StartsWithIgnoringAsciiCase(std::string_view name, std::string_view prefix)
{
    return name.size() >= prefix.size() && EqualsIgnoringAsciiCase(name.substr(0, prefix.size()), prefix);
}

// Every file under `root` named `prefix`…`extension`, ignoring ASCII case, in the order of their relative paths.
std::vector<std::filesystem::path> WorldFiles(const std::filesystem::path& root, std::string_view prefix,
                                              std::string_view extension)
{
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (entry.is_regular_file() && HasExtension(entry.path(), extension) &&
            StartsWithIgnoringAsciiCase(entry.path().filename().string(), prefix))
        {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files, {}, [&root](const std::filesystem::path& file)
                      { return file.lexically_relative(root).generic_string(); });
    return files;
}

std::vector<std::byte> FileBytes(const std::filesystem::path& file)
{
    const std::optional<std::vector<char>> read = ReadFileBytes(file);
    if (!read.has_value())
    {
        ADD_FAILURE() << "could not read " << file;
        return {};
    }
    const std::span<const std::byte> bytes = std::as_bytes(std::span(*read));
    return {bytes.begin(), bytes.end()};
}

TEST_F(GoldenWorldFiles, EveryMappingIsWrittenBackTheSame)
{
    const std::vector<std::filesystem::path> files = WorldFiles(m_dataRoot, "EncTerrain", ".map");

    EXPECT_EQ(files.size(), 23U);
    for (const std::filesystem::path& file : files)
    {
        const std::string name = file.lexically_relative(m_dataRoot).generic_string();
        const std::vector<std::byte> bytes = FileBytes(file);
        const auto read = ReadTerrainMappingFile(bytes);
        ASSERT_TRUE(read.HasValue()) << name;
        EXPECT_TRUE(SameBytes(WriteTerrainMappingFile(*read.Value()), bytes)) << name;
    }
}
} // namespace
