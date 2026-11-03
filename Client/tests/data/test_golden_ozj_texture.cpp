#include "Data/Textures/OzjTexture.h"

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Textures/Texture.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/JpegVectors.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// OZJ reales: los de la carpeta golden y, si está, los buffers de textura de la memoria del original
// (Client/tests/support/GoldenDir.h).
namespace
{
using Client::Data::DataError;
using Client::Data::Textures::LoadOzjTexture;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::Texture;
using Client::Tests::HasExtension;
using Client::Tests::LorenciaCapture;
using Client::Tests::OzjFilesOfTheLab;
using Client::Tests::ReadFileBytes;
using Client::Tests::SameBytes;

using GoldenOzjTexture = Client::Tests::GoldenDataTest;
using MemoryOzjTexture = Client::Tests::MemoryDumpTest;

// La textura de `path`, o el error con que no cargó.
Client::Core::Expected<Texture, DataError> LoadFile(const std::filesystem::path& path)
{
    const std::optional<std::vector<char>> file = ReadFileBytes(path);
    if (!file.has_value())
    {
        ADD_FAILURE() << "no se pudo leer " << path;
        return Client::Core::Unexpected(DataError::FileTooShort);
    }
    return LoadOzjTexture(std::as_bytes(std::span(*file)));
}

// Los 2.387 OZJ del laboratorio cargan salvo ocho, que el original tampoco carga: los 7 TGA de
// Q-C14, que libjpeg rechaza, y skill\null.OZJ, de 800 × 600, más grande que 512. Que todos son en
// color lo mira test_golden_jpeg_image.cpp.
TEST_F(GoldenOzjTexture, LoadsEveryOzjOfTheLabButTheTgaAndTheOneOver512)
{
    std::size_t files = 0;
    std::vector<std::string> rejected;
    // Sin error_code: si no se puede recorrer la carpeta, el iterador tira filesystem_error y el
    // test falla con su mensaje.
    for (const auto& entry : std::filesystem::recursive_directory_iterator(m_dataRoot))
    {
        if (!entry.is_regular_file() || !HasExtension(entry.path(), ".ozj"))
        {
            continue;
        }
        ++files;
        const std::string name = entry.path().lexically_relative(m_dataRoot).generic_string();
        const auto loaded = LoadFile(entry.path());
        if (!loaded.HasValue())
        {
            EXPECT_EQ(loaded.Error(), DataError::InvalidData) << name;
            rejected.push_back(name);
            continue;
        }
        const Texture& texture = loaded.Value();
        EXPECT_EQ(texture.format, PixelFormat::Rgb) << name;
        EXPECT_GT(texture.width, 0) << name;
        EXPECT_GT(texture.height, 0) << name;
        // Ninguno de los que cargan pasa de 512, así que el lado es la potencia de 2 más chica que
        // lo cubre.
        EXPECT_EQ(static_cast<unsigned>(texture.textureWidth), std::bit_ceil(static_cast<unsigned>(texture.width)))
            << name;
        EXPECT_EQ(static_cast<unsigned>(texture.textureHeight), std::bit_ceil(static_cast<unsigned>(texture.height)))
            << name;
    }

    std::ranges::sort(rejected);
    EXPECT_EQ(files, OzjFilesOfTheLab);
    EXPECT_EQ(rejected, (std::vector<std::string>{
                            "object34/Pool001.OZJ",
                            "object34/ma_001.OZJ",
                            "object35/Pool001.OZJ",
                            "object35/cr1_shadow.OZJ",
                            "object35/ma_001.OZJ",
                            "object55/Pool001.OZJ",
                            "object55/ma_001.OZJ",
                            "skill/null.OZJ",
                        }));
}

// Las texturas son las que tenía en memoria el main.exe original, en su tabla de texturas
// (0x07D3A198, el buffer de +0x34), en la captura de Lorencia: los 9 tiles OZJ de World1, en los
// slots 0x23 a 0x2b (README de mu-golden). Son todas de lados potencia de 2, así que el buffer es
// la imagen.
TEST_F(MemoryOzjTexture, EqualsTheOriginalsMemory)
{
    struct Slot
    {
        std::string_view file;
        std::string_view dump;
    };
    constexpr std::array<Slot, 9> slots{{
        {"World1\\TileGrass01.OZJ", "lor2_slot023.bin"},
        {"World1\\TileGrass02.OZJ", "lor2_slot024.bin"},
        {"World1\\TileGround01.OZJ", "lor2_slot025.bin"},
        {"World1\\TileGround02.OZJ", "lor2_slot026.bin"},
        {"World1\\TileGround03.OZJ", "lor2_slot027.bin"},
        {"World1\\TileWater01.OZJ", "lor2_slot028.bin"},
        {"World1\\TileWood01.OZJ", "lor2_slot029.bin"},
        {"World1\\TileRock01.OZJ", "lor2_slot02a.bin"},
        {"World1\\TileRock02.OZJ", "lor2_slot02b.bin"},
    }};
    for (const Slot& slot : slots)
    {
        SCOPED_TRACE(slot.file);
        const auto path = DataFile(slot.file);
        const auto dump = DumpFile(LorenciaCapture, slot.dump);
        ASSERT_TRUE(path.has_value());
        ASSERT_TRUE(dump.has_value());
        const std::optional<std::vector<char>> memory = ReadFileBytes(*dump);
        ASSERT_TRUE(memory.has_value());

        const auto loaded = LoadFile(*path);

        ASSERT_TRUE(loaded.HasValue());
        EXPECT_EQ(loaded.Value().textureWidth, loaded.Value().width);
        EXPECT_EQ(loaded.Value().textureHeight, loaded.Value().height);
        EXPECT_TRUE(SameBytes(std::as_bytes(std::span(loaded.Value().pixels)), std::as_bytes(std::span(*memory))));
    }
}
} // namespace
