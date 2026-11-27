#include "Player/ItemSetOptionList.h"

#include "Core/Expected.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

// 0x40d690 (equal to 0x40da20), 0x40d730 and 0x40d7f0 on [0x6eed3c]'s list (06, "Los ocho helpers de los stats
// propios"). The values come from the ASM checked against the PE c32bbd5f.
namespace
{
using Client::Player::ItemSetOptionList;

struct Entry
{
    std::uint8_t kind = 0;
    std::uint8_t flag = 0;
    std::int32_t value = 0;
};

// A list with the entries in +0x9348 (and nothing in +0x9349) and the marks at 0.
ItemSetOptionList ListOf(std::initializer_list<Entry> entries)
{
    ItemSetOptionList list;
    std::size_t index = 0;
    for (const Entry& entry : entries)
    {
        list.entries[index] = {entry.kind, entry.flag, std::uint8_t{0}};
        list.values[index] = entry.value;
        ++index;
    }
    list.count9348 = static_cast<std::uint8_t>(index);
    return list;
}

// The selector minus 0x8c against the first byte; each entry, once. The sum is signed 32 bits and the word gets its low
// 16.
TEST(ItemSetOptionListTest, ValuesAddOncePerEntryOfTheSelector)
{
    ItemSetOptionList list = ListOf({{0x05, 0, 7}, {0x06, 0, 100}, {0x05, 0, -3}});
    std::uint16_t word = 10;

    ASSERT_TRUE(Client::Player::AddItemSetOptionValues(list, word, 0x91).HasValue());
    EXPECT_EQ(word, 14);
    EXPECT_EQ(list.used, (std::array<std::uint8_t, 16>{1, 0, 1}));

    ASSERT_TRUE(Client::Player::AddItemSetOptionValues(list, word, 0x91).HasValue());
    EXPECT_EQ(word, 14);

    ASSERT_TRUE(Client::Player::AddItemSetOptionValues(list, word, 0x92).HasValue());
    EXPECT_EQ(word, 114);

    std::uint16_t wrapped = 0xfff0;
    ItemSetOptionList other = ListOf({{0x05, 0, 0x20}});
    ASSERT_TRUE(Client::Player::AddItemSetOptionValues(other, wrapped, 0x91).HasValue());
    EXPECT_EQ(wrapped, 0x10);
}

// The length is +0x9348 plus +0x9349, and the entries past it do not count.
TEST(ItemSetOptionListTest, TheLengthIsTheSumOfBothCounts)
{
    ItemSetOptionList list = ListOf({{0x05, 0, 1}, {0x05, 0, 2}, {0x05, 0, 4}});
    list.count9348 = 1;
    list.count9349 = 1;
    std::uint16_t word = 0;

    ASSERT_TRUE(Client::Player::AddItemSetOptionValues(list, word, 0x91).HasValue());

    EXPECT_EQ(word, 3);
    EXPECT_EQ(list.used[2], 0);
}

// With more than 16 the original would read the marks as entries: an error, with no marking or adding. 16 still fits.
TEST(ItemSetOptionListTest, MoreThanSixteenEntriesIsAnExplicitError)
{
    ItemSetOptionList list;
    list.entries.fill({0x05, 1, 0});
    list.values.fill(1);
    list.count9348 = 10;
    list.count9349 = 7;
    std::uint16_t word = 5;

    const auto added = Client::Player::AddItemSetOptionValues(list, word, 0x91);
    ASSERT_FALSE(added.HasValue());
    EXPECT_NE(added.Error().find("17 entradas"), std::string::npos);
    EXPECT_EQ(word, 5);
    EXPECT_EQ(list.used, (std::array<std::uint8_t, 16>{}));
    EXPECT_FALSE(Client::Player::ApplyItemSetOptionPercent(list, word, 0x91).HasValue());
    EXPECT_FALSE(Client::Player::AddItemSetOptionPercentOf(list, word, 100, 0x91).HasValue());

    list.count9349 = 6;
    ASSERT_TRUE(Client::Player::AddItemSetOptionValues(list, word, 0x91).HasValue());
    EXPECT_EQ(word, 21);
}

// 0x40d730: word += (word · S) / 100, the product in signed 32 bits and the division truncated toward 0.
TEST(ItemSetOptionListTest, ThePercentIsASigned32BitProductTruncatedTowardZero)
{
    ItemSetOptionList list = ListOf({{0x16, 0, 5}, {0x16, 0, 10}});
    std::uint16_t word = 1000;
    ASSERT_TRUE(Client::Player::ApplyItemSetOptionPercent(list, word, 0xa2).HasValue());
    EXPECT_EQ(word, 1150);

    list = ListOf({{0x16, 0, -3}});
    word = 1001;
    ASSERT_TRUE(Client::Player::ApplyItemSetOptionPercent(list, word, 0xa2).HasValue());
    EXPECT_EQ(word, 971); // −3003 / 100 = −30, not −31

    // 65535 · 65536 = 0xffff0000, which signed is −65536: −655.
    list = ListOf({{0x16, 0, 0x10000}});
    word = 0xffff;
    ASSERT_TRUE(Client::Player::ApplyItemSetOptionPercent(list, word, 0xa2).HasValue());
    EXPECT_EQ(word, 64880);
}

// 0x40d7f0: only the entries with the second byte other than 0; the last one's value, times the unsigned base, over
// 100, times how many there were. The one with the second byte at 0 is not marked.
TEST(ItemSetOptionListTest, ThePercentOfABaseTakesTheLastFlaggedValueTimesTheCount)
{
    ItemSetOptionList list = ListOf({{0x18, 1, 10}, {0x18, 0, 50}, {0x18, 2, 20}});
    std::uint16_t word = 3;

    ASSERT_TRUE(Client::Player::AddItemSetOptionPercentOf(list, word, 155, 0xa4).HasValue());

    EXPECT_EQ(word, 65); // 3 + (155 · 20 / 100) · 2
    EXPECT_EQ(list.used, (std::array<std::uint8_t, 16>{1, 0, 1}));

    list = ListOf({{0x18, 1, -7}});
    word = 100;
    ASSERT_TRUE(Client::Player::AddItemSetOptionPercentOf(list, word, 101, 0xa4).HasValue());
    EXPECT_EQ(word, 93); // −707 / 100 = −7
}

// The marks belong to the list: an entry one selector took is not taken by another function.
TEST(ItemSetOptionListTest, AnEntryTakenByOneFunctionIsNotTakenByAnother)
{
    ItemSetOptionList list = ListOf({{0x18, 1, 40}});
    std::uint16_t word = 0;
    ASSERT_TRUE(Client::Player::AddItemSetOptionValues(list, word, 0xa4).HasValue());
    EXPECT_EQ(word, 40);

    ASSERT_TRUE(Client::Player::AddItemSetOptionPercentOf(list, word, 1000, 0xa4).HasValue());
    ASSERT_TRUE(Client::Player::ApplyItemSetOptionPercent(list, word, 0xa4).HasValue());
    EXPECT_EQ(word, 40);
}

// 0x40d8c0, the magic's: the first byte minus 0x1d against the selector's low byte, with the second other than 0 and
// no mark; the 32-bit sum goes whole to the value, and with nothing it is untouched. With more than 16 entries, an
// error.
TEST(ItemSetOptionListTest, TheMagicSumTakesTheFlaggedEntriesOfTheSelectorPlus0x1d)
{
    ItemSetOptionList list = ListOf({{0x20, 1, 70000}, {0x20, 0, 5}, {0x21, 1, 9}, {0x20, 3, -1}});
    std::int32_t value = 12;

    ASSERT_TRUE(Client::Player::AddSkillSetOptionValues(list, value, 0x103).HasValue());

    EXPECT_EQ(value, 12 + 70000 - 1);
    EXPECT_EQ(list.used, (std::array<std::uint8_t, 16>{1, 0, 0, 1}));
    ASSERT_TRUE(Client::Player::AddSkillSetOptionValues(list, value, 3).HasValue());
    EXPECT_EQ(value, 12 + 70000 - 1);

    // A byte below 0x1d does not give the selector: the subtraction is 32 bits (0x10 − 0x1d is not 0xf3).
    list = ListOf({{0x10, 1, 4}});
    value = 0;
    ASSERT_TRUE(Client::Player::AddSkillSetOptionValues(list, value, 0xf3).HasValue());
    EXPECT_EQ(value, 0);

    list.count9348 = 10;
    list.count9349 = 7;
    const auto failed = Client::Player::AddSkillSetOptionValues(list, value, 3);
    ASSERT_FALSE(failed.HasValue());
    EXPECT_NE(failed.Error().find("0x40d8c0"), std::string::npos);
}
} // namespace
