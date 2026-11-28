#include "Quests/Quests.h"

#include "Crt/CrtRandom.h"
#include "Tables/QuestTable.h"
#include "Tables/DialogTable.h"
#include "Tables/TextTable.h"
#include "Kernel/ActorState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Quests/QuestState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameDialogs.h"
#include "Kernel/ItemState.h"

#include "support/PlayerActors.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Client::Quests: the functions of the quest object [0x710148] that do not draw (0x4195c0, 0x418e20, 0x418ee0,
// 0x418f40, 0x418fb0, 0x4194c0, 0x419600, 0x419b10, 0x4198b0, 0x419840 and 0x558410). The quests, dialogs and texts
// are made up.
namespace
{
using Client::Quests::QuestContext;
using Client::Quests::QuestState;
using Client::Quests::QuestStateOf;

constexpr std::uint32_t Seed = 0x5eed;
// An own record closed down to 0 spends 3848 rand() (0x40b4d0).
constexpr int OwnRecordClose = 0xf08;
// Quest 0's made-up scroll.
constexpr std::int32_t ScrollType = 14 * 0x200 + 23;

void PutText(std::array<char, Client::Tables::DialogTextSize>& text, std::string_view value)
{
    std::ranges::copy(value, text.begin());
}

struct Quests
{
    Quests()
    {
        world.Runtime().hero = 3;
        Hero().alive = true;
        Hero().characterClass = 0;
        // Quest 0: an item request for class 0 with condition 0 (level 10 to 0 and 1000 zen).
        Client::Tables::QuestRecord& first = Quest().records[0];
        first.entryCount = 1;
        first.conditionCount = 1;
        auto& entry = first.entries[0];
        entry.byte0 = 1;
        entry.kind = 1;
        entry.itemGroup = 14;
        entry.itemIndex = 23;
        entry.itemLevel = 0;
        entry.itemCount = 1;
        entry.condition = 0;
        entry.classes = {1, 0, 0, 0, 0};
        entry.dialogs = {10, 11, 12, 13};
        auto& condition = first.conditions[0];
        condition.byte0 = 1;
        condition.link = 0;
        condition.needQuest = -1;
        condition.minLevel = 10;
        condition.zen = 1000;
        condition.failDialog = 20;
        // Quest 1: a request for class 1 and nothing else.
        Client::Tables::QuestRecord& second = Quest().records[1];
        second.entryCount = 1;
        second.entries[0].classes = {0, 1, 0, 0, 0};
        second.entries[0].dialogs = {30, 31, 32, 33};

        world.Runtime().dialogTable.assign(Client::Tables::DialogCount, {});
        for (const std::int32_t index : {10, 11, 12, 13, 20, 30, 33})
            PutText(world.Runtime().dialogTable[static_cast<std::size_t>(index)].text, "Un texto inventado");
        auto& withAnswers = world.Runtime().dialogTable[10];
        withAnswers.answerCount = 2;
        std::ranges::copy(std::string_view("Acepto"), withAnswers.answers[0].begin());
        std::ranges::copy(std::string_view("Ahora no"), withAnswers.answers[1].begin());
        world.OwnCharacter().level = 20;
        world.OwnCharacter().zen = 5000;
    }
    Client::Kernel::ActorState& Hero()
    {
        return world.Hero();
    }
    QuestState& Quest()
    {
        return world.Runtime().quest;
    }
    QuestContext Context()
    {
        return QuestContext{.world = world, .random = random, .texts = &texts};
    }
    void GiveTheScroll(std::size_t cell, std::uint32_t level)
    {
        Client::Kernel::ItemState& item = world.Inventory().cells[cell];
        item.id = static_cast<std::uint16_t>(ScrollType);
        item.occupied = 1;
        item.scalar1Low = level << 3;
    }
    Client::Kernel::WorldState world;
    Client::Crt::CrtRandom random{Seed};
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{609, "Cerrar"}});
};

std::unique_ptr<Quests> MakeQuests()
{
    return std::make_unique<Quests>();
}

TEST(Quests, TheStateOfAQuestIsReadShiftedLikeTheOriginal)
{
    const auto runtime = std::make_unique<Client::Kernel::WorldRuntime>();
    QuestState& quest = runtime->quest;
    // Byte 0: quests 0 to 3 where the GS writes them (2 bits each, quest 0 at the bottom).
    quest.states[0] = 0b10'01'11'00;
    quest.states[1] = 0b11'00'00'10;
    EXPECT_EQ(QuestStateOf(*runtime, 0), 0);
    EXPECT_EQ(QuestStateOf(*runtime, 1), 3);
    EXPECT_EQ(QuestStateOf(*runtime, 2), 1);
    EXPECT_EQ(QuestStateOf(*runtime, 3), 2);
    // Q-C841: quest 4 reads byte 1 shifted (4 − 1) · 2 = 6 bits; from quest 5, the count reaches 8 and gives 0.
    EXPECT_EQ(QuestStateOf(*runtime, 4), 3);
    EXPECT_EQ(QuestStateOf(*runtime, 5), 0);
    EXPECT_EQ(QuestStateOf(*runtime, 8), 0);
    // −1: byte 0 (the signed division) shifted 30 bits; outside the 64 bytes, 0.
    EXPECT_EQ(QuestStateOf(*runtime, -1), 0);
    EXPECT_EQ(QuestStateOf(*runtime, 200), 0);
    EXPECT_EQ(QuestStateOf(*runtime, -5), 0);
    // Quest 236 reads the padding at +0x21387 (byte 59) shifted (236 − 59) & 0xf · 2 = 2 bits.
    quest.byte21387 = 0b0000'1100;
    EXPECT_EQ(QuestStateOf(*runtime, 236), 3);

    quest.current = 3;
    Client::Quests::ReadTheCurrentQuestState(*runtime);
    EXPECT_EQ(quest.currentState, 2);
}

TEST(Quests, MissingItemsCountTheOccupiedCellsOfTheTypeAndLevel)
{
    auto tested = MakeQuests();
    tested->GiveTheScroll(0, 1);
    tested->GiveTheScroll(40, 0);
    tested->GiveTheScroll(63, 1);
    tested->world.Inventory().cells[63].occupied = 0;
    const auto& inventory = tested->world.Inventory();
    EXPECT_EQ(Client::Quests::MissingQuestItems(inventory, ScrollType, 2, 1), 1);
    EXPECT_EQ(Client::Quests::MissingQuestItems(inventory, ScrollType, 2, -1), 0);
    EXPECT_EQ(Client::Quests::MissingQuestItems(inventory, ScrollType, 1, 0), 0);
    EXPECT_EQ(Client::Quests::MissingQuestItems(inventory, ScrollType + 1, 3, -1), 3);
    // A count of 0 with none gives 0; with one, too (it reaches the count).
    EXPECT_EQ(Client::Quests::MissingQuestItems(inventory, ScrollType + 1, 0, -1), 0);
}

TEST(Quests, TheListOfTheA0CopiesTheStatesAndFindsTheFirstUnfinished)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.states.fill(0xff);
    tested->Hero().characterClass = 0x13;
    tested->Hero().byte390 = 1;
    tested->world.Runtime().byte6eeda8 = 1;
    const std::array<std::uint8_t, 2> bytes{0b00'00'10'10, 0x40};
    EXPECT_EQ(Client::Quests::ApplyTheQuestList(tested->world, 2, bytes), 0u);
    EXPECT_EQ(quest.characterClass, 3);
    EXPECT_EQ(quest.evolution, 2);
    EXPECT_EQ(quest.refreshClass, 1);
    EXPECT_EQ(tested->world.Runtime().byte6eeda8, 0);
    EXPECT_EQ(quest.states[0], 0b00'00'10'10);
    EXPECT_EQ(quest.states[1], 0x40);
    EXPECT_TRUE(std::ranges::all_of(std::span(quest.states).subspan(2), [](std::uint8_t b) { return b == 0; }));
    // The byte count is also the count of quests it looks at: the first two are done.
    EXPECT_EQ(quest.current, 2);
    EXPECT_EQ(quest.reached, 2);
    EXPECT_EQ(tested->Hero().byte390, 0);
}

TEST(Quests, TheThirdQuestDoneMarksTheHeroAndAShortA0IsFilledWithZeros)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    // Q-C840 and P-C171: the count asks for 3 bytes and the packet carries 1; what is missing goes at 0.
    const std::array<std::uint8_t, 1> bytes{0b10'10'10'10};
    EXPECT_EQ(Client::Quests::ApplyTheQuestList(tested->world, 3, bytes), 0u);
    EXPECT_EQ(quest.states[0], 0b10'10'10'10);
    EXPECT_EQ(quest.states[1], 0);
    EXPECT_EQ(quest.states[2], 0);
    // The three it looks at are done: the first one not done is the count.
    EXPECT_EQ(quest.current, 3);
    EXPECT_EQ(tested->Hero().byte390, 1);
}

// Q-C1126: the A0's copy (0x418e20) goes on past the 50 bytes through the object's fields up to its end (+0x21398):
// +0x2137e.., the window of +0x21383, the dialog, the zen, the tab, +0x2138d (also the 1 just set) and the corner.
// Then, the first one not done (with what the copy left) in +0x2137e and +0x2137f. What goes past the object is not
// written and is counted.
TEST(Quests, AnA0OfMoreThanFiftyBytesWritesTheObjectUpToItsEnd)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    std::vector<std::uint8_t> bytes(80, 0);
    // 0xaa: quests 0 to 4 done (Q-C841) and 5 not.
    std::fill_n(bytes.begin(), 50, std::uint8_t{0xaa});
    for (std::size_t i = 50; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(i);
    EXPECT_EQ(Client::Quests::ApplyTheQuestList(tested->world, 80, bytes), 4u);
    EXPECT_EQ(quest.states[49], 0xaa);
    EXPECT_EQ(quest.current, 5);
    EXPECT_EQ(quest.reached, 5);
    EXPECT_EQ(quest.byte21380, 52);
    EXPECT_EQ(quest.byte21381, 53);
    EXPECT_EQ(quest.byte21382, 54);
    EXPECT_EQ(tested->world.Runtime().panels.delegated.quest, 55);
    EXPECT_EQ(quest.dialog, 0x3938);
    EXPECT_EQ(quest.currentState, 58);
    EXPECT_EQ(quest.byte21387, 59);
    EXPECT_EQ(quest.zen, 0x3f3e3d3cu);
    EXPECT_EQ(quest.tab, 64);
    EXPECT_EQ(quest.refreshClass, 65);
    EXPECT_EQ(quest.x, 0x47464544);
    EXPECT_EQ(quest.y, 0x4b4a4948);
    EXPECT_EQ(tested->Hero().byte390, 1);
    // Up to the exact end, nothing outside.
    EXPECT_EQ(Client::Quests::ApplyTheQuestList(tested->world, 76, bytes), 0u);
}

TEST(Quests, TheA1WritesTheWholeByteAndKeepsTheHighest)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.reached = 9;
    quest.states[1] = 0xff;
    // Q-C843: the whole byte of the four quests, not quest 5's two bits.
    Client::Quests::SetTheQuestState(tested->world, 5, 0x04);
    EXPECT_EQ(quest.current, 5);
    EXPECT_EQ(quest.reached, 9);
    EXPECT_EQ(quest.states[1], 0x04);
    Client::Quests::SetTheQuestState(tested->world, 12, 0x80);
    EXPECT_EQ(quest.reached, 12);
    EXPECT_EQ(tested->Hero().byte390, 0);
    Client::Quests::SetTheQuestState(tested->world, 3, 0x80);
    EXPECT_EQ(tested->Hero().byte390, 1);
}

// Q-C1126: from quest 200 on, 0x418ee0 writes the state in the fields that follow the 50 bytes, after setting
// +0x2137e and +0x2137f: 201 overwrites the current quest just set, 204 the highest, 222 the window and 255 the zen's
// high byte.
TEST(Quests, TheA1PastTheTwoHundredWritesTheFieldsThatFollow)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    Client::Quests::SetTheQuestState(tested->world, 201, 7);
    EXPECT_EQ(quest.current, 7);
    EXPECT_EQ(quest.reached, 201);
    Client::Quests::SetTheQuestState(tested->world, 204, 3);
    EXPECT_EQ(quest.current, 204);
    EXPECT_EQ(quest.reached, 3);
    Client::Quests::SetTheQuestState(tested->world, 222, 2);
    EXPECT_EQ(tested->world.Runtime().panels.delegated.quest, 2);
    EXPECT_EQ(quest.reached, 222);
    quest.zen = 0x00123456;
    Client::Quests::SetTheQuestState(tested->world, 255, 0x01);
    EXPECT_EQ(quest.zen, 0x01123456u);
    EXPECT_EQ(QuestStateOf(tested->world.Runtime(), 255), 1);
}

TEST(Quests, AnAvailableQuestChecksItsConditionsWithTheOwnRecordOpen)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.characterClass = 0;
    quest.states[0] = Client::Quests::QuestAvailable;
    const auto refreshed = Client::Quests::RefreshTheQuest(tested->Context(), 0xff);
    ASSERT_TRUE(refreshed.HasValue());
    EXPECT_EQ(refreshed.Value(), Client::Quests::QuestAvailable);
    EXPECT_EQ(quest.dialog, 10);
    // Without the zen: it stores it without opening the record. The minimum level, with an access that closes it.
    EXPECT_EQ(quest.zen, 1000u);
    EXPECT_EQ(tested->random.State(), Client::Tests::AfterDraws(Seed, OwnRecordClose).State());
}

TEST(Quests, AFailedConditionLeavesItsDialogAndFive)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.characterClass = 0;
    tested->world.OwnCharacter().level = 9;
    const auto refreshed = Client::Quests::RefreshTheQuest(tested->Context(), Client::Quests::QuestAvailable);
    ASSERT_TRUE(refreshed.HasValue());
    EXPECT_EQ(refreshed.Value(), Client::Quests::QuestConditionFailed);
    EXPECT_EQ(quest.dialog, 20);
    EXPECT_EQ(tested->random.State(), Client::Tests::AfterDraws(Seed, OwnRecordClose).State());
}

TEST(Quests, TheZenIsReadWithTheRecordOpenOnlyWhenAsked)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.characterClass = 0;
    tested->world.OwnCharacter().zen = 999;
    Client::Tables::QuestCondition& condition = quest.records[0].conditions[0];
    condition.maxLevel = 30;
    const auto checked = Client::Quests::CheckTheQuestConditions(tested->Context(), 0, true);
    ASSERT_TRUE(checked.HasValue());
    EXPECT_FALSE(checked.Value());
    EXPECT_EQ(quest.zen, 1000u);
    EXPECT_EQ(quest.dialog, 20);
    EXPECT_EQ(quest.currentState, Client::Quests::QuestConditionFailed);
    // The minimum, the maximum and the zen: three accesses that close the record.
    EXPECT_EQ(tested->random.State(), Client::Tests::AfterDraws(Seed, 3 * OwnRecordClose).State());

    tested->world.OwnCharacter().zen = 1000;
    const auto passed = Client::Quests::CheckTheQuestConditions(tested->Context(), 0, true);
    ASSERT_TRUE(passed.HasValue());
    EXPECT_TRUE(passed.Value());
    // The maximum is compared unsigned: 31 exceeds it.
    tested->world.OwnCharacter().level = 31;
    const auto over = Client::Quests::CheckTheQuestConditions(tested->Context(), 0, false);
    ASSERT_TRUE(over.HasValue());
    EXPECT_FALSE(over.Value());
}

TEST(Quests, AConditionNeedsItsQuestDone)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.characterClass = 0;
    quest.records[0].conditions[0].needQuest = 1;
    quest.records[0].conditions[0].minLevel = 0;
    const auto failed = Client::Quests::CheckTheQuestConditions(tested->Context(), 0, false);
    ASSERT_TRUE(failed.HasValue());
    EXPECT_FALSE(failed.Value());
    EXPECT_EQ(tested->random.State(), Client::Crt::CrtRandom(Seed).State());
    quest.states[0] = 0b10'00;
    const auto passed = Client::Quests::CheckTheQuestConditions(tested->Context(), 0, false);
    ASSERT_TRUE(passed.HasValue());
    EXPECT_TRUE(passed.Value());
}

TEST(Quests, AQuestInProgressShowsWhetherTheItemsAreThere)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.characterClass = 0;
    quest.currentState = Client::Quests::QuestInProgress;
    const auto missing = Client::Quests::RefreshTheQuest(tested->Context(), Client::Quests::QuestInProgress);
    ASSERT_TRUE(missing.HasValue());
    EXPECT_EQ(quest.dialog, 11);
    const auto none = Client::Quests::CanCompleteTheQuest(tested->world);
    ASSERT_TRUE(none.HasValue());
    EXPECT_FALSE(none.Value());

    tested->GiveTheScroll(5, 0);
    const auto found = Client::Quests::RefreshTheQuest(tested->Context(), Client::Quests::QuestInProgress);
    ASSERT_TRUE(found.HasValue());
    EXPECT_EQ(found.Value(), Client::Quests::QuestInProgress);
    EXPECT_EQ(quest.dialog, 12);
    const auto complete = Client::Quests::CanCompleteTheQuest(tested->world);
    ASSERT_TRUE(complete.HasValue());
    EXPECT_TRUE(complete.Value());
    // Without item requests for the class, no.
    quest.records[0].entries[0].kind = 0;
    const auto noItems = Client::Quests::CanCompleteTheQuest(tested->world);
    ASSERT_TRUE(noItems.HasValue());
    EXPECT_FALSE(noItems.Value());
    EXPECT_EQ(tested->random.State(), Client::Crt::CrtRandom(Seed).State());
}

TEST(Quests, ADoneQuestShowsItsLastDialog)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.characterClass = 0;
    const auto refreshed = Client::Quests::RefreshTheQuest(tested->Context(), Client::Quests::QuestDone);
    ASSERT_TRUE(refreshed.HasValue());
    EXPECT_EQ(quest.dialog, 13);
}

TEST(Quests, WithoutAnEntryForTheClassTheQuestGoesBackOne)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.characterClass = 0;
    quest.current = 1;
    quest.states[0] = 0b00'10;
    // Quest 1 has no request for class 0: −−+0x2137e and 0x419600(0xff) with quest 0, done.
    const auto refreshed = Client::Quests::RefreshTheQuest(tested->Context(), Client::Quests::QuestDone);
    ASSERT_TRUE(refreshed.HasValue());
    EXPECT_EQ(quest.current, 0);
    EXPECT_EQ(quest.dialog, 13);
    EXPECT_EQ(quest.currentState, Client::Quests::QuestDone);

    // Q-C842: from quest 0, the previous one is 255. Its state is the zen's high byte at +0x21388 (byte 63 from
    // +0x2134c, unshifted): with 0, no case, and the dialog stays.
    quest.characterClass = 1;
    quest.current = 0;
    const auto wrapped = Client::Quests::RefreshTheQuest(tested->Context(), Client::Quests::QuestDone);
    ASSERT_TRUE(wrapped.HasValue());
    EXPECT_EQ(wrapped.Value(), Client::Quests::QuestNotStarted);
    EXPECT_EQ(quest.current, 255);
    EXPECT_EQ(quest.dialog, 13);
    // With a zen from 0x2000000, quest 255 is done and its record is outside the 200: it cuts.
    quest.zen = 0x02000000;
    quest.current = 0;
    const auto outside = Client::Quests::RefreshTheQuest(tested->Context(), Client::Quests::QuestDone);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("Q-C842"), std::string::npos);
}

TEST(Quests, AClassPastTheFiveFlagsReadsTheBytesThatFollow)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    // Class 5 reads the low byte of the first dialog (10); class 6, the high one (0): with no request, it goes back to
    // 255.
    quest.characterClass = 5;
    const auto low = Client::Quests::QuestDialogOf(tested->Context(), 0, 3);
    ASSERT_TRUE(low.HasValue());
    EXPECT_EQ(low.Value(), 13);
    quest.characterClass = 6;
    ASSERT_TRUE(Client::Quests::QuestDialogOf(tested->Context(), 0, 3).HasValue());
    EXPECT_EQ(quest.current, 255);
    // The 0xff before the first A0 reads +0x2d + 0xff: the item group of request 13.
    quest.current = 0;
    quest.characterClass = Client::Quests::QuestClassUnknown;
    quest.records[0].entries[13].itemGroup = 1;
    const auto unknown = Client::Quests::QuestDialogOf(tested->Context(), 0, 3);
    ASSERT_TRUE(unknown.HasValue());
    EXPECT_EQ(unknown.Value(), 13);
}

TEST(Quests, TheRefreshCopiesTheHeroClassOnceAfterTheA0)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.characterClass = Client::Quests::QuestClassUnknown;
    quest.refreshClass = 1;
    tested->Hero().characterClass = 0x08;
    ASSERT_TRUE(Client::Quests::RefreshTheQuest(tested->Context(), Client::Quests::QuestNotStarted).HasValue());
    EXPECT_EQ(quest.characterClass, 0);
    EXPECT_EQ(quest.evolution, 1);
    EXPECT_EQ(quest.refreshClass, 0);
    tested->Hero().characterClass = 1;
    ASSERT_TRUE(Client::Quests::RefreshTheQuest(tested->Context(), Client::Quests::QuestNotStarted).HasValue());
    EXPECT_EQ(quest.characterClass, 0);
}

TEST(Quests, OpeningTheDialogShowsTheQuestWithItsAnswers)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
    quest.characterClass = 0;
    quest.states[0] = Client::Quests::QuestAvailable;
    runtime.panels.field78abb00 = 7;
    ASSERT_TRUE(Client::Quests::OpenTheQuestDialog(tested->Context(), -1).HasValue());
    EXPECT_EQ(runtime.panels.delegated.quest, Client::Quests::QuestDialogWindow);
    EXPECT_EQ(runtime.panels.field78abb00, 0);
    EXPECT_EQ(runtime.dialogBox.dialogIndex, 10);
    EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 0), "Un texto inventado");
    EXPECT_EQ(runtime.dialogBox.answerCount, 2);
    EXPECT_EQ(Client::Ui::DialogAnswer(runtime.dialogBox, 0), "1) Acepto");
    EXPECT_EQ(Client::Ui::DialogAnswer(runtime.dialogBox, 1), "2) Ahora no");
}

TEST(Quests, ADialogWithoutAnswersShowsTheCloseText)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
    quest.characterClass = 0;
    ASSERT_TRUE(Client::Quests::OpenTheQuestDialog(tested->Context(), 0).HasValue());
    // State 0: no case; +0x21384 stays as it was (0), and dialog 0 has no text or answers.
    EXPECT_EQ(runtime.dialogBox.dialogIndex, 0);
    EXPECT_EQ(runtime.dialogBox.answerCount, 1);
    EXPECT_EQ(Client::Ui::DialogAnswer(runtime.dialogBox, 0), "1) Cerrar");
}

TEST(Quests, TheListShowsTheHighestAndGoesBackToTheCurrent)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
    quest.characterClass = 1;
    quest.reached = 1;
    quest.byte21380 = 4;
    quest.byte21381 = 5;
    quest.states[0] = 0b00'00'10'00;
    ASSERT_TRUE(Client::Quests::OpenTheQuestList(tested->Context(), 0).HasValue());
    EXPECT_EQ(runtime.panels.delegated.quest, Client::Quests::QuestListWindow);
    EXPECT_EQ(quest.byte21380, 0);
    EXPECT_EQ(quest.byte21381, 0);
    // Quest 1 read with the 2-bit shift: done, dialog 33.
    EXPECT_EQ(quest.dialog, 33);
    EXPECT_EQ(runtime.dialogBox.dialogIndex, 33);
    EXPECT_EQ(quest.current, 0);
}

TEST(Quests, TheReturnToSelectionClearsTheQuestsButTheLastFlag)
{
    QuestState quest;
    quest.characterClass = 2;
    quest.byte21381 = 1;
    quest.dialog = 7;
    quest.byte21382 = 1;
    quest.current = 3;
    quest.reached = 4;
    quest.states.fill(0xaa);
    quest.flags = {1, 2, 3, 4};
    quest.zen = 99;
    std::uint8_t window = Client::Quests::QuestListWindow;
    Client::Quests::ResetTheQuests(quest, window);
    EXPECT_EQ(quest.characterClass, Client::Quests::QuestClassUnknown);
    EXPECT_EQ(quest.byte21381, 0);
    EXPECT_EQ(quest.dialog, 0);
    EXPECT_EQ(window, 0);
    EXPECT_EQ(quest.byte21382, 0);
    EXPECT_EQ(quest.current, 0);
    EXPECT_EQ(quest.reached, 0);
    EXPECT_TRUE(std::ranges::all_of(quest.states, [](std::uint8_t b) { return b == 0; }));
    EXPECT_EQ(quest.flags, (std::array<std::uint8_t, 4>{0, 0, 0, 4}));
    EXPECT_EQ(quest.zen, 99u);
}
} // namespace
