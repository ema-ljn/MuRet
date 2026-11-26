#include "Data/Textures/JpegImage.h"

#include "Data/DataError.h"
#include "support/FileBytes.h"
#include "support/Fnv1a.h"
#include "support/GoldenDir.h"
#include "support/JpegVectors.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <ios>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// lab-6b.txt holds what 6b gives for each golden OZJ decoded as the original does: size, bytes per pixel and a pixel
// hash (Client/tests/vectors/jpeg/README.md). It proves turbo gives 6b's bytes in the lab (CODING_RULES.md §10), and
// must pass when turbo's version changes.
namespace
{
using Client::Data::DataError;
using Client::Data::Textures::DecodeJpeg;
using Client::Tests::Fnv1a64;
using Client::Tests::HasExtension;
using Client::Tests::JpegVectorsDir;
using Client::Tests::OzjFilesOfTheLab;
using Client::Tests::OzjRepeatedBytes;
using Client::Tests::ReadFileBytes;

using GoldenJpegImage = Client::Tests::GoldenDataTest;

// skill/null.OZJ, 800 × 600, is compared too: 6b decodes it and the texture loader then rejects it for exceeding 512.
constexpr int NoSideLimit = std::numeric_limits<int>::max();

// "path width height components fnv", or "path ERR code" if 6b stopped with an error before the last row (the
// msg_code of its jerror.h).
struct LabEntry
{
    std::string path;
    bool rejected = false;
    int width = 0;
    int height = 0;
    int components = 0;
    // 64-bit FNV-1a of the pixels, row by row from the top (Fnv1a.h).
    std::uint64_t hash = 0;
};

// The lab's paths have no spaces.
std::vector<std::string_view> Fields(std::string_view line)
{
    std::vector<std::string_view> fields;
    while (!line.empty())
    {
        const std::size_t space = line.find(' ');
        if (space != 0)
        {
            fields.push_back(line.substr(0, space));
        }
        if (space == std::string_view::npos)
        {
            break;
        }
        line.remove_prefix(space + 1);
    }
    return fields;
}

// std::nullopt if anything is left over or missing.
template <typename Number> std::optional<Number> ParseNumber(std::string_view text, int base = 10)
{
    Number value{};
    const char* end = text.data() + text.size();
    const auto [parsed, error] = std::from_chars(text.data(), end, value, base);
    if (error != std::errc{} || parsed != end)
    {
        return std::nullopt;
    }
    return value;
}

std::optional<LabEntry> ParseLine(std::string_view line)
{
    const std::vector<std::string_view> fields = Fields(line);
    if (fields.size() == 3 && fields[1] == "ERR" && ParseNumber<int>(fields[2]).has_value())
    {
        return LabEntry{.path = std::string(fields[0]), .rejected = true};
    }
    if (fields.size() != 5)
    {
        return std::nullopt;
    }
    const std::optional<int> width = ParseNumber<int>(fields[1]);
    const std::optional<int> height = ParseNumber<int>(fields[2]);
    const std::optional<int> components = ParseNumber<int>(fields[3]);
    const std::optional<std::uint64_t> hash = ParseNumber<std::uint64_t>(fields[4], 16);
    if (!width || !height || !components || !hash)
    {
        return std::nullopt;
    }
    return LabEntry{
        .path = std::string(fields[0]), .width = *width, .height = *height, .components = *components, .hash = *hash};
}

// A missing file or a line it cannot parse fails the test.
std::vector<LabEntry> ReadLab6b()
{
    const std::filesystem::path path = JpegVectorsDir() / "lab-6b.txt";
    const std::optional<std::vector<char>> file = ReadFileBytes(path);
    if (!file.has_value())
    {
        ADD_FAILURE() << "falta " << path;
        return {};
    }
    std::vector<LabEntry> entries;
    std::string_view text(file->data(), file->size());
    while (!text.empty())
    {
        const std::size_t newline = text.find('\n');
        const std::string_view line = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
        const std::optional<LabEntry> entry = ParseLine(line);
        if (!entry.has_value())
        {
            ADD_FAILURE() << "una línea de lab-6b.txt que no se entiende: " << line;
            continue;
        }
        entries.push_back(*entry);
    }
    return entries;
}

std::string Hex(std::uint64_t value)
{
    std::ostringstream text;
    text << std::hex << std::setw(16) << std::setfill('0') << value;
    return text.str();
}

TEST_F(GoldenJpegImage, TheLab6bListHasEveryOzjOfTheLab)
{
    std::vector<std::string> listed;
    for (const LabEntry& entry : ReadLab6b())
    {
        listed.push_back(entry.path);
    }
    std::vector<std::string> found;
    // Without error_code: if the folder cannot be walked, the iterator throws filesystem_error and the test fails with
    // its message.
    for (const auto& entry : std::filesystem::recursive_directory_iterator(m_dataRoot))
    {
        if (entry.is_regular_file() && HasExtension(entry.path(), ".ozj"))
        {
            found.push_back(entry.path().lexically_relative(m_dataRoot).generic_string());
        }
    }
    std::ranges::sort(listed);
    std::ranges::sort(found);

    EXPECT_EQ(listed.size(), OzjFilesOfTheLab);
    EXPECT_EQ(listed, found);
}

// Without its 24 repeated bytes (the original's fseek of 0x18). The 7 TGAs of Q-C14, which 6b rejects with
// JERR_NO_SOI, are InvalidData.
TEST_F(GoldenJpegImage, DecodesEveryOzjOfTheLabLikeTheLibjpeg6b)
{
    const std::vector<LabEntry> entries = ReadLab6b();
    ASSERT_EQ(entries.size(), OzjFilesOfTheLab);
    for (const LabEntry& entry : entries)
    {
        const std::optional<std::filesystem::path> path = DataFile(entry.path);
        if (!path.has_value())
        {
            continue;
        }
        const std::optional<std::vector<char>> file = ReadFileBytes(*path);
        if (!file.has_value() || file->size() < OzjRepeatedBytes)
        {
            ADD_FAILURE() << "no se pudo leer " << entry.path << ", o tiene menos de 24 bytes";
            continue;
        }
        const std::span<const std::byte> jpeg = std::as_bytes(std::span(*file)).subspan(OzjRepeatedBytes);

        const auto decoded = DecodeJpeg(jpeg, NoSideLimit);

        if (entry.rejected)
        {
            EXPECT_FALSE(decoded.HasValue()) << entry.path << " decodificó, y la 6b lo rechaza";
            if (!decoded.HasValue())
            {
                EXPECT_EQ(decoded.Error(), DataError::InvalidData) << entry.path;
            }
            continue;
        }
        if (!decoded.HasValue())
        {
            ADD_FAILURE() << entry.path << " no decodificó, y la 6b sí";
            continue;
        }
        EXPECT_EQ(decoded.Value().width, entry.width) << entry.path;
        EXPECT_EQ(decoded.Value().height, entry.height) << entry.path;
        EXPECT_EQ(decoded.Value().components, entry.components) << entry.path;
        EXPECT_EQ(Hex(Fnv1a64(std::as_bytes(std::span(decoded.Value().pixels)))), Hex(entry.hash)) << entry.path;
    }
}
} // namespace
