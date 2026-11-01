// Text.bmd real: el de la carpeta golden y, si está, el volcado de la memoria del original
// (Client/tests/support/GoldenDir.h).

#include "Data/Text/TextTable.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace
{

using Client::Data::Text::LoadTextTable;
using Client::Data::Text::TextCount;
using Client::Data::Text::TextRecordSize;
using Client::Data::Text::TextTableSize;
using Client::Tests::DeviasCapture;
using Client::Tests::ReadFileBytes;

using GoldenTextTable = Client::Tests::GoldenDataTest;
using MemoryTextTable = Client::Tests::MemoryDumpTest;

constexpr std::string_view TextFile = "Local\\Text.bmd";

TEST_F(GoldenTextTable, LoadsTheRealFile)
{
    const auto path = DataFile(TextFile);
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->size(), TextTableSize); // 614.400 bytes

    const auto loaded = LoadTextTable(std::as_bytes(std::span(*file)));

    ASSERT_TRUE(loaded.HasValue());
    std::size_t fullRecords = 0;
    for (std::size_t index = 0; index < TextCount; ++index)
    {
        const std::optional<std::string_view> text = loaded.Value().Text(index);
        ASSERT_TRUE(text.has_value()) << "el texto " << index;
        if (text->size() == TextRecordSize)
        {
            ++fullRecords;
        }
    }
    // Q-C07: el registro 7 ocupa sus 300 bytes sin '\0', y es el único.
    const std::optional<std::string_view> seventh = loaded.Value().Text(7);
    ASSERT_TRUE(seventh.has_value());
    EXPECT_EQ(seventh->size(), TextRecordSize);
    EXPECT_EQ(fullRecords, 1u);
}

// El oráculo más fuerte: cada texto que da MuRet sale del registro que tenía en memoria el main.exe
// original (g_GlobalText, 0x076BD1DC) después de cargar el mismo archivo. Comparar contra el
// archivo solo probaría que el XOR3 se deshace. El registro se corta como lo corta MuRet, en el
// primer '\0' o a los 300 bytes (Q-C07): el original lee el texto 7 como cadena de C y sigue en el
// registro 8, y eso no se copia.
TEST_F(MemoryTextTable, EqualsTheOriginalsMemory)
{
    const auto path = DataFile(TextFile);
    const auto dump = DumpFile(DeviasCapture, "s31_text.bin");
    ASSERT_TRUE(path.has_value());
    ASSERT_TRUE(dump.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    const std::optional<std::vector<char>> memory = ReadFileBytes(*dump);
    ASSERT_TRUE(file.has_value());
    ASSERT_TRUE(memory.has_value());
    ASSERT_EQ(memory->size(), TextTableSize);

    const auto loaded = LoadTextTable(std::as_bytes(std::span(*file)));

    ASSERT_TRUE(loaded.HasValue());
    for (std::size_t index = 0; index < TextCount; ++index)
    {
        const std::string_view record(memory->data() + index * TextRecordSize, TextRecordSize);
        const std::string_view original = record.substr(0, record.find('\0'));
        const std::optional<std::string_view> text = loaded.Value().Text(index);
        ASSERT_TRUE(text.has_value()) << "el texto " << index;
        // Si falla, dice el índice y los largos, y no los textos: no se imprimen textos del juego.
        ASSERT_TRUE(*text == original) << "el texto " << index << ": " << text->size() << " bytes contra "
                                       << original.size();
    }
}

} // namespace
