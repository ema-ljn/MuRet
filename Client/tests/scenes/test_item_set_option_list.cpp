#include "Scenes/ItemSetOptionList.h"

#include "Core/Expected.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

// 0x40d690 (igual a 0x40da20), 0x40d730 y 0x40d7f0 sobre la lista de [0x6eed3c] (06, "Los ocho helpers de
// los stats propios"). Los valores salen del ASM cotejado con el PE c32bbd5f.
namespace
{
using Client::Scenes::ItemSetOptionList;

struct Entry
{
    std::uint8_t kind = 0;
    std::uint8_t flag = 0;
    std::int32_t value = 0;
};

// Una lista con las entradas en +0x9348 (y nada en +0x9349) y las marcas en 0.
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

// El selector menos 0x8c contra el primer byte; cada entrada, una sola vez. La suma es de 32 bits con
// signo y a la palabra van sus 16 de abajo.
TEST(ItemSetOptionListTest, ValuesAddOncePerEntryOfTheSelector)
{
    ItemSetOptionList list = ListOf({{0x05, 0, 7}, {0x06, 0, 100}, {0x05, 0, -3}});
    std::uint16_t word = 10;

    ASSERT_TRUE(Client::Scenes::AddItemSetOptionValues(list, word, 0x91).HasValue());
    EXPECT_EQ(word, 14);
    EXPECT_EQ(list.used, (std::array<std::uint8_t, 16>{1, 0, 1}));

    ASSERT_TRUE(Client::Scenes::AddItemSetOptionValues(list, word, 0x91).HasValue());
    EXPECT_EQ(word, 14);

    ASSERT_TRUE(Client::Scenes::AddItemSetOptionValues(list, word, 0x92).HasValue());
    EXPECT_EQ(word, 114);

    std::uint16_t wrapped = 0xfff0;
    ItemSetOptionList other = ListOf({{0x05, 0, 0x20}});
    ASSERT_TRUE(Client::Scenes::AddItemSetOptionValues(other, wrapped, 0x91).HasValue());
    EXPECT_EQ(wrapped, 0x10);
}

// El largo es +0x9348 más +0x9349, y las entradas que pasan de ahí no cuentan.
TEST(ItemSetOptionListTest, TheLengthIsTheSumOfBothCounts)
{
    ItemSetOptionList list = ListOf({{0x05, 0, 1}, {0x05, 0, 2}, {0x05, 0, 4}});
    list.count9348 = 1;
    list.count9349 = 1;
    std::uint16_t word = 0;

    ASSERT_TRUE(Client::Scenes::AddItemSetOptionValues(list, word, 0x91).HasValue());

    EXPECT_EQ(word, 3);
    EXPECT_EQ(list.used[2], 0);
}

// Con más de 16 el original leería las marcas como entradas: error, sin marcar ni sumar. 16 todavía
// entra.
TEST(ItemSetOptionListTest, MoreThanSixteenEntriesIsAnExplicitError)
{
    ItemSetOptionList list;
    list.entries.fill({0x05, 1, 0});
    list.values.fill(1);
    list.count9348 = 10;
    list.count9349 = 7;
    std::uint16_t word = 5;

    const auto added = Client::Scenes::AddItemSetOptionValues(list, word, 0x91);
    ASSERT_FALSE(added.HasValue());
    EXPECT_NE(added.Error().find("17 entradas"), std::string::npos);
    EXPECT_EQ(word, 5);
    EXPECT_EQ(list.used, (std::array<std::uint8_t, 16>{}));
    EXPECT_FALSE(Client::Scenes::ApplyItemSetOptionPercent(list, word, 0x91).HasValue());
    EXPECT_FALSE(Client::Scenes::AddItemSetOptionPercentOf(list, word, 100, 0x91).HasValue());

    list.count9349 = 6;
    ASSERT_TRUE(Client::Scenes::AddItemSetOptionValues(list, word, 0x91).HasValue());
    EXPECT_EQ(word, 21);
}

// 0x40d730: word += (word · S) / 100, el producto en 32 bits con signo y la división truncada hacia 0.
TEST(ItemSetOptionListTest, ThePercentIsASigned32BitProductTruncatedTowardZero)
{
    ItemSetOptionList list = ListOf({{0x16, 0, 5}, {0x16, 0, 10}});
    std::uint16_t word = 1000;
    ASSERT_TRUE(Client::Scenes::ApplyItemSetOptionPercent(list, word, 0xa2).HasValue());
    EXPECT_EQ(word, 1150);

    list = ListOf({{0x16, 0, -3}});
    word = 1001;
    ASSERT_TRUE(Client::Scenes::ApplyItemSetOptionPercent(list, word, 0xa2).HasValue());
    EXPECT_EQ(word, 971); // −3003 / 100 = −30, no −31

    // 65535 · 65536 = 0xffff0000, que con signo es −65536: −655.
    list = ListOf({{0x16, 0, 0x10000}});
    word = 0xffff;
    ASSERT_TRUE(Client::Scenes::ApplyItemSetOptionPercent(list, word, 0xa2).HasValue());
    EXPECT_EQ(word, 64880);
}

// 0x40d7f0: solo las entradas con el segundo byte distinto de 0; el valor de la última, por la base sin
// signo, sobre 100, por cuántas fueron. La que tiene el segundo byte en 0 no queda marcada.
TEST(ItemSetOptionListTest, ThePercentOfABaseTakesTheLastFlaggedValueTimesTheCount)
{
    ItemSetOptionList list = ListOf({{0x18, 1, 10}, {0x18, 0, 50}, {0x18, 2, 20}});
    std::uint16_t word = 3;

    ASSERT_TRUE(Client::Scenes::AddItemSetOptionPercentOf(list, word, 155, 0xa4).HasValue());

    EXPECT_EQ(word, 65); // 3 + (155 · 20 / 100) · 2
    EXPECT_EQ(list.used, (std::array<std::uint8_t, 16>{1, 0, 1}));

    list = ListOf({{0x18, 1, -7}});
    word = 100;
    ASSERT_TRUE(Client::Scenes::AddItemSetOptionPercentOf(list, word, 101, 0xa4).HasValue());
    EXPECT_EQ(word, 93); // −707 / 100 = −7
}

// Las marcas son de la lista: una entrada que tomó un selector no la toma otra función.
TEST(ItemSetOptionListTest, AnEntryTakenByOneFunctionIsNotTakenByAnother)
{
    ItemSetOptionList list = ListOf({{0x18, 1, 40}});
    std::uint16_t word = 0;
    ASSERT_TRUE(Client::Scenes::AddItemSetOptionValues(list, word, 0xa4).HasValue());
    EXPECT_EQ(word, 40);

    ASSERT_TRUE(Client::Scenes::AddItemSetOptionPercentOf(list, word, 1000, 0xa4).HasValue());
    ASSERT_TRUE(Client::Scenes::ApplyItemSetOptionPercent(list, word, 0xa4).HasValue());
    EXPECT_EQ(word, 40);
}
} // namespace
