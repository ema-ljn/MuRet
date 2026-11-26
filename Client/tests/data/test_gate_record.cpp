#include "Tables/GateRecord.h"

#include "Tables/GateTable.h"
#include "support/GateFiles.h"

#include <gtest/gtest.h>

#include <optional>

namespace
{
using Client::Tables::GateCount;
using Client::Tables::GateRecord;
using Client::Tables::GateTable;
using Client::Tables::ReadGateRecord;
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
