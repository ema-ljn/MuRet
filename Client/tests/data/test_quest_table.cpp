#include "Data/Tables/QuestTable.h"

#include "Core/Xor3.h"
#include "Data/DataError.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Client::Data::Tables::LoadQuestTable: quest.bmd (0x418d10), 200 registros de 0x2a8 con el XOR3 de 0x418de0 desde el
// primer byte de cada uno. Las misiones son inventadas.
namespace
{
using Client::Data::DataError;
using Client::Data::Tables::LoadQuestTable;
using Client::Data::Tables::NameOf;
using Client::Data::Tables::QuestCount;
using Client::Data::Tables::QuestRecordSize;
using Client::Data::Tables::QuestTableSize;

void Put(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i)
        bytes[offset + i] = static_cast<std::byte>((value >> (8 * i)) & 0xffu);
}

std::vector<std::byte> EncryptedFile(std::vector<std::byte> plain)
{
    for (std::size_t i = 0; i < QuestCount; ++i)
        Client::Core::Xor3(std::span(plain).subspan(i * QuestRecordSize, QuestRecordSize));
    return plain;
}

TEST(QuestTable, ReadsEachFieldOfARecordAtItsOffset)
{
    std::vector<std::byte> plain(QuestTableSize);
    const std::span<std::byte> record = std::span(plain).subspan(7 * QuestRecordSize, QuestRecordSize);
    Put(record, 0, 2, 2);
    Put(record, 2, 0xffff, 2);
    Put(record, 4, 0xe1, 1);
    const char name[] = "Una prueba";
    for (std::size_t i = 0; i + 1 < sizeof(name); ++i)
        record[5 + i] = static_cast<std::byte>(name[i]);
    // El segundo pedido (+0x26 + 0x14): ítems 3/4, nivel 5, 6 de ellos, condición 9, la clase 2 y los diálogos.
    const std::size_t entry = 0x26 + 0x14;
    Put(record, entry + 1, 1, 1);
    Put(record, entry + 2, 3, 1);
    Put(record, entry + 3, 4, 1);
    Put(record, entry + 4, 5, 1);
    Put(record, entry + 5, 6, 1);
    Put(record, entry + 6, 9, 1);
    Put(record, entry + 9, 2, 1);
    Put(record, entry + 0xc, 11, 2);
    Put(record, entry + 0xe, 12, 2);
    Put(record, entry + 0x10, 13, 2);
    Put(record, entry + 0x12, 0xfffe, 2);
    // La última condición (+0x168 + 15 · 0x14).
    const std::size_t condition = 0x168 + 15 * 0x14;
    Put(record, condition + 1, 0xff, 1);
    Put(record, condition + 2, 0xffff, 2);
    Put(record, condition + 4, 0xfffe, 2);
    Put(record, condition + 6, 40, 2);
    Put(record, condition + 0xc, 0x89abcdef, 4);
    Put(record, condition + 0x10, 77, 2);

    const auto loaded = LoadQuestTable(EncryptedFile(plain));
    ASSERT_TRUE(loaded.HasValue());
    ASSERT_EQ(loaded.Value().size(), QuestCount);
    const auto& quest = loaded.Value()[7];
    EXPECT_EQ(quest.entryCount, 2);
    EXPECT_EQ(quest.conditionCount, -1);
    EXPECT_EQ(quest.npcType, 0xe1);
    EXPECT_EQ(NameOf(quest), "Una prueba");
    const auto& second = quest.entries[1];
    EXPECT_EQ(second.kind, 1);
    EXPECT_EQ(second.itemGroup, 3);
    EXPECT_EQ(second.itemIndex, 4);
    EXPECT_EQ(second.itemLevel, 5);
    EXPECT_EQ(second.itemCount, 6);
    EXPECT_EQ(second.condition, 9);
    EXPECT_EQ(second.classes[2], 2);
    EXPECT_EQ(second.classes[0], 0);
    EXPECT_EQ(second.dialogs[0], 11);
    EXPECT_EQ(second.dialogs[3], -2);
    const auto& last = quest.conditions[15];
    EXPECT_EQ(last.link, 0xff);
    EXPECT_EQ(last.needQuest, -1);
    EXPECT_EQ(last.minLevel, 0xfffe);
    EXPECT_EQ(last.maxLevel, 40);
    EXPECT_EQ(last.zen, 0x89abcdefu);
    EXPECT_EQ(last.failDialog, 77);
    EXPECT_EQ(loaded.Value()[6], Client::Data::Tables::QuestRecord{});
}

TEST(QuestTable, ANameWithoutZeroKeepsItsThirtyTwoBytes)
{
    std::vector<std::byte> plain(QuestTableSize);
    for (std::size_t i = 0; i < 0x20; ++i)
        plain[5 + i] = std::byte{'a'};
    const auto loaded = LoadQuestTable(EncryptedFile(plain));
    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(NameOf(loaded.Value()[0]).size(), 0x20u);
}

TEST(QuestTable, AShortFileIsAnError)
{
    const std::vector<std::byte> file(QuestTableSize - 1);
    const auto loaded = LoadQuestTable(file);
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(loaded.Error(), DataError::FileTooShort);
}
} // namespace
