#include "Data/Tables/GateRecord.h"

#include "Data/Tables/GateTable.h"
#include "support/GateFiles.h"

#include <gtest/gtest.h>

#include <optional>

namespace
{
using Client::Data::Tables::GateCount;
using Client::Data::Tables::GateRecord;
using Client::Data::Tables::GateTable;
using Client::Data::Tables::ReadGateRecord;
using Client::Tests::GateTableBytes;

TEST(GateRecordTest, CopiesEveryByteWithoutChangingTheRecord)
{
    const GateRecord expected{1, 2, 3, 4, 5, 6, 255, 128, 254};
    const GateTable table(GateTableBytes({{255, expected}}));

    EXPECT_EQ(ReadGateRecord(table, 255), expected);
    EXPECT_EQ(ReadGateRecord(table, 0), GateRecord{});
}

TEST(GateRecordTest, AnIndexOutsideTheTableHasNoRecord)
{
    const GateTable table(GateTableBytes({}));

    EXPECT_EQ(ReadGateRecord(table, GateCount), std::nullopt);
    EXPECT_EQ(ReadGateRecord(table, GateCount + 1), std::nullopt);
}
} // namespace
