#include "Data/Textures/OztTexture.h"

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Textures/Texture.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using Client::Data::DataError;
using Client::Data::Textures::LoadOztTexture;
using Client::Data::Textures::Texture;
using Client::Tests::HasExtension;
using Client::Tests::LorenciaCapture;
using Client::Tests::ReadFileBytes;
using Client::Tests::SameBytes;

using GoldenOztTexture = Client::Tests::GoldenDataTest;
using MemoryOztTexture = Client::Tests::MemoryDumpTest;

// 05-formatos-de-datos.md, the formats table.
constexpr std::size_t OztFilesOfTheLab = 664;

Client::Core::Expected<Texture, DataError> LoadFile(const std::filesystem::path& path)
{
    const std::optional<std::vector<char>> file = ReadFileBytes(path);
    if (!file.has_value())
    {
        ADD_FAILURE() << "no se pudo leer " << path;
        return Client::Core::Unexpected(DataError::FileTooShort);
    }
    return LoadOztTexture(std::as_bytes(std::span(*file)));
}

// Monster\wolfa02.OZT is 24-bit and the original rejects it.
TEST_F(GoldenOztTexture, LoadsEveryOztOfTheLabButTheOneOf24Bits)
{
    std::size_t files = 0;
    std::vector<std::string> rejected;
    // Without error_code: if the folder cannot be walked, the iterator throws filesystem_error and the test fails with
    // its message (the constructor's error_code does not cover the later steps).
    for (const auto& entry : std::filesystem::recursive_directory_iterator(m_dataRoot))
    {
        if (!entry.is_regular_file() || !HasExtension(entry.path(), ".ozt"))
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
        EXPECT_GT(texture.width, 0) << name;
        EXPECT_GT(texture.height, 0) << name;
        // None exceeds 512, so the side is the smallest power of 2 that covers it.
        EXPECT_EQ(static_cast<unsigned>(texture.textureWidth), std::bit_ceil(static_cast<unsigned>(texture.width)))
            << name;
        EXPECT_EQ(static_cast<unsigned>(texture.textureHeight), std::bit_ceil(static_cast<unsigned>(texture.height)))
            << name;
    }

    EXPECT_EQ(files, OztFilesOfTheLab);
    EXPECT_EQ(rejected, (std::vector<std::string>{"Monster/wolfa02.OZT"}));
}

// The original main.exe's texture table in memory (0x07D3A198, the buffer at +0x34) in the Lorencia capture: the 9
// cursors in slots 0x02 to 0x0a and World1's TileGrass01.OZT at 0x32 (mu-golden's README; the research compared them
// in s31-assets/byte-a-byte.md). All have power-of-2 sides, so the buffer is the image.
TEST_F(MemoryOztTexture, EqualsTheOriginalsMemory)
{
    struct Slot
    {
        std::string_view file;
        std::string_view dump;
    };
    constexpr std::array<Slot, 10> slots{{
        {"Interface\\Cursor.OZT", "lor2_slot002.bin"},
        {"Interface\\CursorPush.OZT", "lor2_slot003.bin"},
        {"Interface\\CursorAttack.OZT", "lor2_slot004.bin"},
        {"Interface\\CursorGet.OZT", "lor2_slot005.bin"},
        {"Interface\\CursorTalk.OZT", "lor2_slot006.bin"},
        {"Interface\\CursorRepair.OZT", "lor2_slot007.bin"},
        {"Interface\\CursorLeanAgainst.OZT", "lor2_slot008.bin"},
        {"Interface\\CursorSitDown.OZT", "lor2_slot009.bin"},
        {"Interface\\CursorDontMove.OZT", "lor2_slot00a.bin"},
        {"World1\\TileGrass01.OZT", "lor2_slot032.bin"},
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
