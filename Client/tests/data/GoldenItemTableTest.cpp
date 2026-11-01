// Item.bmd real: el de la carpeta golden y, si está, el volcado de la memoria del original
// (Client/tests/support/GoldenDir.h).

#include "Data/Tables/ItemTable.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace
{

using Client::Data::Tables::ItemCount;
using Client::Data::Tables::ItemTableSize;
using Client::Data::Tables::LoadItemTable;
using Client::Tests::FirstRecordThatDiffers;
using Client::Tests::ReadFileBytes;

using GoldenItemTable = Client::Tests::GoldenDataTest;
using MemoryItemTable = Client::Tests::MemoryDumpTest;

constexpr std::string_view ItemFile = "Local\\Item.bmd";
constexpr std::size_t LabExtraBytes = 0x50; // los que trae la Item.bmd del laboratorio después del checksum

// Q-C01: la Item.bmd del laboratorio trae 0x50 bytes después del checksum y no pasa el suyo, y el
// original la carga igual. MuRet también, y lo informa.
TEST_F(GoldenItemTable, LoadsTheRealFileAlthoughItsChecksumDoesNotMatch)
{
    const auto path = DataFile(ItemFile);
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->size(), ItemTableSize + sizeof(std::uint32_t) + LabExtraBytes); // 688.212 bytes

    const auto loaded = LoadItemTable(std::as_bytes(std::span(*file)));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_FALSE(loaded.Value().checksumMatches);
    EXPECT_EQ(loaded.Value().table.Count(), ItemCount);
}

// La itemtest.bmd de la misma carpeta tiene el tamaño justo y sí pasa su checksum.
TEST_F(GoldenItemTable, ItemTestBmdPassesItsChecksum)
{
    const auto path = DataFile("Local\\itemtest.bmd");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());

    const auto loaded = LoadItemTable(std::as_bytes(std::span(*file)));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_TRUE(loaded.Value().checksumMatches);
}

// Cada registro es el que tenía en memoria el main.exe original (la tabla de 0x07758550) después
// de cargar la misma Item.bmd: la del checksum que no coincide.
TEST_F(MemoryItemTable, EqualsTheOriginalsMemory)
{
    const auto path = DataFile(ItemFile);
    const auto dump = DumpFile("s31_item.bin");
    ASSERT_TRUE(path.has_value());
    ASSERT_TRUE(dump.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    const std::optional<std::vector<char>> memory = ReadFileBytes(*dump);
    ASSERT_TRUE(file.has_value());
    ASSERT_TRUE(memory.has_value());
    ASSERT_EQ(memory->size(), ItemTableSize);

    const auto loaded = LoadItemTable(std::as_bytes(std::span(*file)));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(FirstRecordThatDiffers(loaded.Value().table, std::as_bytes(std::span(*memory))), std::nullopt);
}

} // namespace
