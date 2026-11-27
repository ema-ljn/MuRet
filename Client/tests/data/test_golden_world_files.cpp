#include "Data/Terrain/TerrainAttributesFile.h"
#include "Data/Terrain/TerrainHeightsFile.h"
#include "Data/Terrain/TerrainMappingFile.h"
#include "Data/Terrain/TerrainObjectsFile.h"

#include "Core/Ascii.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using Client::Core::EqualsIgnoringAsciiCase;
using Client::Core::ToLowerAscii;
using Client::Data::Terrain::ReadTerrainAttributesFile;
using Client::Data::Terrain::ReadTerrainHeightsFile;
using Client::Data::Terrain::ReadTerrainMappingFile;
using Client::Data::Terrain::ReadTerrainObjectsFile;
using Client::Data::Terrain::WriteTerrainAttributesFile;
using Client::Data::Terrain::WriteTerrainHeightsFile;
using Client::Data::Terrain::WriteTerrainMappingFile;
using Client::Data::Terrain::WriteTerrainObjectsFile;
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

std::string LowerAscii(std::string text)
{
    std::ranges::transform(text, text.begin(), ToLowerAscii);
    return text;
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

TEST_F(GoldenWorldFiles, EveryAttributesFileIsWrittenBackTheSame)
{
    const std::vector<std::filesystem::path> files = WorldFiles(m_dataRoot, "EncTerrain", ".att");

    EXPECT_EQ(files.size(), 29U);
    for (const std::filesystem::path& file : files)
    {
        const std::string name = file.lexically_relative(m_dataRoot).generic_string();
        const std::vector<std::byte> bytes = FileBytes(file);
        const auto read = ReadTerrainAttributesFile(bytes);
        ASSERT_TRUE(read.HasValue()) << name;
        EXPECT_TRUE(SameBytes(WriteTerrainAttributesFile(*read.Value()), bytes)) << name;
    }
}

// Measured on 2026-11-27: five of the 16 files with two bytes per cell, in two worlds, use the high byte.
TEST_F(GoldenWorldFiles, TheWordFilesKeepTheirHighBytes)
{
    const std::map<std::string, std::size_t> expected = {
        {"world31/encterrain31.att", 348},      {"world31/encterrain312.att", 348},
        {"world31/encterraintest312.att", 348}, {"world31/encterraintest31.att", 348},
        {"world56/encterrain56.att", 337},
    };
    std::size_t wordFiles = 0;
    for (const std::filesystem::path& file : WorldFiles(m_dataRoot, "EncTerrain", ".att"))
    {
        const std::string name = file.lexically_relative(m_dataRoot).generic_string();
        const auto read = ReadTerrainAttributesFile(FileBytes(file));
        ASSERT_TRUE(read.HasValue()) << name;
        if (!read.Value()->highBytes.has_value())
        {
            continue;
        }
        ++wordFiles;
        const auto notZero = static_cast<std::size_t>(
            std::ranges::count_if(*read.Value()->highBytes, [](std::uint8_t high) { return high != 0; }));
        const auto known = expected.find(LowerAscii(name));
        EXPECT_EQ(notZero, known == expected.end() ? std::size_t{0} : known->second) << name;
    }
    EXPECT_EQ(wordFiles, 16U);
}

TEST_F(GoldenWorldFiles, EveryObjectsFileIsWrittenBackTheSame)
{
    const std::vector<std::filesystem::path> files = WorldFiles(m_dataRoot, "EncTerrain", ".obj");

    EXPECT_EQ(files.size(), 23U);
    for (const std::filesystem::path& file : files)
    {
        const std::string name = file.lexically_relative(m_dataRoot).generic_string();
        const std::vector<std::byte> bytes = FileBytes(file);
        const auto read = ReadTerrainObjectsFile(bytes);
        ASSERT_TRUE(read.HasValue()) << name;
        const auto written = WriteTerrainObjectsFile(read.Value());
        ASSERT_TRUE(written.HasValue()) << name;
        EXPECT_TRUE(SameBytes(written.Value(), bytes)) << name;
    }
}

TEST_F(GoldenWorldFiles, EveryHeightsFileIsWrittenBackTheSame)
{
    const std::vector<std::filesystem::path> files = WorldFiles(m_dataRoot, "TerrainHeight", ".ozb");

    EXPECT_EQ(files.size(), 19U);
    for (const std::filesystem::path& file : files)
    {
        const std::string name = file.lexically_relative(m_dataRoot).generic_string();
        const std::vector<std::byte> bytes = FileBytes(file);
        const auto read = ReadTerrainHeightsFile(bytes);
        ASSERT_TRUE(read.HasValue()) << name;
        EXPECT_TRUE(SameBytes(WriteTerrainHeightsFile(*read.Value()), bytes)) << name;
    }
}
} // namespace
