#include "Tables/GateTable.h"

#include "Data/DataError.h"
#include "support/BytePattern.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace
{
using Client::Data::DataError;
using Client::Tables::GateCount;
using Client::Tables::GateRecordSize;
using Client::Tables::GateTableSize;
using Client::Tables::LoadGateTable;
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

// Q-C24: the original would go on with the records its reused buffer leaves.
TEST(GateTableTest, ShortFileIsAnError)
{
    const auto oneByteShort = LoadGateTable(std::vector<std::byte>(GateTableSize - 1));
    const auto empty = LoadGateTable(std::span<const std::byte>{});

    ASSERT_FALSE(oneByteShort.HasValue());
    EXPECT_EQ(oneByteShort.Error(), DataError::FileTooShort);
    ASSERT_FALSE(empty.HasValue());
    EXPECT_EQ(empty.Error(), DataError::FileTooShort);
}

// Even if they are enough for one more record.
TEST(GateTableTest, ExtraBytesAreIgnored)
{
    const std::vector<std::byte> plain = BytePattern(GateTableSize + GateRecordSize, 37, 11);

    const auto loaded = LoadGateTable(EncryptRecords(plain, GateRecordSize));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Count(), GateCount);
    EXPECT_EQ(FirstRecordThatDiffers(loaded.Value(), std::span(plain).first(GateTableSize)), std::nullopt);
}
} // namespace
