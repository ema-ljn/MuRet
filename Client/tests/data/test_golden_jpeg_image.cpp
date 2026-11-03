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

// DecodeJpeg contra la IJG libjpeg 6b en los OZJ reales. lab-6b.txt dice lo que da la 6b de cada
// OZJ de la carpeta golden, decodificado como lo hace el original: el tamaño, los bytes por píxel y
// un hash de los píxeles (Client/tests/vectors/jpeg/README.md). Acá se le pide lo mismo a
// DecodeJpeg, que usa libjpeg-turbo: es la prueba de que turbo da los bytes de la 6b en el
// laboratorio (CODING_RULES.md §10), y la que tiene que pasar al cambiar de versión de turbo.
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

// Sin tope de tamaño: se compara también skill/null.OZJ, de 800 × 600, que la 6b decodifica y que
// después el cargador de texturas rechaza por pasar de 512.
constexpr int NoSideLimit = std::numeric_limits<int>::max();

// Una línea de lab-6b.txt: "ruta ancho alto componentes fnv", o "ruta ERR código" si la 6b cortó
// con un error antes de la última fila (el msg_code de su jerror.h).
struct LabEntry
{
    std::string path;
    bool rejected = false;
    int width = 0;
    int height = 0;
    int components = 0;
    // El FNV-1a de 64 bits de los píxeles, fila por fila de arriba hacia abajo (Fnv1a.h).
    std::uint64_t hash = 0;
};

// Las partes de `line` separadas por espacios. Las rutas del laboratorio no tienen espacios.
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

// El número de `text` entero, en base `base`, o std::nullopt si sobra o falta algo.
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

// Las líneas de lab-6b.txt. Si falta o tiene una línea que no se entiende, el test falla.
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

// lab-6b.txt habla de todos los OZJ de la carpeta golden, y de nada más.
TEST_F(GoldenJpegImage, TheLab6bListHasEveryOzjOfTheLab)
{
    std::vector<std::string> listed;
    for (const LabEntry& entry : ReadLab6b())
    {
        listed.push_back(entry.path);
    }
    std::vector<std::string> found;
    // Sin error_code: si no se puede recorrer la carpeta, el iterador tira filesystem_error y el
    // test falla con su mensaje.
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

// Cada OZJ, sin sus 24 bytes repetidos (el fseek de 0x18 del original), da lo mismo que en la 6b:
// el mismo tamaño, los mismos bytes por píxel y los mismos píxeles. Los 7 TGA de Q-C14, que la 6b
// rechaza con JERR_NO_SOI, son InvalidData.
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
