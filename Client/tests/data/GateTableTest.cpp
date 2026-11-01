// Client::Data::Tables::LoadGateTable: Gate.bmd, con archivos sintéticos.

#include "Data/Tables/GateTable.h"

#include "Data/DataError.h"
#include "support/BytePattern.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace
{

using Client::Data::DataError;
using Client::Data::Tables::GateCount;
using Client::Data::Tables::GateRecordSize;
using Client::Data::Tables::GateTableSize;
using Client::Data::Tables::LoadGateTable;
using Client::Tests::BytePattern;
using Client::Tests::EncryptRecords;
using Client::Tests::FirstRecordThatDiffers;

TEST(GateTableTest, LoadsTheRecordsInTheClear)
{
    const std::vector<std::byte> plain = BytePattern(GateTableSize, 37, 11);

    const auto loaded = LoadGateTable(EncryptRecords(plain, GateRecordSize));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Count(), GateCount);
    EXPECT_EQ(FirstRecordThatDiffers(loaded.Value(), plain), std::nullopt);
}

// Q-C24: un archivo corto es un error y no hay tabla. El original seguiría con los registros que
// deja su buffer reusado.
TEST(GateTableTest, ShortFileIsAnError)
{
    const auto oneByteShort = LoadGateTable(std::vector<std::byte>(GateTableSize - 1));
    const auto empty = LoadGateTable(std::span<const std::byte>{});

    ASSERT_FALSE(oneByteShort.HasValue());
    EXPECT_EQ(oneByteShort.Error(), DataError::FileTooShort);
    ASSERT_FALSE(empty.HasValue());
    EXPECT_EQ(empty.Error(), DataError::FileTooShort);
}

// Los bytes de más se ignoran, aunque alcancen para un registro más.
TEST(GateTableTest, ExtraBytesAreIgnored)
{
    const std::vector<std::byte> plain = BytePattern(GateTableSize + GateRecordSize, 37, 11);

    const auto loaded = LoadGateTable(EncryptRecords(plain, GateRecordSize));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Count(), GateCount);
    EXPECT_EQ(FirstRecordThatDiffers(loaded.Value(), std::span(plain).first(GateTableSize)), std::nullopt);
}

} // namespace
