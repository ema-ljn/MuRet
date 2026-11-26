#include "Data/Terrain/TerrainLight.h"

#include "Core/Ascii.h"
#include "Core/Expected.h"
#include "Data/DataError.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using Client::Core::EqualsIgnoringAsciiCase;
using Client::Data::DataError;
using Client::Data::Terrain::LoadTerrainLight;
using Client::Data::Terrain::TerrainLight;
using Client::Tests::DeviasCapture;
using Client::Tests::HasExtension;
using Client::Tests::LorenciaCapture;
using Client::Tests::MemoryCapture;
using Client::Tests::ReadFileBytes;
using Client::Tests::SameBytes;

using GoldenTerrainLight = Client::Tests::GoldenDataTest;

// A TerrainLight.OZJ in 19 of the lab's 20 worlds (World6 has none), plus TerrainLight2 and two test ones
// (TerrainLightTest and TerrainLightTest2) in World31, and TerrainLight1 and TerrainLight2 in World35.
constexpr std::size_t TerrainLightFilesOfTheLab = 24;

// TerrainLight<something>.OZJ, ignoring case.
bool IsTerrainLight(const std::filesystem::path& path)
{
    constexpr std::string_view prefix = "TerrainLight";
    const std::string stem = path.stem().string();
    return HasExtension(path, ".ozj") && stem.size() >= prefix.size() &&
           EqualsIgnoringAsciiCase(std::string_view(stem).substr(0, prefix.size()), prefix);
}

Client::Core::Expected<std::unique_ptr<TerrainLight>, DataError> LoadFile(const std::filesystem::path& path)
{
    const std::optional<std::vector<char>> file = ReadFileBytes(path);
    if (!file.has_value())
    {
        ADD_FAILURE() << "no se pudo leer " << path;
        return Client::Core::Unexpected(DataError::FileTooShort);
    }
    return LoadTerrainLight(std::as_bytes(std::span(*file)));
}

// All 256 × 256 color.
TEST_F(GoldenTerrainLight, LoadsEveryLightOfTheLab)
{
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(m_dataRoot))
    {
        if (!entry.is_regular_file() || !IsTerrainLight(entry.path()))
        {
            continue;
        }
        ++files;
        const auto loaded = LoadFile(entry.path());
        EXPECT_TRUE(loaded.HasValue()) << entry.path().lexically_relative(m_dataRoot).generic_string();
    }

    EXPECT_EQ(files, TerrainLightFilesOfTheLab);
}

// The light the original had at 0x078ecc10, with the same bits: Devias's (s31_light.bin) and Lorencia's
// (lor_light.bin).
class MemoryTerrainLight : public Client::Tests::MemoryDumpTest
{
protected:
    void ExpectTheOriginalsMemory(const MemoryCapture& capture, std::string_view dumpName)
    {
        const auto path = DataFile("World" + std::to_string(capture.world) + "\\TerrainLight.OZJ");
        const auto dump = DumpFile(capture, dumpName);
        ASSERT_TRUE(path.has_value());
        ASSERT_TRUE(dump.has_value());
        const std::optional<std::vector<char>> memory = ReadFileBytes(*dump);
        ASSERT_TRUE(memory.has_value());

        const auto loaded = LoadFile(*path);

        ASSERT_TRUE(loaded.HasValue());
        EXPECT_TRUE(SameBytes(std::as_bytes(std::span(loaded.Value()->cells)), std::as_bytes(std::span(*memory))));
    }
};

TEST_F(MemoryTerrainLight, EqualsTheOriginalsMemoryInDevias)
{
    ExpectTheOriginalsMemory(DeviasCapture, "s31_light.bin");
}

TEST_F(MemoryTerrainLight, EqualsTheOriginalsMemoryInLorencia)
{
    ExpectTheOriginalsMemory(LorenciaCapture, "lor_light.bin");
}
} // namespace
