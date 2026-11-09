#include "Data/Tables/ItemSetTables.h"

#include "support/FileBytes.h"
#include "support/Fnv1a.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// ItemSetType.bmd e ItemSetOption.bmd reales, los de la carpeta golden (05, "Las tablas de los
// conjuntos"). Lo que se espera sale de los archivos, leídos aparte.
namespace
{
using Client::Data::Tables::ItemSetOptionCount;
using Client::Data::Tables::ItemSetOptionRecordSize;
using Client::Data::Tables::ItemSetOptionTable;
using Client::Data::Tables::ItemSetOptionTableSize;
using Client::Data::Tables::ItemSetTypeCount;
using Client::Data::Tables::ItemSetTypeTable;
using Client::Data::Tables::ItemSetTypeTableSize;
using Client::Data::Tables::LoadItemSetOptionTable;
using Client::Data::Tables::LoadItemSetTypeTable;
using Client::Tests::Fnv1a64;
using Client::Tests::ReadFileBytes;

constexpr std::uint8_t NoSet = 0xff;
// En el archivo de los conjuntos, los que tienen nombre son los impares del 1 al 31.
constexpr std::size_t NamedSets = 16;

class GoldenItemSetTables : public Client::Tests::GoldenDataTest
{
protected:
    std::optional<std::vector<char>> Read(std::string_view name)
    {
        const auto path = DataFile(name);
        if (!path)
        {
            return std::nullopt;
        }
        return ReadFileBytes(*path);
    }
};

std::uint8_t ByteOf(std::span<const std::byte> record, std::size_t offset)
{
    return std::to_integer<std::uint8_t>(record[offset]);
}

// El nombre del conjunto, hasta su 0.
std::string NameOf(std::span<const std::byte> record)
{
    std::string name;
    for (const std::byte value : record)
    {
        if (value == std::byte{0})
        {
            break;
        }
        name.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    return name;
}

// Los dos archivos tienen el tamaño justo y pasan su checksum, como pide 0x40c660. El FNV-1a de los
// registros en claro, de una lectura aparte con el XOR3 por registro.
TEST_F(GoldenItemSetTables, LoadsTheRealTypesWithTheirChecksum)
{
    const auto file = Read("Local\\ItemSetType.bmd");
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->size(), ItemSetTypeTableSize + sizeof(std::uint32_t)); // 32.772 bytes

    const auto loaded = LoadItemSetTypeTable(std::as_bytes(std::span(*file)));

    ASSERT_TRUE(loaded.HasValue());
    const ItemSetTypeTable& types = loaded.Value();
    ASSERT_EQ(types.Count(), ItemSetTypeCount);
    std::vector<std::byte> all;
    std::size_t withSet = 0;
    std::size_t withoutSet = 0;
    for (std::size_t type = 0; type < types.Count(); ++type)
    {
        const auto record = *types.Record(type);
        all.insert(all.end(), record.begin(), record.end());
        const std::uint8_t first = ByteOf(record, 0);
        // Los que tienen conjunto lo traen en el byte 0 y 0 en los otros tres: con & 3 en 2 o 3, ninguno.
        if (first != NoSet && first != 0)
        {
            ++withSet;
            EXPECT_EQ(ByteOf(record, 1), 0);
            EXPECT_EQ(ByteOf(record, 2), 0);
            EXPECT_EQ(ByteOf(record, 3), 0);
        }
        else if (first == NoSet)
        {
            ++withoutSet;
        }
    }
    EXPECT_EQ(withSet, 78u);
    EXPECT_EQ(withoutSet, 8114u);
    EXPECT_EQ(Fnv1a64(all), 0x2d372c1d89b52299u);
    // Rapier (0 2) es del conjunto 21 (Ceto) y Lightning Sword (0 14) del 11 (Hyon); Kris (0 0), de ninguno.
    EXPECT_EQ(ByteOf(*types.Record(2), 0), 21);
    EXPECT_EQ(ByteOf(*types.Record(14), 0), 11);
    EXPECT_EQ(ByteOf(*types.Record(0), 0), NoSet);
}

TEST_F(GoldenItemSetTables, LoadsTheRealSetsWithTheirChecksum)
{
    const auto file = Read("Local\\ItemSetOption.bmd");
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->size(), ItemSetOptionTableSize + sizeof(std::uint32_t)); // 4.868 bytes

    const auto loaded = LoadItemSetOptionTable(std::as_bytes(std::span(*file)));

    ASSERT_TRUE(loaded.HasValue());
    const ItemSetOptionTable& options = loaded.Value();
    ASSERT_EQ(options.Count(), ItemSetOptionCount);
    std::vector<std::byte> all;
    std::size_t named = 0;
    for (std::size_t set = 0; set < options.Count(); ++set)
    {
        const auto record = *options.Record(set);
        all.insert(all.end(), record.begin(), record.end());
        named += ByteOf(record, 0) != NoSet ? 1u : 0u;
    }
    EXPECT_EQ(named, NamedSets);
    EXPECT_EQ(Fnv1a64(all), 0xb0fb847a5c206b2au);

    // Hyperion (el 3): "Hyperion" con su 0, los pares (2, 15) y (1, 15), +0x3c en 4 (tres piezas), el
    // completo con (0x13, 20) y (0x0c, 30), y ninguna marca de clase.
    const auto hyperion = *options.Record(3);
    EXPECT_EQ(NameOf(hyperion), "Hyperion");
    EXPECT_EQ(ByteOf(hyperion, 0x20), 2);
    EXPECT_EQ(ByteOf(hyperion, 0x21), NoSet);
    EXPECT_EQ(ByteOf(hyperion, 0x22), 1);
    EXPECT_EQ(ByteOf(hyperion, 0x2c), 15);
    EXPECT_EQ(ByteOf(hyperion, 0x2e), 15);
    EXPECT_EQ(ByteOf(hyperion, 0x3c), 4);
    EXPECT_EQ(ByteOf(hyperion, 0x3d), 0x13);
    EXPECT_EQ(ByteOf(hyperion, 0x3e), 0x0c);
    EXPECT_EQ(ByteOf(hyperion, 0x3f), NoSet);
    EXPECT_EQ(ByteOf(hyperion, 0x42), 20);
    EXPECT_EQ(ByteOf(hyperion, 0x43), 30);
    for (std::size_t mark = 0x47; mark < ItemSetOptionRecordSize; ++mark)
    {
        EXPECT_EQ(ByteOf(hyperion, mark), 0);
    }
}
} // namespace
