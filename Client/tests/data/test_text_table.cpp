#include "Tables/TextTable.h"

#include "Crypto/Xor3.h"
#include "Data/DataError.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Client::Data::DataError;
using Client::Tables::LoadTextTable;
using Client::Tables::TextCount;
using Client::Tables::TextOr;
using Client::Tables::TextRecordSize;
using Client::Tables::TextTable;
using Client::Tables::TextTableSize;

// Text() can be asked of a table that stays alive, and not of a temporary.
template <typename Table>
concept ReadsText = requires(Table&& table) { std::forward<Table>(table).Text(0); };
static_assert(ReadsText<TextTable&>);
static_assert(ReadsText<const TextTable&>);
static_assert(!ReadsText<TextTable>);
static_assert(!ReadsText<const TextTable>);

template <typename Table>
concept ReadsTextOr = requires(Table&& table) { TextOr(std::forward<Table>(table), 0); };
static_assert(ReadsTextOr<TextTable&>);
static_assert(ReadsTextOr<const TextTable&>);
static_assert(!ReadsTextOr<TextTable>);
static_assert(!ReadsTextOr<const TextTable>);

// A plain table, with all records empty.
std::vector<std::byte> PlainTable(std::size_t size = TextTableSize)
{
    return std::vector<std::byte>(size);
}

void PutText(std::vector<std::byte>& table, std::size_t index, std::string_view text)
{
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        table[index * TextRecordSize + i] = static_cast<std::byte>(text[i]);
    }
}

// What the file would hold: the table encrypted with XOR3.
std::vector<std::byte> Encrypt(std::vector<std::byte> table)
{
    Client::Crypto::Xor3(table);
    return table;
}

TEST(TextTableTest, ReadsTheFirstAndTheLastText)
{
    std::vector<std::byte> table = PlainTable();
    PutText(table, 0, "el primero");
    PutText(table, TextCount - 1, "el último");

    const auto loaded = LoadTextTable(Encrypt(table));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Text(0), "el primero");
    EXPECT_EQ(loaded.Value().Text(TextCount - 1), "el último");
    EXPECT_EQ(loaded.Value().Text(1), "");
}

TEST(TextTableTest, TextEndsAtTheFirstNul)
{
    std::vector<std::byte> table = PlainTable();
    PutText(table, 3, std::string_view("corto\0resto viejo", 17));

    const auto loaded = LoadTextTable(Encrypt(table));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Text(3), "corto");
}

// Q-C07: a record without '\0' is a 300-byte text, and does not run into the next record. The original would read it
// as a C string and go on into the 8. The last record reaches the end of the file, so it also tests that it was
// decrypted whole.
TEST(TextTableTest, RecordWithoutNulIsAFullRecord)
{
    std::vector<std::byte> table = PlainTable();
    const std::string fullRecord(TextRecordSize, 'A');
    PutText(table, 7, fullRecord);
    PutText(table, 8, "el de al lado");
    PutText(table, TextCount - 1, fullRecord);

    const auto loaded = LoadTextTable(Encrypt(table));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Text(7), fullRecord);
    EXPECT_EQ(loaded.Value().Text(TextCount - 1), fullRecord);
}

// An index from the server can go past 2047: FUN_004da070 adds 2020 to a packet byte, so it reaches 2275, and the
// original would read past the table. There is no text either for an index that, times 300, wraps and would land
// inside, nor for the largest.
TEST(TextTableTest, IndexPastTheTableHasNoText)
{
    constexpr std::size_t Largest = std::numeric_limits<std::size_t>::max();
    const auto loaded = LoadTextTable(Encrypt(PlainTable()));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Text(TextCount), std::nullopt);
    EXPECT_EQ(loaded.Value().Text(2020 + 255), std::nullopt);
    EXPECT_EQ(loaded.Value().Text(Largest / TextRecordSize + 1), std::nullopt);
    EXPECT_EQ(loaded.Value().Text(Largest), std::nullopt);
}

// Taking the table out of an Expected moves it. The standard does not say how a moved std::vector is left (valid but
// unspecified; in practice, empty), so Text() looks at the records the table kept and does not read outside them.
TEST(TextTableTest, AMovedTableReadsOnlyWhatItKept)
{
    std::vector<std::byte> table = PlainTable();
    PutText(table, 0, "el primero");
    auto loaded = LoadTextTable(Encrypt(table));
    ASSERT_TRUE(loaded.HasValue());

    const TextTable kept = std::move(loaded).Value();
    const std::optional<std::string_view> rest = loaded.Value().Text(0);

    EXPECT_EQ(kept.Text(0), "el primero");
    EXPECT_TRUE(!rest.has_value() || rest->size() <= TextRecordSize);
}

// Question 2 of the plan. The original would read anyway and leave in the table whatever was in memory.
TEST(TextTableTest, ShortFileIsAnError)
{
    const auto loaded = LoadTextTable(Encrypt(PlainTable(TextTableSize - 1)));

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(loaded.Error(), DataError::FileTooShort);
}

// Like the original's fixed-size fread: even if they are enough for one more record, there is no text 2048.
TEST(TextTableTest, ExtraBytesAreIgnored)
{
    std::vector<std::byte> table = PlainTable(TextTableSize + TextRecordSize);
    PutText(table, TextCount - 1, "el último");
    PutText(table, TextCount, "de más");

    const auto loaded = LoadTextTable(Encrypt(table));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().Text(TextCount - 1), "el último");
    EXPECT_EQ(loaded.Value().Text(TextCount), std::nullopt);
}

TEST(TextOrTest, ReturnsTheTextOfAnIndexInTheTable)
{
    std::vector<std::byte> table = PlainTable();
    PutText(table, 3, "/guild");

    const auto loaded = LoadTextTable(Encrypt(table));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(TextOr(loaded.Value(), 3, "?"), "/guild");
    EXPECT_EQ(TextOr(loaded.Value(), 4, "?"), "");
}

TEST(TextOrTest, ReturnsTheFallbackForAnIndexPastTheTable)
{
    const auto loaded = LoadTextTable(Encrypt(PlainTable()));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(TextOr(loaded.Value(), TextCount, "?"), "?");
    EXPECT_EQ(TextOr(loaded.Value(), TextCount), "");
}
} // namespace
