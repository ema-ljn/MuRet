#include "Data/Tables/TableChecksum.h"

#include "Data/Tables/ItemTable.h"
#include "Data/Tables/SkillTable.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// El checksum contra tablas reales (Client/tests/support/GoldenDir.h): cada una guarda el suyo
// después de los registros, y tiene que dar ese con su clave y su paridad.
//
// La Item.bmd del laboratorio no sirve de oráculo: trae 0x50 bytes después de los registros y su
// checksum, y no pasa el suyo, porque el cliente parcheado no lo mira (Q-C01; eso lo prueba su
// cargador, en GoldenItemTableTest.cpp). La itemtest.bmd de la misma carpeta tiene el tamaño justo
// y sí pasa: es la que prueba la clave y la paridad de Item.
namespace
{
using Client::Data::Tables::ChecksumParity;
using Client::Data::Tables::ItemChecksumKey;
using Client::Data::Tables::ItemTableSize;
using Client::Data::Tables::SkillChecksumKey;
using Client::Data::Tables::SkillTableSize;
using Client::Data::Tables::StoredChecksum;
using Client::Data::Tables::TableChecksum;
using Client::Tests::ReadFileBytes;

using GoldenTableChecksum = Client::Tests::GoldenDataTest;

// El checksum que guarda `file` después de sus `tableSize` bytes de registros, y el que da
// calcularlo. El archivo tiene que medir los registros más el checksum, ni un byte más.
struct Checksums
{
    std::uint32_t stored = 0;
    std::uint32_t computed = 0;
};

std::optional<Checksums> ReadChecksums(std::span<const std::byte> file, std::size_t tableSize, std::uint32_t key,
                                       ChecksumParity parity)
{
    const std::optional<std::uint32_t> stored = StoredChecksum(file, tableSize);
    if (!stored || file.size() != tableSize + sizeof(std::uint32_t))
    {
        return std::nullopt;
    }
    return Checksums{*stored, TableChecksum(file.first(tableSize), key, parity)};
}

TEST_F(GoldenTableChecksum, MatchesTheOneStoredInSkillBmd)
{
    const auto path = DataFile("Local\\Skill.bmd");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());

    const std::optional<Checksums> checksums =
        ReadChecksums(std::as_bytes(std::span(*file)), SkillTableSize, SkillChecksumKey, ChecksumParity::XorEvenDwords);

    ASSERT_TRUE(checksums.has_value());
    EXPECT_EQ(checksums->computed, checksums->stored);
}

TEST_F(GoldenTableChecksum, MatchesTheOneStoredInItemTestBmd)
{
    const auto path = DataFile("Local\\itemtest.bmd");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());

    const std::optional<Checksums> checksums =
        ReadChecksums(std::as_bytes(std::span(*file)), ItemTableSize, ItemChecksumKey, ChecksumParity::XorOddDwords);

    ASSERT_TRUE(checksums.has_value());
    EXPECT_EQ(checksums->computed, checksums->stored);
}
} // namespace
