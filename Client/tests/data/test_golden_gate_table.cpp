#include "Data/Tables/GateTable.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Gate.bmd real: el de la carpeta golden y, si está, el volcado de la memoria del original
// (Client/tests/support/GoldenDir.h).
namespace
{
using Client::Data::Tables::GateCount;
using Client::Data::Tables::GateTableSize;
using Client::Data::Tables::LoadGateTable;
using Client::Tests::DeviasCapture;
using Client::Tests::FirstRecordThatDiffers;
using Client::Tests::ReadFileBytes;

using GoldenGateTable = Client::Tests::GoldenDataTest;
using MemoryGateTable = Client::Tests::MemoryDumpTest;

// El original lo abre de Data\, no de Data\Local\ (0x6d2aec, "Data\\Gate.bmd").
constexpr std::string_view GateFile = "Gate.bmd";

TEST_F(GoldenGateTable, LoadsTheRealFile)
{
    const auto path = DataFile(GateFile);
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->size(), GateTableSize);

    const auto loaded = LoadGateTable(std::as_bytes(std::span(*file)));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Count(), GateCount);
}

// Cada registro es el que tenía en memoria el main.exe original (la tabla de 0x07688AB8) después
// de cargar el mismo archivo.
TEST_F(MemoryGateTable, EqualsTheOriginalsMemory)
{
    const auto path = DataFile(GateFile);
    const auto dump = DumpFile(DeviasCapture, "s31_gate.bin");
    ASSERT_TRUE(path.has_value());
    ASSERT_TRUE(dump.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    const std::optional<std::vector<char>> memory = ReadFileBytes(*dump);
    ASSERT_TRUE(file.has_value());
    ASSERT_TRUE(memory.has_value());
    ASSERT_EQ(memory->size(), GateTableSize);

    const auto loaded = LoadGateTable(std::as_bytes(std::span(*file)));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(FirstRecordThatDiffers(loaded.Value(), std::as_bytes(std::span(*memory))), std::nullopt);
}
} // namespace
