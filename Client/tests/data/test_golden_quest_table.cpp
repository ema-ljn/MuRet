#include "Data/Tables/QuestTable.h"
#include "Data/Text/DialogTable.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// quest.bmd y Dialog.bmd reales (Data\Local, 0x418d10 y 0x54b2b0 desde 0x61ccb0): las cuatro misiones, sus pedidos y
// condiciones, y que cada diálogo que nombran tenga texto.
namespace
{
using Client::Data::Tables::LoadQuestTable;
using Client::Data::Tables::NameOf;
using Client::Data::Text::DialogTextOf;
using Client::Data::Text::LoadDialogTable;
using Client::Tests::ReadFileBytes;
using GoldenQuestTable = Client::Tests::GoldenDataTest;

TEST_F(GoldenQuestTable, TheRealFileHasFourQuests)
{
    const auto path = DataFile("Local\\quest.bmd");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    const auto loaded = LoadQuestTable(std::as_bytes(std::span(*file)));
    ASSERT_TRUE(loaded.HasValue());
    const auto& quests = loaded.Value();

    EXPECT_EQ(NameOf(quests[0]), " Find the 'Scroll of Emperor'!");
    EXPECT_EQ(quests[0].entryCount, 2);
    EXPECT_EQ(quests[0].conditionCount, 3);
    EXPECT_EQ(quests[0].npcType, 0xeb);
    EXPECT_EQ(NameOf(quests[1]), " Three Treasures of Mu");
    EXPECT_EQ(quests[1].entryCount, 3);
    EXPECT_EQ(quests[1].conditionCount, 1);
    EXPECT_EQ(NameOf(quests[2]), "Gain'Hero status'!");
    EXPECT_EQ(quests[2].npcType, 0xe5);
    EXPECT_EQ(NameOf(quests[3]), "Secret of'Dark Stone'");
    EXPECT_EQ(quests[3].entryCount, 1);
    for (std::size_t i = 4; i < quests.size(); ++i)
        EXPECT_EQ(quests[i], Client::Data::Tables::QuestRecord{}) << i;

    // La primera: el pergamino (14/23) para las tres primeras clases con la condición 0, y para las dos últimas con la
    // 1. Las condiciones: la misión 0 terminada y nivel 10.000 para el enlace 1; un millón de zen y nivel 150 para
    // todos.
    const auto& first = quests[0].entries[0];
    EXPECT_EQ(first.kind, 1);
    EXPECT_EQ(first.itemGroup, 14);
    EXPECT_EQ(first.itemIndex, 23);
    EXPECT_EQ(first.itemCount, 1);
    EXPECT_EQ(first.condition, 0);
    EXPECT_EQ(first.classes, (std::array<std::uint8_t, 5>{1, 1, 1, 0, 0}));
    EXPECT_EQ(first.dialogs, (std::array<std::int16_t, 4>{52, 54, 55, 56}));
    EXPECT_EQ(quests[0].entries[1].classes, (std::array<std::uint8_t, 5>{0, 0, 0, 1, 1}));
    EXPECT_EQ(quests[0].conditions[0].link, 1);
    EXPECT_EQ(quests[0].conditions[0].needQuest, 0);
    EXPECT_EQ(quests[0].conditions[0].minLevel, 10000);
    EXPECT_EQ(quests[0].conditions[0].failDialog, 73);
    EXPECT_EQ(quests[0].conditions[1].link, 0xff);
    EXPECT_EQ(quests[0].conditions[1].zen, 1000000u);
    EXPECT_EQ(quests[0].conditions[1].failDialog, 51);
    EXPECT_EQ(quests[0].conditions[2].minLevel, 150);
    // La tercera pide el nivel del ítem 1 y la segunda misión terminada.
    EXPECT_EQ(quests[2].entries[0].itemLevel, 1);
    EXPECT_EQ(quests[2].conditions[1].needQuest, 1);
    EXPECT_EQ(quests[2].conditions[0].minLevel, 220);

    const auto dialogPath = DataFile("Local\\Dialog.bmd");
    ASSERT_TRUE(dialogPath.has_value());
    const std::optional<std::vector<char>> dialogFile = ReadFileBytes(*dialogPath);
    ASSERT_TRUE(dialogFile.has_value());
    const auto dialogs = LoadDialogTable(std::as_bytes(std::span(*dialogFile)));
    ASSERT_TRUE(dialogs.HasValue());
    for (std::size_t q = 0; q < 4; ++q)
    {
        for (std::size_t e = 0; e < static_cast<std::size_t>(quests[q].entryCount); ++e)
        {
            for (const std::int16_t dialog : quests[q].entries[e].dialogs)
                EXPECT_FALSE(DialogTextOf(dialogs.Value()[static_cast<std::size_t>(dialog)]).empty()) << dialog;
        }
        for (std::size_t c = 0; c < static_cast<std::size_t>(quests[q].conditionCount); ++c)
        {
            const auto dialog = static_cast<std::size_t>(quests[q].conditions[c].failDialog);
            EXPECT_FALSE(DialogTextOf(dialogs.Value()[dialog]).empty()) << dialog;
        }
    }
}

TEST_F(GoldenQuestTable, TheRealDialogsHaveAtMostThreeAnswers)
{
    const auto path = DataFile("Local\\Dialog.bmd");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    const auto loaded = LoadDialogTable(std::as_bytes(std::span(*file)));
    ASSERT_TRUE(loaded.HasValue());
    std::size_t withText = 0;
    for (const auto& dialog : loaded.Value())
    {
        EXPECT_LE(dialog.answerCount, 3);
        if (!DialogTextOf(dialog).empty())
            ++withText;
    }
    EXPECT_EQ(withText, 75u);
    // El 54: una respuesta que lleva al 54 y lo cierra (acción 2); el 50, una que lleva al 74 sin acción.
    EXPECT_EQ(loaded.Value()[54].answerCount, 1);
    EXPECT_EQ(loaded.Value()[54].next[0], 54);
    EXPECT_EQ(loaded.Value()[54].actions[0], 2);
    EXPECT_EQ(loaded.Value()[50].next[0], 74);
    EXPECT_EQ(loaded.Value()[50].actions[0], -1);
}
} // namespace
