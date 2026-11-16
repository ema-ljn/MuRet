#include "Scenes/QuestReplies.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Text/DialogTable.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Quest.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/QuestState.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameDialogs.h"
#include "World/GameCamera.h"

#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Client::Scenes::Apply* de los 9F y A0..A3 (0x4ddaa0, 0x4ddad0, 0x4ddb00, 0x4ddb40 y 0x4ddb80). Los textos son
// inventados.
namespace
{
namespace Quest = Client::Protocol::Quest;

constexpr std::uint32_t Seed = 0xa3;
constexpr int OwnRecordClose = 0xf08;

struct Replies
{
    Replies()
    {
        world.Runtime().hero = 7;
        Hero().alive = true;
        Hero().key = HeroKey;
        Other().alive = true;
        Other().key = OtherKey;
        cloth.random = Client::Render::Models::CrtRandom(Seed);
        // La misión 1, un ítem para la clase 0: el diálogo 40 antes de aceptar y el 41 en curso sin el ítem.
        auto& record = world.Runtime().quest.records[1];
        record.entryCount = 1;
        record.entries[0].kind = 1;
        record.entries[0].itemGroup = 14;
        record.entries[0].itemIndex = 23;
        record.entries[0].itemCount = 1;
        record.entries[0].classes = {1, 0, 0, 0, 0};
        record.entries[0].dialogs = {40, 41, 42, 43};
        world.Runtime().quest.characterClass = 0;
        world.Runtime().dialogTable.assign(Client::Data::Text::DialogCount, {});
        std::ranges::copy(std::string_view("Una misión inventada"), world.Runtime().dialogTable[41].text.begin());
    }
    Client::Scenes::ActorState& Hero()
    {
        return world.Actors()[world.Runtime().hero];
    }
    Client::Scenes::ActorState& Other()
    {
        return world.Actors()[12];
    }
    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = world,
                .selection = selection,
                .dataRoot = {},
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = models,
                .log = log,
                .sendWindowClose = [this] { ++windowCloses; },
                .texts = &texts};
    }
    static constexpr std::uint16_t HeroKey = 0x21;
    static constexpr std::uint16_t OtherKey = 0x1234;
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth;
    Client::Scenes::SingleModelLibrary models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable({{609, "Cerrar"}});
    int windowCloses = 0;
};

std::unique_ptr<Replies> MakeReplies()
{
    return std::make_unique<Replies>();
}

TEST(QuestRepliesTest, TheFlagWritesOnlyTheFirstFour)
{
    auto tested = MakeReplies();
    auto& flags = tested->world.Runtime().quest.flags;
    Client::Scenes::ApplyQuestFlag(Quest::QuestFlag{3, 0x41}, tested->world);
    Client::Scenes::ApplyQuestFlag(Quest::QuestFlag{4, 0x42}, tested->world);
    EXPECT_EQ(flags, (std::array<std::uint8_t, 4>{0, 0, 0, 0x41}));
}

// El A0 deja el estado que lee la ventana: la clase, la primera sin terminar y +0x2138d para la próxima apertura.
TEST(QuestRepliesTest, TheListSetsTheStateTheWindowReads)
{
    auto tested = MakeReplies();
    tested->Hero().characterClass = 0x10;
    Client::Scenes::ApplyQuestList(Quest::QuestList{.count = 1, .states = {0x02}}, tested->Resources());
    const auto& quest = tested->world.Runtime().quest;
    EXPECT_EQ(quest.characterClass, 0);
    EXPECT_EQ(quest.evolution, 2);
    EXPECT_EQ(quest.current, 1);
    EXPECT_EQ(quest.reached, 1);
    EXPECT_EQ(quest.refreshClass, 1);
    EXPECT_EQ(tested->world.Runtime().panels.delegated.quest, 0);
}

// Q-C1126: un A0 con más bytes que el objeto (hasta +0x21398) escribe lo que cae adentro y descarta el resto con un
// aviso, sin cortar.
TEST(QuestRepliesTest, AnA0PastTheObjectDiscardsTheRestWithAWarning)
{
    auto tested = MakeReplies();
    Client::Scenes::ApplyQuestList(Quest::QuestList{.count = 0x80, .states = std::vector<std::uint8_t>(0x80, 0x11)},
                                   tested->Resources());
    const auto& quest = tested->world.Runtime().quest;
    EXPECT_EQ(quest.states[49], 0x11);
    EXPECT_EQ(quest.y, 0x11111111);
    ASSERT_EQ(tested->sink.lines.size(), 1u);
    EXPECT_EQ(tested->sink.levels[0], Client::Core::LogLevel::Warning);
    EXPECT_NE(tested->sink.lines[0].find("Q-C1126"), std::string::npos) << tested->sink.lines[0];
}

// 0x4ddb00: el A1 cierra todo (las misiones abiertas mandan el C1 03 31) y abre el diálogo de la misión.
TEST(QuestRepliesTest, TheStateClosesTheListAndOpensTheDialog)
{
    auto tested = MakeReplies();
    auto& runtime = tested->world.Runtime();
    runtime.panels.delegated.quest = Client::Scenes::QuestListWindow;
    ASSERT_TRUE(Client::Scenes::ApplyQuestState(Quest::QuestStateChange{1, 0b01'00}, tested->Resources()).HasValue());
    EXPECT_EQ(tested->windowCloses, 1);
    EXPECT_EQ(runtime.panels.delegated.quest, Client::Scenes::QuestDialogWindow);
    EXPECT_EQ(runtime.quest.current, 1);
    EXPECT_EQ(runtime.quest.states[0], 0b01'00);
    EXPECT_EQ(runtime.quest.currentState, Client::Scenes::QuestInProgress);
    EXPECT_EQ(runtime.quest.dialog, 41);
    EXPECT_EQ(runtime.dialogBox.dialogIndex, 41);
    EXPECT_EQ(Client::UI::DialogRow(runtime.dialogBox, 0), "Una misión inventada");
    EXPECT_EQ(Client::UI::DialogAnswer(runtime.dialogBox, 0), "1) Cerrar");
}

// 0x4ddb40: con pkt[4] distinto de 0, nada; con 0, como el A1 con pkt[5].
TEST(QuestRepliesTest, TheSetQuestResultActsOnlyOnZero)
{
    auto tested = MakeReplies();
    auto& runtime = tested->world.Runtime();
    ASSERT_TRUE(
        Client::Scenes::ApplySetQuestResult(Quest::SetQuestResult{1, 1, 0b01'00}, tested->Resources()).HasValue());
    EXPECT_EQ(runtime.quest.states[0], 0);
    EXPECT_EQ(runtime.panels.delegated.quest, 0);
    ASSERT_TRUE(
        Client::Scenes::ApplySetQuestResult(Quest::SetQuestResult{1, 0, 0b01'00}, tested->Resources()).HasValue());
    EXPECT_EQ(runtime.quest.states[0], 0b01'00);
    EXPECT_EQ(runtime.panels.delegated.quest, Client::Scenes::QuestDialogWindow);
    EXPECT_EQ(tested->windowCloses, 0);
    // 0x657a90 cierra con los dos bytes protegidos de 0x6594a3 y 0x6597f9: un rand() cada uno.
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(Seed, 2).State());
}

// Q-C842 desde el A1 y el A2 (resultado 0): la misión 0 en curso sin pedido para la clase baja a la 255, y con el byte
// alto del zen en 1 la 255 está en curso: su registro está fuera de los 200. El original lee después del objeto; MuRet
// descarta lo que falta del diálogo con un aviso, sin cortar la sesión, y queda lo escrito hasta ahí.
TEST(QuestRepliesTest, AQuestOutsideTheRecordsIsDiscardedFromThePacket)
{
    for (const bool fromSetQuest : {false, true})
    {
        SCOPED_TRACE(fromSetQuest);
        auto tested = MakeReplies();
        auto& runtime = tested->world.Runtime();
        runtime.quest.zen = 0x01000000;
        const auto applied =
            fromSetQuest ? Client::Scenes::ApplySetQuestResult(Quest::SetQuestResult{0, 0, 0b01}, tested->Resources())
                         : Client::Scenes::ApplyQuestState(Quest::QuestStateChange{0, 0b01}, tested->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(runtime.quest.current, 255);
        EXPECT_EQ(runtime.panels.delegated.quest, Client::Scenes::QuestDialogWindow);
        EXPECT_EQ(runtime.dialogBox.dialogIndex, -1);
        ASSERT_EQ(tested->sink.lines.size(), 1u);
        EXPECT_EQ(tested->sink.levels[0], Client::Core::LogLevel::Warning);
        EXPECT_NE(tested->sink.lines[0].find("Q-C842"), std::string::npos) << tested->sink.lines[0];
    }
}

// Q-C1126: el A1 de la misión 210 escribe su estado en +0x21380 (el byte 52) y abre el diálogo de esa misión, que se
// lee en 0 (el corrimiento de 28 bits): sin registro que leer, sin cortar.
TEST(QuestRepliesTest, AnA1PastTheTwoHundredWritesTheObject)
{
    auto tested = MakeReplies();
    auto& runtime = tested->world.Runtime();
    ASSERT_TRUE(Client::Scenes::ApplyQuestState(Quest::QuestStateChange{210, 0x55}, tested->Resources()).HasValue());
    EXPECT_EQ(runtime.quest.byte21380, 0x55);
    EXPECT_EQ(runtime.quest.current, 210);
    EXPECT_EQ(runtime.quest.reached, 210);
    EXPECT_EQ(runtime.quest.currentState, Client::Scenes::QuestNotStarted);
    EXPECT_EQ(runtime.panels.delegated.quest, Client::Scenes::QuestDialogWindow);
}

// 0x4ddbd5..0x4dddf2: 0xc8 suma pkt[6] a los puntos del héroe con el registro propio abierto; corta en los joints.
TEST(QuestRepliesTest, ThePointsPrizeAddsToTheHeroAndCutsAtTheJoints)
{
    auto tested = MakeReplies();
    tested->world.OwnCharacter().levelUpPoints = 7;
    const auto applied = Client::Scenes::ApplyQuestResult(
        Quest::QuestResult{Replies::HeroKey, Quest::PointsPrize, 0xfe}, tested->Resources());
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4dde20"), std::string::npos);
    EXPECT_EQ(tested->world.OwnCharacter().levelUpPoints, 7 + 0xfe);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, 0x66);
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(Seed, OwnRecordClose).State());
}

TEST(QuestRepliesTest, ThePointsOfAnotherCharacterLeaveTheRecord)
{
    auto tested = MakeReplies();
    const auto applied = Client::Scenes::ApplyQuestResult(
        Quest::QuestResult{Replies::OtherKey, Quest::ClassPointsPrize, 5}, tested->Resources());
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4de3bc"), std::string::npos);
    EXPECT_EQ(tested->world.OwnCharacter().levelUpPoints, 0);
    EXPECT_EQ(tested->cloth.random.State(), Client::Render::Models::CrtRandom(Seed).State());
}

TEST(QuestRepliesTest, TheClassPointsOfTheHeroAlsoOpenTheRecord)
{
    auto tested = MakeReplies();
    const auto applied = Client::Scenes::ApplyQuestResult(
        Quest::QuestResult{Replies::HeroKey, Quest::ClassPointsPrize, 5}, tested->Resources());
    ASSERT_FALSE(applied.HasValue());
    EXPECT_EQ(tested->world.OwnCharacter().levelUpPoints, 5);
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(Seed, OwnRecordClose).State());
}

// 0x4dde7b..0x4ddf72: la clase ((v & 0x10) | (v >> 4)) >> 1 en el personaje y, si es el héroe, en +0xb del registro.
TEST(QuestRepliesTest, TheClassChangeWritesTheActorAndTheHeroRecord)
{
    auto tested = MakeReplies();
    const auto other = Client::Scenes::ApplyQuestResult(
        Quest::QuestResult{Replies::OtherKey, Quest::ClassChangePrize, 0x30}, tested->Resources());
    ASSERT_FALSE(other.HasValue());
    EXPECT_NE(other.Error().find("0x4de0eb"), std::string::npos);
    EXPECT_EQ(tested->Other().characterClass, 9);
    EXPECT_EQ(tested->world.OwnCharacter().characterClass, 0);
    EXPECT_EQ(tested->cloth.random.State(), Client::Render::Models::CrtRandom(Seed).State());

    const auto hero = Client::Scenes::ApplyQuestResult(
        Quest::QuestResult{Replies::HeroKey, Quest::ClassChangePrize, 0x21}, tested->Resources());
    ASSERT_FALSE(hero.HasValue());
    EXPECT_EQ(tested->Hero().characterClass, 1);
    EXPECT_EQ(tested->world.OwnCharacter().characterClass, 1);
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(Seed, OwnRecordClose).State());
}

// Sin nadie con la clave, 0x5110d0 da el 400: la clase va a su +0x1d4, como en el original (Q-C1125). Los joints 0x60f
// que siguen (0x530860, con sus rand()) también son del 400 y cortan con la CL11 como con cualquiera.
TEST(QuestRepliesTest, AnUnknownKeyWritesTheSpareActor)
{
    auto tested = MakeReplies();
    const auto applied =
        Client::Scenes::ApplyQuestResult(Quest::QuestResult{0x777, Quest::ClassChangePrize, 0x10}, tested->Resources());
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4de0eb"), std::string::npos);
    EXPECT_NE(applied.Error().find("CL11"), std::string::npos);
    EXPECT_EQ(tested->world.Actors()[400].characterClass, 8);
    EXPECT_EQ(tested->cloth.random.State(), Client::Render::Models::CrtRandom(Seed).State());
}

TEST(QuestRepliesTest, TheEffectPrizeCutsAndAnotherKindOnlySounds)
{
    auto tested = MakeReplies();
    const auto effect = Client::Scenes::ApplyQuestResult(Quest::QuestResult{Replies::HeroKey, Quest::EffectPrize, 0},
                                                         tested->Resources());
    ASSERT_FALSE(effect.HasValue());
    EXPECT_NE(effect.Error().find("0x4de470"), std::string::npos);
    tested->world.Runtime().lastSoundId.reset();
    EXPECT_TRUE(Client::Scenes::ApplyQuestResult(Quest::QuestResult{Replies::HeroKey, 0xcc, 0}, tested->Resources())
                    .HasValue());
    EXPECT_EQ(tested->world.Runtime().lastSoundId, 0x66);
    EXPECT_EQ(tested->cloth.random.State(), Client::Render::Models::CrtRandom(Seed).State());
}
} // namespace
