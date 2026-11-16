#include "Scenes/Quests.h"

#include "Data/Tables/QuestTable.h"
#include "Data/Text/DialogTable.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/QuestState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameDialogs.h"
#include "World/ItemState.h"

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

// Client::Scenes: las funciones del objeto de las misiones [0x710148] que no dibujan (0x4195c0, 0x418e20, 0x418ee0,
// 0x418f40, 0x418fb0, 0x4194c0, 0x419600, 0x419b10, 0x4198b0, 0x419840 y 0x558410). Las misiones, los diálogos y los
// textos son inventados.
namespace
{
using Client::Scenes::QuestContext;
using Client::Scenes::QuestState;
using Client::Scenes::QuestStateOf;

constexpr std::uint32_t Seed = 0x5eed;
// Un registro propio que se cierra hasta 0 gasta 3.848 rand() (0x40b4d0).
constexpr int OwnRecordClose = 0xf08;
// El pergamino inventado de la misión 0: grupo 14, índice 23.
constexpr std::int32_t ScrollType = 14 * 0x200 + 23;

void PutText(std::array<char, Client::Data::Text::DialogTextSize>& text, std::string_view value)
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
        // La misión 0: un pedido de ítems para la clase 0 con la condición 0 (nivel 10 a 0 y 1.000 de zen).
        Client::Data::Tables::QuestRecord& first = Quest().records[0];
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
        // La misión 1: un pedido para la clase 1 nada más.
        Client::Data::Tables::QuestRecord& second = Quest().records[1];
        second.entryCount = 1;
        second.entries[0].classes = {0, 1, 0, 0, 0};
        second.entries[0].dialogs = {30, 31, 32, 33};

        world.Runtime().dialogTable.assign(Client::Data::Text::DialogCount, {});
        for (const std::int32_t index : {10, 11, 12, 13, 20, 30, 33})
            PutText(world.Runtime().dialogTable[static_cast<std::size_t>(index)].text, "Un texto inventado");
        auto& withAnswers = world.Runtime().dialogTable[10];
        withAnswers.answerCount = 2;
        std::ranges::copy(std::string_view("Acepto"), withAnswers.answers[0].begin());
        std::ranges::copy(std::string_view("Ahora no"), withAnswers.answers[1].begin());
        world.OwnCharacter().level = 20;
        world.OwnCharacter().zen = 5000;
    }
    Client::Scenes::ActorState& Hero()
    {
        return world.Actors()[world.Runtime().hero];
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
        Client::World::ItemState& item = world.Inventory().cells[cell];
        item.id = static_cast<std::uint16_t>(ScrollType);
        item.occupied = 1;
        item.scalar1Low = level << 3;
    }
    Client::Scenes::WorldState world;
    Client::Render::Models::CrtRandom random{Seed};
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable({{609, "Cerrar"}});
};

std::unique_ptr<Quests> MakeQuests()
{
    return std::make_unique<Quests>();
}

TEST(Quests, TheStateOfAQuestIsReadShiftedLikeTheOriginal)
{
    const auto runtime = std::make_unique<Client::Scenes::WorldRuntime>();
    QuestState& quest = runtime->quest;
    // Byte 0: las misiones 0 a 3 donde las escribe el GS (2 bits cada una, la 0 abajo).
    quest.states[0] = 0b10'01'11'00;
    quest.states[1] = 0b11'00'00'10;
    EXPECT_EQ(QuestStateOf(*runtime, 0), 0);
    EXPECT_EQ(QuestStateOf(*runtime, 1), 3);
    EXPECT_EQ(QuestStateOf(*runtime, 2), 1);
    EXPECT_EQ(QuestStateOf(*runtime, 3), 2);
    // Q-C841: la 4 lee el byte 1 corrido (4 − 1) · 2 = 6 bits; desde la 5, la cuenta llega a 8 y da 0.
    EXPECT_EQ(QuestStateOf(*runtime, 4), 3);
    EXPECT_EQ(QuestStateOf(*runtime, 5), 0);
    EXPECT_EQ(QuestStateOf(*runtime, 8), 0);
    // −1: el byte 0 (la división con signo) corrido 30 bits; fuera de los 64 bytes, 0.
    EXPECT_EQ(QuestStateOf(*runtime, -1), 0);
    EXPECT_EQ(QuestStateOf(*runtime, 200), 0);
    EXPECT_EQ(QuestStateOf(*runtime, -5), 0);
    // La 236 lee el relleno de +0x21387 (el byte 59) corrido (236 − 59) & 0xf · 2 = 2 bits.
    quest.byte21387 = 0b0000'1100;
    EXPECT_EQ(QuestStateOf(*runtime, 236), 3);

    quest.current = 3;
    Client::Scenes::ReadTheCurrentQuestState(*runtime);
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
    EXPECT_EQ(Client::Scenes::MissingQuestItems(inventory, ScrollType, 2, 1), 1);
    EXPECT_EQ(Client::Scenes::MissingQuestItems(inventory, ScrollType, 2, -1), 0);
    EXPECT_EQ(Client::Scenes::MissingQuestItems(inventory, ScrollType, 1, 0), 0);
    EXPECT_EQ(Client::Scenes::MissingQuestItems(inventory, ScrollType + 1, 3, -1), 3);
    // Una cuenta de 0 sin ninguno da 0; con uno, también (llega a la cuenta).
    EXPECT_EQ(Client::Scenes::MissingQuestItems(inventory, ScrollType + 1, 0, -1), 0);
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
    EXPECT_EQ(Client::Scenes::ApplyTheQuestList(tested->world, 2, bytes), 0u);
    EXPECT_EQ(quest.characterClass, 3);
    EXPECT_EQ(quest.evolution, 2);
    EXPECT_EQ(quest.refreshClass, 1);
    EXPECT_EQ(tested->world.Runtime().byte6eeda8, 0);
    EXPECT_EQ(quest.states[0], 0b00'00'10'10);
    EXPECT_EQ(quest.states[1], 0x40);
    EXPECT_TRUE(std::ranges::all_of(std::span(quest.states).subspan(2), [](std::uint8_t b) { return b == 0; }));
    // La cuenta de bytes es también la de misiones que mira: las dos primeras están terminadas.
    EXPECT_EQ(quest.current, 2);
    EXPECT_EQ(quest.reached, 2);
    EXPECT_EQ(tested->Hero().byte390, 0);
}

TEST(Quests, TheThirdQuestDoneMarksTheHeroAndAShortA0IsFilledWithZeros)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    // Q-C840 y P-C171: la cuenta pide 3 bytes y el paquete trae 1; lo que falta va en 0.
    const std::array<std::uint8_t, 1> bytes{0b10'10'10'10};
    EXPECT_EQ(Client::Scenes::ApplyTheQuestList(tested->world, 3, bytes), 0u);
    EXPECT_EQ(quest.states[0], 0b10'10'10'10);
    EXPECT_EQ(quest.states[1], 0);
    EXPECT_EQ(quest.states[2], 0);
    // Las tres que mira están terminadas: la primera sin terminar es la cuenta.
    EXPECT_EQ(quest.current, 3);
    EXPECT_EQ(tested->Hero().byte390, 1);
}

// Q-C1126: la copia del A0 (0x418e20) sigue después de los 50 bytes por los campos del objeto hasta su final
// (+0x21398): +0x2137e.., la ventana de +0x21383, el diálogo, el zen, la pestaña, +0x2138d (también el 1 de recién) y
// la esquina. Después, la primera sin terminar (con lo que dejó la copia) en +0x2137e y +0x2137f. Lo que pasa del
// objeto no se escribe y se cuenta.
TEST(Quests, AnA0OfMoreThanFiftyBytesWritesTheObjectUpToItsEnd)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    std::vector<std::uint8_t> bytes(80, 0);
    // 0xaa: las misiones 0 a 4 terminadas (Q-C841) y la 5 no.
    std::fill_n(bytes.begin(), 50, std::uint8_t{0xaa});
    for (std::size_t i = 50; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(i);
    EXPECT_EQ(Client::Scenes::ApplyTheQuestList(tested->world, 80, bytes), 4u);
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
    // Hasta el final justo, nada afuera.
    EXPECT_EQ(Client::Scenes::ApplyTheQuestList(tested->world, 76, bytes), 0u);
}

TEST(Quests, TheA1WritesTheWholeByteAndKeepsTheHighest)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.reached = 9;
    quest.states[1] = 0xff;
    // Q-C843: el byte entero de las cuatro misiones, no los dos bits de la 5.
    Client::Scenes::SetTheQuestState(tested->world, 5, 0x04);
    EXPECT_EQ(quest.current, 5);
    EXPECT_EQ(quest.reached, 9);
    EXPECT_EQ(quest.states[1], 0x04);
    Client::Scenes::SetTheQuestState(tested->world, 12, 0x80);
    EXPECT_EQ(quest.reached, 12);
    EXPECT_EQ(tested->Hero().byte390, 0);
    Client::Scenes::SetTheQuestState(tested->world, 3, 0x80);
    EXPECT_EQ(tested->Hero().byte390, 1);
}

// Q-C1126: desde la misión 200, 0x418ee0 escribe el estado en los campos que siguen a los 50 bytes, después de poner
// +0x2137e y +0x2137f: la 201 pisa la misión en curso de recién, la 204 la más alta, la 222 la ventana y la 255 el byte
// alto del zen.
TEST(Quests, TheA1PastTheTwoHundredWritesTheFieldsThatFollow)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    Client::Scenes::SetTheQuestState(tested->world, 201, 7);
    EXPECT_EQ(quest.current, 7);
    EXPECT_EQ(quest.reached, 201);
    Client::Scenes::SetTheQuestState(tested->world, 204, 3);
    EXPECT_EQ(quest.current, 204);
    EXPECT_EQ(quest.reached, 3);
    Client::Scenes::SetTheQuestState(tested->world, 222, 2);
    EXPECT_EQ(tested->world.Runtime().panels.delegated.quest, 2);
    EXPECT_EQ(quest.reached, 222);
    quest.zen = 0x00123456;
    Client::Scenes::SetTheQuestState(tested->world, 255, 0x01);
    EXPECT_EQ(quest.zen, 0x01123456u);
    EXPECT_EQ(QuestStateOf(tested->world.Runtime(), 255), 1);
}

TEST(Quests, AnAvailableQuestChecksItsConditionsWithTheOwnRecordOpen)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.characterClass = 0;
    quest.states[0] = Client::Scenes::QuestAvailable;
    const auto refreshed = Client::Scenes::RefreshTheQuest(tested->Context(), 0xff);
    ASSERT_TRUE(refreshed.HasValue());
    EXPECT_EQ(refreshed.Value(), Client::Scenes::QuestAvailable);
    EXPECT_EQ(quest.dialog, 10);
    // Sin el zen: lo guarda sin abrir el registro. El nivel mínimo, con un acceso que lo cierra.
    EXPECT_EQ(quest.zen, 1000u);
    EXPECT_EQ(tested->random.State(), Client::Tests::AfterDraws(Seed, OwnRecordClose).State());
}

TEST(Quests, AFailedConditionLeavesItsDialogAndFive)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.characterClass = 0;
    tested->world.OwnCharacter().level = 9;
    const auto refreshed = Client::Scenes::RefreshTheQuest(tested->Context(), Client::Scenes::QuestAvailable);
    ASSERT_TRUE(refreshed.HasValue());
    EXPECT_EQ(refreshed.Value(), Client::Scenes::QuestConditionFailed);
    EXPECT_EQ(quest.dialog, 20);
    EXPECT_EQ(tested->random.State(), Client::Tests::AfterDraws(Seed, OwnRecordClose).State());
}

TEST(Quests, TheZenIsReadWithTheRecordOpenOnlyWhenAsked)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.characterClass = 0;
    tested->world.OwnCharacter().zen = 999;
    Client::Data::Tables::QuestCondition& condition = quest.records[0].conditions[0];
    condition.maxLevel = 30;
    const auto checked = Client::Scenes::CheckTheQuestConditions(tested->Context(), 0, true);
    ASSERT_TRUE(checked.HasValue());
    EXPECT_FALSE(checked.Value());
    EXPECT_EQ(quest.zen, 1000u);
    EXPECT_EQ(quest.dialog, 20);
    EXPECT_EQ(quest.currentState, Client::Scenes::QuestConditionFailed);
    // El mínimo, el máximo y el zen: tres accesos que cierran el registro.
    EXPECT_EQ(tested->random.State(), Client::Tests::AfterDraws(Seed, 3 * OwnRecordClose).State());

    tested->world.OwnCharacter().zen = 1000;
    const auto passed = Client::Scenes::CheckTheQuestConditions(tested->Context(), 0, true);
    ASSERT_TRUE(passed.HasValue());
    EXPECT_TRUE(passed.Value());
    // El máximo se compara sin signo: 31 lo pasa.
    tested->world.OwnCharacter().level = 31;
    const auto over = Client::Scenes::CheckTheQuestConditions(tested->Context(), 0, false);
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
    const auto failed = Client::Scenes::CheckTheQuestConditions(tested->Context(), 0, false);
    ASSERT_TRUE(failed.HasValue());
    EXPECT_FALSE(failed.Value());
    EXPECT_EQ(tested->random.State(), Client::Render::Models::CrtRandom(Seed).State());
    quest.states[0] = 0b10'00;
    const auto passed = Client::Scenes::CheckTheQuestConditions(tested->Context(), 0, false);
    ASSERT_TRUE(passed.HasValue());
    EXPECT_TRUE(passed.Value());
}

TEST(Quests, AQuestInProgressShowsWhetherTheItemsAreThere)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.characterClass = 0;
    quest.currentState = Client::Scenes::QuestInProgress;
    const auto missing = Client::Scenes::RefreshTheQuest(tested->Context(), Client::Scenes::QuestInProgress);
    ASSERT_TRUE(missing.HasValue());
    EXPECT_EQ(quest.dialog, 11);
    const auto none = Client::Scenes::CanCompleteTheQuest(tested->world);
    ASSERT_TRUE(none.HasValue());
    EXPECT_FALSE(none.Value());

    tested->GiveTheScroll(5, 0);
    const auto found = Client::Scenes::RefreshTheQuest(tested->Context(), Client::Scenes::QuestInProgress);
    ASSERT_TRUE(found.HasValue());
    EXPECT_EQ(found.Value(), Client::Scenes::QuestInProgress);
    EXPECT_EQ(quest.dialog, 12);
    const auto complete = Client::Scenes::CanCompleteTheQuest(tested->world);
    ASSERT_TRUE(complete.HasValue());
    EXPECT_TRUE(complete.Value());
    // Sin pedidos de ítems de la clase, no.
    quest.records[0].entries[0].kind = 0;
    const auto noItems = Client::Scenes::CanCompleteTheQuest(tested->world);
    ASSERT_TRUE(noItems.HasValue());
    EXPECT_FALSE(noItems.Value());
    EXPECT_EQ(tested->random.State(), Client::Render::Models::CrtRandom(Seed).State());
}

TEST(Quests, ADoneQuestShowsItsLastDialog)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.characterClass = 0;
    const auto refreshed = Client::Scenes::RefreshTheQuest(tested->Context(), Client::Scenes::QuestDone);
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
    // La misión 1 no tiene pedido de la clase 0: −−+0x2137e y 0x419600(0xff) con la 0, terminada.
    const auto refreshed = Client::Scenes::RefreshTheQuest(tested->Context(), Client::Scenes::QuestDone);
    ASSERT_TRUE(refreshed.HasValue());
    EXPECT_EQ(quest.current, 0);
    EXPECT_EQ(quest.dialog, 13);
    EXPECT_EQ(quest.currentState, Client::Scenes::QuestDone);

    // Q-C842: desde la 0, la anterior es la 255. Su estado es el byte alto del zen de +0x21388 (byte 63 desde
    // +0x2134c, sin correr): con 0, ningún caso, y el diálogo queda.
    quest.characterClass = 1;
    quest.current = 0;
    const auto wrapped = Client::Scenes::RefreshTheQuest(tested->Context(), Client::Scenes::QuestDone);
    ASSERT_TRUE(wrapped.HasValue());
    EXPECT_EQ(wrapped.Value(), Client::Scenes::QuestNotStarted);
    EXPECT_EQ(quest.current, 255);
    EXPECT_EQ(quest.dialog, 13);
    // Con un zen desde 0x2000000, la 255 está terminada y su registro está fuera de los 200: corta.
    quest.zen = 0x02000000;
    quest.current = 0;
    const auto outside = Client::Scenes::RefreshTheQuest(tested->Context(), Client::Scenes::QuestDone);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("Q-C842"), std::string::npos);
}

TEST(Quests, AClassPastTheFiveFlagsReadsTheBytesThatFollow)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    // La clase 5 lee el byte bajo del primer diálogo (10); la 6, el alto (0): sin pedido, vuelve a la 255.
    quest.characterClass = 5;
    const auto low = Client::Scenes::QuestDialogOf(tested->Context(), 0, 3);
    ASSERT_TRUE(low.HasValue());
    EXPECT_EQ(low.Value(), 13);
    quest.characterClass = 6;
    ASSERT_TRUE(Client::Scenes::QuestDialogOf(tested->Context(), 0, 3).HasValue());
    EXPECT_EQ(quest.current, 255);
    // La 0xff de antes del primer A0 lee +0x2d + 0xff: el grupo del ítem del pedido 13.
    quest.current = 0;
    quest.characterClass = Client::Scenes::QuestClassUnknown;
    quest.records[0].entries[13].itemGroup = 1;
    const auto unknown = Client::Scenes::QuestDialogOf(tested->Context(), 0, 3);
    ASSERT_TRUE(unknown.HasValue());
    EXPECT_EQ(unknown.Value(), 13);
}

TEST(Quests, TheRefreshCopiesTheHeroClassOnceAfterTheA0)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    quest.characterClass = Client::Scenes::QuestClassUnknown;
    quest.refreshClass = 1;
    tested->Hero().characterClass = 0x08;
    ASSERT_TRUE(Client::Scenes::RefreshTheQuest(tested->Context(), Client::Scenes::QuestNotStarted).HasValue());
    EXPECT_EQ(quest.characterClass, 0);
    EXPECT_EQ(quest.evolution, 1);
    EXPECT_EQ(quest.refreshClass, 0);
    tested->Hero().characterClass = 1;
    ASSERT_TRUE(Client::Scenes::RefreshTheQuest(tested->Context(), Client::Scenes::QuestNotStarted).HasValue());
    EXPECT_EQ(quest.characterClass, 0);
}

TEST(Quests, OpeningTheDialogShowsTheQuestWithItsAnswers)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
    quest.characterClass = 0;
    quest.states[0] = Client::Scenes::QuestAvailable;
    runtime.panels.field78abb00 = 7;
    ASSERT_TRUE(Client::Scenes::OpenTheQuestDialog(tested->Context(), -1).HasValue());
    EXPECT_EQ(runtime.panels.delegated.quest, Client::Scenes::QuestDialogWindow);
    EXPECT_EQ(runtime.panels.field78abb00, 0);
    EXPECT_EQ(runtime.dialogBox.dialogIndex, 10);
    EXPECT_EQ(Client::UI::DialogRow(runtime.dialogBox, 0), "Un texto inventado");
    EXPECT_EQ(runtime.dialogBox.answerCount, 2);
    EXPECT_EQ(Client::UI::DialogAnswer(runtime.dialogBox, 0), "1) Acepto");
    EXPECT_EQ(Client::UI::DialogAnswer(runtime.dialogBox, 1), "2) Ahora no");
}

TEST(Quests, ADialogWithoutAnswersShowsTheCloseText)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
    quest.characterClass = 0;
    ASSERT_TRUE(Client::Scenes::OpenTheQuestDialog(tested->Context(), 0).HasValue());
    // Estado 0: ningún caso; +0x21384 queda como estaba (0), y el 0 no tiene texto ni respuestas.
    EXPECT_EQ(runtime.dialogBox.dialogIndex, 0);
    EXPECT_EQ(runtime.dialogBox.answerCount, 1);
    EXPECT_EQ(Client::UI::DialogAnswer(runtime.dialogBox, 0), "1) Cerrar");
}

TEST(Quests, TheListShowsTheHighestAndGoesBackToTheCurrent)
{
    auto tested = MakeQuests();
    QuestState& quest = tested->Quest();
    Client::Scenes::WorldRuntime& runtime = tested->world.Runtime();
    quest.characterClass = 1;
    quest.reached = 1;
    quest.byte21380 = 4;
    quest.byte21381 = 5;
    quest.states[0] = 0b00'00'10'00;
    ASSERT_TRUE(Client::Scenes::OpenTheQuestList(tested->Context(), 0).HasValue());
    EXPECT_EQ(runtime.panels.delegated.quest, Client::Scenes::QuestListWindow);
    EXPECT_EQ(quest.byte21380, 0);
    EXPECT_EQ(quest.byte21381, 0);
    // La 1 leída con el corrimiento de 2 bits: terminada, el diálogo 33.
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
    std::uint8_t window = Client::Scenes::QuestListWindow;
    Client::Scenes::ResetTheQuests(quest, window);
    EXPECT_EQ(quest.characterClass, Client::Scenes::QuestClassUnknown);
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
