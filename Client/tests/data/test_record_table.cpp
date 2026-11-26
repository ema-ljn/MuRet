#include "Data/Tables/RecordTable.h"

#include "Crypto/Xor3.h"
#include "support/BytePattern.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using Client::Crypto::Xor3;
using Client::Data::Tables::RecordTable;
using Client::Tests::BytePattern;

// Not a multiple of 3, so the key restarting on each record shows.
using SmallTable = RecordTable<4>;

// Record() works on a live table, not on a temporary.
template <typename Table>
concept ReadsRecords = requires(Table&& table) { std::forward<Table>(table).Record(0); };
static_assert(ReadsRecords<SmallTable&>);
static_assert(ReadsRecords<const SmallTable&>);
static_assert(!ReadsRecords<SmallTable>);
static_assert(!ReadsRecords<const SmallTable>);

// The length is in the type: a 4-byte std::span.
static_assert(std::is_same_v<decltype(std::declval<const SmallTable&>().Record(0)),
                             std::optional<std::span<const std::byte, 4>>>);

bool HasBytes(std::optional<std::span<const std::byte, 4>> record, std::array<std::byte, 4> expected)
{
    return record.has_value() && std::ranges::equal(*record, expected);
}

// Over zeros each record is FC CF AB FC: the original decrypts record by record (0x5496c0 with the record length), so
// the key restarts. End to end, the second record would be CF AB FC CF.
TEST(RecordTableTest, TheKeyStartsAgainOnEachRecord)
{
    const std::vector<std::byte> zeros(8);

    const SmallTable table(zeros);

    const std::array<std::byte, 4> key = {std::byte{0xFC}, std::byte{0xCF}, std::byte{0xAB}, std::byte{0xFC}};
    ASSERT_EQ(table.Count(), 2u);
    EXPECT_TRUE(HasBytes(table.Record(0), key));
    EXPECT_TRUE(HasBytes(table.Record(1), key));
}

TEST(RecordTableTest, GivesBackThePlainRecords)
{
    const std::vector<std::byte> plain = BytePattern(12, 37, 11);
    std::vector<std::byte> encrypted = plain;
    for (std::size_t offset = 0; offset < encrypted.size(); offset += 4)
    {
        Xor3(std::span(encrypted).subspan(offset, 4));
    }

    const SmallTable table(encrypted);

    ASSERT_EQ(table.Count(), 3u);
    for (std::size_t index = 0; index < table.Count(); ++index)
    {
        const std::optional<std::span<const std::byte, 4>> record = table.Record(index);
        ASSERT_TRUE(record.has_value()) << "el registro " << index;
        EXPECT_TRUE(std::ranges::equal(*record, std::span(plain).subspan(index * 4, 4))) << "el registro " << index;
    }
}

TEST(RecordTableTest, AnIncompleteRecordIsLeftOut)
{
    const std::vector<std::byte> bytes(4 + 3);

    const SmallTable table(bytes);

    EXPECT_EQ(table.Count(), 1u);
    EXPECT_TRUE(table.Record(0).has_value());
    EXPECT_FALSE(table.Record(1).has_value());
}

// An index whose product with the length wraps around and would land inside.
TEST(RecordTableTest, IndexPastTheTableHasNoRecord)
{
    constexpr std::size_t Largest = std::numeric_limits<std::size_t>::max();
    const SmallTable table(std::vector<std::byte>(8));

    EXPECT_FALSE(table.Record(2).has_value());
    EXPECT_FALSE(table.Record(Largest / 4 + 1).has_value());
    EXPECT_FALSE(table.Record(Largest).has_value());
}

TEST(RecordTableTest, AnEmptyFileGivesAnEmptyTable)
{
    const SmallTable table(std::span<const std::byte>{});

    EXPECT_EQ(table.Count(), 0u);
    EXPECT_FALSE(table.Record(0).has_value());
}
} // namespace
