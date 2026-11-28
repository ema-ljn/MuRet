#include "Quests/QuestReplies.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/DialogTable.h"
#include "Tables/TextTable.h"
#include "Protocol/Quests/Quest.h"
#include "Models/Cloth.h"
#include "Kernel/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "FrontEnd/LoginModels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Quests/QuestState.h"
#include "FrontEnd/SelectionState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameDialogs.h"
#include "Camera/GameCamera.h"
#include "Effects/ModelEffects.h"
#include "Audio/SoundId.h"

#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Client::Scenes::Apply* de los 9F y A0..A3 (0x4ddaa0, 0x4ddad0, 0x4ddb00, 0x4ddb40 y 0x4ddb80). Los textos son
// inventados.
namespace
{
namespace Quest = Client::Protocol::Quests;

constexpr std::uint32_t Seed = 0xa3;
constexpr int OwnRecordClose = 0xf08;

struct Replies
{
    Replies()
    {
        world.Runtime().hero = 7;
        // 0x4ea030 y 0x4f2250 leen las acciones de Player.bmd (los jugadores de 0x513100).
        models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        Hero().alive = true;
        Hero().key = HeroKey;
        Other().alive = true;
        Other().key = OtherKey;
        cloth.random = Client::Crt::CrtRandom(Seed);
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
        world.Runtime().dialogTable.assign(Client::Tables::DialogCount, {});
        std::ranges::copy(std::string_view("Una misión inventada"), world.Runtime().dialogTable[41].text.begin());
    }
    Client::Kernel::ActorState& Hero()
    {
        return world.Hero();
    }
    Client::Kernel::ActorState& Other()
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
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Models::ClothWorld cloth;
    Client::FrontEnd::SingleModelLibrary models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{609, "Cerrar"}});
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
    Client::Quests::ApplyQuestFlag(Quest::QuestFlag{3, 0x41}, tested->world);
    Client::Quests::ApplyQuestFlag(Quest::QuestFlag{4, 0x42}, tested->world);
    EXPECT_EQ(flags, (std::array<std::uint8_t, 4>{0, 0, 0, 0x41}));
}

// El A0 deja el estado que lee la ventana: la clase, la primera sin terminar y +0x2138d para la próxima apertura.
TEST(QuestRepliesTest, TheListSetsTheStateTheWindowReads)
{
    auto tested = MakeReplies();
    tested->Hero().characterClass = 0x10;
    Client::Quests::ApplyQuestList(Quest::QuestList{.count = 1, .states = {0x02}}, tested->Resources());
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
    Client::Quests::ApplyQuestList(Quest::QuestList{.count = 0x80, .states = std::vector<std::uint8_t>(0x80, 0x11)},
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
    runtime.panels.delegated.quest = Client::Quests::QuestListWindow;
    ASSERT_TRUE(Client::Quests::ApplyQuestState(Quest::QuestStateChange{1, 0b01'00}, tested->Resources()).HasValue());
    EXPECT_EQ(tested->windowCloses, 1);
    EXPECT_EQ(runtime.panels.delegated.quest, Client::Quests::QuestDialogWindow);
    EXPECT_EQ(runtime.quest.current, 1);
    EXPECT_EQ(runtime.quest.states[0], 0b01'00);
    EXPECT_EQ(runtime.quest.currentState, Client::Quests::QuestInProgress);
    EXPECT_EQ(runtime.quest.dialog, 41);
    EXPECT_EQ(runtime.dialogBox.dialogIndex, 41);
    EXPECT_EQ(Client::Ui::DialogRow(runtime.dialogBox, 0), "Una misión inventada");
    EXPECT_EQ(Client::Ui::DialogAnswer(runtime.dialogBox, 0), "1) Cerrar");
}

// 0x4ddb40: con pkt[4] distinto de 0, nada; con 0, como el A1 con pkt[5].
TEST(QuestRepliesTest, TheSetQuestResultActsOnlyOnZero)
{
    auto tested = MakeReplies();
    auto& runtime = tested->world.Runtime();
    ASSERT_TRUE(
        Client::Quests::ApplySetQuestResult(Quest::SetQuestResult{1, 1, 0b01'00}, tested->Resources()).HasValue());
    EXPECT_EQ(runtime.quest.states[0], 0);
    EXPECT_EQ(runtime.panels.delegated.quest, 0);
    ASSERT_TRUE(
        Client::Quests::ApplySetQuestResult(Quest::SetQuestResult{1, 0, 0b01'00}, tested->Resources()).HasValue());
    EXPECT_EQ(runtime.quest.states[0], 0b01'00);
    EXPECT_EQ(runtime.panels.delegated.quest, Client::Quests::QuestDialogWindow);
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
            fromSetQuest ? Client::Quests::ApplySetQuestResult(Quest::SetQuestResult{0, 0, 0b01}, tested->Resources())
                         : Client::Quests::ApplyQuestState(Quest::QuestStateChange{0, 0b01}, tested->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(runtime.quest.current, 255);
        EXPECT_EQ(runtime.panels.delegated.quest, Client::Quests::QuestDialogWindow);
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
    ASSERT_TRUE(Client::Quests::ApplyQuestState(Quest::QuestStateChange{210, 0x55}, tested->Resources()).HasValue());
    EXPECT_EQ(runtime.quest.byte21380, 0x55);
    EXPECT_EQ(runtime.quest.current, 210);
    EXPECT_EQ(runtime.quest.reached, 210);
    EXPECT_EQ(runtime.quest.currentState, Client::Quests::QuestNotStarted);
    EXPECT_EQ(runtime.panels.delegated.quest, Client::Quests::QuestDialogWindow);
}

// Los joints del premio que deja una respuesta, en orden desde el lugar `first`: quince 60f/0 en la posición del
// personaje con él de dueño, el ancho 40 y la key 2 y, con `falling`, cada uno seguido del 60f/0xa desde 200 más
// arriba.
void ExpectThePrizeSparkles(const Replies& tested, const Client::Kernel::ActorState& owner, bool falling,
                            std::size_t first = 0)
{
    const auto& joints = tested.world.DrawTail().joints;
    const std::size_t count = falling ? 30 : 15;
    glm::vec3 raised = owner.position;
    raised.z = static_cast<float>(static_cast<double>(owner.position.z) + 200.0);
    for (std::size_t i = 0; i < count; ++i)
    {
        SCOPED_TRACE(i);
        const auto& joint = joints[first + i];
        const bool descending = falling && i % 2 == 1;
        EXPECT_TRUE(joint.alive);
        EXPECT_EQ(joint.type, 0x60f);
        EXPECT_EQ(joint.subtype, descending ? 0xa : 0);
        EXPECT_EQ(joint.ownerContext, &owner);
        EXPECT_EQ(joint.width, 40.0f);
        EXPECT_EQ(joint.key, 2);
        EXPECT_EQ(joint.life, 50);
        EXPECT_EQ(joint.position, descending ? raised : owner.position);
        EXPECT_EQ(joint.targetPosition, descending ? raised : owner.position);
    }
    EXPECT_FALSE(joints[first + count].alive);
}

// El calco 0x61e/0 de 0x518cf0 en el lugar `slot`, con el personaje de dueño.
void ExpectTheDecal(const Replies& tested, const Client::Kernel::ActorState& owner, std::size_t slot = 0)
{
    const auto& decal = tested.world.Map().effects.Effects()[slot];
    EXPECT_TRUE(decal.alive);
    EXPECT_EQ(decal.type, Client::Effects::ColoredDecalEffect);
    EXPECT_EQ(decal.subtype, Client::Effects::LevelUpDecalSubtype);
    EXPECT_EQ(decal.transform.position, owner.position);
    EXPECT_EQ(decal.ownerContext, &owner);
}

// 0x4ddbd5..0x4dde65: 0xc8 suma pkt[6] a los puntos del héroe con el registro propio abierto; después, quince 60f/0
// (tres rand() cada uno: el ancho 40 pasa de 10), el calco 0x61e/0 y el sonido 0x65, sin la acción.
TEST(QuestRepliesTest, ThePointsPrizeAddsToTheHeroAndSparkles)
{
    auto tested = MakeReplies();
    tested->world.OwnCharacter().levelUpPoints = 7;
    tested->Hero().position = {1000.0f, 2000.0f, 150.0f};
    tested->Hero().animation.action = 0x22;
    const auto applied = Client::Quests::ApplyQuestResult(
        Quest::QuestResult{Replies::HeroKey, Quest::PointsPrize, 0xfe}, tested->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(tested->world.OwnCharacter().levelUpPoints, 7 + 0xfe);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x65});
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(Seed, OwnRecordClose + 15 * 3).State());
    ExpectThePrizeSparkles(*tested, tested->Hero(), false);
    ExpectTheDecal(*tested, tested->Hero());
    EXPECT_EQ(tested->Hero().animation.action, 0x22);
}

// 0xca de otro personaje: sin el registro propio; treinta joints (noventa rand()), el calco, 0x513100 (que con otro
// tipo que el 0x20a no hace nada), la acción 0xb8 por el modelo del personaje (que acá no está: nada) y el 0x66.
TEST(QuestRepliesTest, ThePointsOfAnotherCharacterLeaveTheRecord)
{
    auto tested = MakeReplies();
    const auto applied = Client::Quests::ApplyQuestResult(
        Quest::QuestResult{Replies::OtherKey, Quest::ClassPointsPrize, 5}, tested->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(tested->world.OwnCharacter().levelUpPoints, 0);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x66});
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(Seed, 30 * 3).State());
    ExpectThePrizeSparkles(*tested, tested->Other(), true);
    ExpectTheDecal(*tested, tested->Other());
}

TEST(QuestRepliesTest, TheClassPointsOfTheHeroAlsoOpenTheRecord)
{
    auto tested = MakeReplies();
    const auto applied = Client::Quests::ApplyQuestResult(
        Quest::QuestResult{Replies::HeroKey, Quest::ClassPointsPrize, 5}, tested->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(tested->world.OwnCharacter().levelUpPoints, 5);
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(Seed, OwnRecordClose + 30 * 3).State());
}

// 0x4dde7b..0x4ddf72: la clase ((v & 0x10) | (v >> 4)) >> 1 en el personaje y, si es el héroe, en +0xb del registro.
// Los joints de la segunda respuesta siguen en el pool después de los de la primera.
TEST(QuestRepliesTest, TheClassChangeWritesTheActorAndTheHeroRecord)
{
    auto tested = MakeReplies();
    const auto other = Client::Quests::ApplyQuestResult(
        Quest::QuestResult{Replies::OtherKey, Quest::ClassChangePrize, 0x30}, tested->Resources());
    ASSERT_TRUE(other.HasValue()) << other.Error();
    EXPECT_EQ(tested->Other().characterClass, 9);
    EXPECT_EQ(tested->world.OwnCharacter().characterClass, 0);
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(Seed, 30 * 3).State());

    const auto hero = Client::Quests::ApplyQuestResult(
        Quest::QuestResult{Replies::HeroKey, Quest::ClassChangePrize, 0x21}, tested->Resources());
    ASSERT_TRUE(hero.HasValue()) << hero.Error();
    EXPECT_EQ(tested->Hero().characterClass, 1);
    EXPECT_EQ(tested->world.OwnCharacter().characterClass, 1);
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(Seed, 30 * 3 + OwnRecordClose + 30 * 3).State());
    ExpectThePrizeSparkles(*tested, tested->Hero(), true, 30);
    ExpectTheDecal(*tested, tested->Hero(), 1);
}

// Sin nadie con la clave, 0x5110d0 da el 400: la clase va a su +0x1d4, como en el original (Q-C1125), y los joints y
// el calco lo llevan de dueño.
TEST(QuestRepliesTest, AnUnknownKeyWritesTheSpareActor)
{
    auto tested = MakeReplies();
    const auto applied =
        Client::Quests::ApplyQuestResult(Quest::QuestResult{0x777, Quest::ClassChangePrize, 0x10}, tested->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(tested->world.Actors()[400].characterClass, 8);
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(Seed, 30 * 3).State());
    ExpectThePrizeSparkles(*tested, tested->world.Actors()[400], true);
}

// 0x4ddb80 con un jugador: después de los joints y el calco, 0x513100 viste la clase nueva (el casco base de la clase 1
// pasa al de la 9) y 0x4ea030 pone la acción 0xb8 (Player.bmd la tiene). Con la acción 0x22, 0x513100 no llama a
// 0x4f2250: sólo los rand() del registro y de los joints. El 0x66 otra vez al final.
TEST(QuestRepliesTest, TheClassChangeDressesAPlayerAndSetsTheAction)
{
    auto tested = MakeReplies();
    auto& hero = tested->Hero();
    hero.type = Client::Tables::PlayerSkeletonModel;
    hero.characterClass = 1;
    hero.animation.action = 0x22;
    hero.position = {1000.0f, 2000.0f, 150.0f};
    hero.body[0].model = 0x2213;
    hero.body[0].level = 3;
    hero.body[1].model = 0x221b;
    const auto applied = Client::Quests::ApplyQuestResult(
        Quest::QuestResult{Replies::HeroKey, Quest::ClassChangePrize, 0x30}, tested->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(hero.characterClass, 9);
    EXPECT_EQ(hero.body[0].model, 0x2218);
    EXPECT_EQ(hero.body[0].level, 0);
    // Q-C1024: la armadura compara los pantalones, que no son la armadura base.
    EXPECT_EQ(hero.body[1].model, 0x221b);
    EXPECT_EQ(hero.animation.action, 0xb8);
    EXPECT_EQ(hero.animation.previousAction, 0x22);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x66});
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(Seed, OwnRecordClose + 30 * 3).State());
    ExpectThePrizeSparkles(*tested, hero, true);
    ExpectTheDecal(*tested, hero);
}

// Sin Player.bmd, un jugador corta antes de 0x513100 (en 0x4de140), con la clase, los joints y el calco ya escritos y
// la ropa sin tocar.
TEST(QuestRepliesTest, AClassPrizeWithoutPlayerBmdCutsBeforeTheClothes)
{
    auto tested = MakeReplies();
    tested->models.Clear();
    auto& hero = tested->Hero();
    hero.type = Client::Tables::PlayerSkeletonModel;
    hero.animation.action = 0x22;
    hero.body[0].model = 0x2212;
    const auto applied = Client::Quests::ApplyQuestResult(
        Quest::QuestResult{Replies::HeroKey, Quest::ClassChangePrize, 0x30}, tested->Resources());
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4de140"), std::string::npos) << applied.Error();
    EXPECT_EQ(hero.characterClass, 9);
    EXPECT_EQ(hero.body[0].model, 0x2212);
    EXPECT_EQ(hero.animation.action, 0x22);
    ExpectThePrizeSparkles(*tested, hero, true);
    ExpectTheDecal(*tested, hero);
}

// 0x530860 con el pool de joints lleno no crea ni saca rand(), y las vueltas siguen: el calco y el sonido igual.
TEST(QuestRepliesTest, AFullJointPoolSkipsOnlyTheSparkles)
{
    auto tested = MakeReplies();
    for (auto& joint : tested->world.DrawTail().joints)
        joint.alive = true;
    const auto applied = Client::Quests::ApplyQuestResult(Quest::QuestResult{Replies::OtherKey, Quest::PointsPrize, 1},
                                                          tested->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(tested->cloth.random.State(), Client::Crt::CrtRandom(Seed).State());
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x65});
    ExpectTheDecal(*tested, tested->Other());
}

TEST(QuestRepliesTest, TheEffectPrizeSparklesAndAnotherKindOnlySounds)
{
    auto tested = MakeReplies();
    const auto effect = Client::Quests::ApplyQuestResult(Quest::QuestResult{Replies::HeroKey, Quest::EffectPrize, 0},
                                                         tested->Resources());
    ASSERT_TRUE(effect.HasValue()) << effect.Error();
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(Seed, 30 * 3).State());
    ExpectThePrizeSparkles(*tested, tested->Hero(), true);
    ExpectTheDecal(*tested, tested->Hero());
    tested->world.Runtime().lastSoundId.reset();
    EXPECT_TRUE(Client::Quests::ApplyQuestResult(Quest::QuestResult{Replies::HeroKey, 0xcc, 0}, tested->Resources())
                    .HasValue());
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x66});
    EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(Seed, 30 * 3).State());
}
} // namespace
