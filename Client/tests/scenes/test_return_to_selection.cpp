#include "Session/ReturnToSelection.h"
#include "Kernel/HelperTable.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "UI/PanelDialog.h"
#include "support/LetterMeasure.h"
#include "support/SceneDataFiles.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Protocol/FrontEnd/CharacterSelectResult.h"
#include "Models/Cloth.h"
#include "Kernel/ActorState.h"
#include "Pets/DarkHorse.h"
#include "Scenes/FaunaState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GuildState.h"
#include "Pets/HelperTable.h"
#include "Session/IWorldEntryPublisher.h"
#include "Session/ModuleStates.h"
#include "Scenes/LoginModels.h"
#include "Scenes/NpcWindowState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Scenes/QuestState.h"
#include "Scenes/SceneId.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SocialState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/GameContext.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "World/Leaves.h"
#include "World/ModelEffects.h"
#include "Terrain/WorldTerrain.h"

#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

// La vuelta del juego a la selección (0x4c2710 con el F1 02 con 1 cifrado, 0x4c3626..0x4c390f, y 0x4c2480),
// en lo que MuRet tiene del mundo. Contrato: 06, "La vuelta a la selección", y Q-C211 a Q-C215.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Scenes::ApplyReturnToSelection;
using Client::Scenes::WorldEntryResources;

constexpr std::uint32_t Seed = 0x1234;
constexpr std::uint16_t PlayerKey = 5;
// El cierre del registro propio que lo deja sin accesos (0x4c260f..0x4c264b, 0x40b4d0).
constexpr int OwnRecordClose = 0xf08;
// 0x510f70(−1) compara la clave con signo: solo sobrevive esta (Q-C212).
constexpr std::uint16_t SurvivingKey = 0xffff;
constexpr std::uint16_t OtherKey = 0x1234;
// El último del pool, que 0x510f70 no recorre.
constexpr std::size_t LastActor = Client::Kernel::ActorPoolSize - 1;

class SilentPublisher final : public Client::Scenes::IWorldEntryPublisher
{
public:
    void PublishWorldEntry() override {}
};

// Lo que dura la sesión, con Player.bmd, un Item.bmd sin armas marcadas y el registro propio sin equipo,
// como en test_world_entry.cpp.
struct Returning
{
    Returning(std::filesystem::path root, std::uint32_t seed)
        : dataRoot(std::move(root)), cloth{.random = CrtRandom(seed)}
    {
        models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        world.OwnCharacter().ClearEquipmentForEntry();
    }

    [[nodiscard]] WorldEntryResources Resources()
    {
        return WorldEntryResources{.world = world,
                                   .selection = selection,
                                   .dataRoot = dataRoot,
                                   .cloth = cloth,
                                   .camera = camera,
                                   .cameraInput = cameraInput,
                                   .models = models,
                                   .items = &items,
                                   .log = log,
                                   .textMeasure = &letters,
                                   .texts = &texts,
                                   .states = &states};
    }
    // El renglón de bienvenida del F3 03 (0x4c4b60).
    Client::Tests::LetterMeasure letters;
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({});

    [[nodiscard]] std::size_t AliveEffects() const
    {
        const auto effects = world.Map().effects.Effects();
        return static_cast<std::size_t>(
            std::ranges::count_if(effects, [](const auto& effect) { return effect.alive; }));
    }

    std::filesystem::path dataRoot;
    Client::Kernel::WorldState world;
    Client::Session::ModuleStates states;
    Client::Scenes::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput cameraInput;
    Client::Models::ClothWorld cloth;
    Client::Scenes::SingleModelLibrary models;
    Client::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

// Las tablas de los conjuntos sin ningún conjunto, para que la entrada llegue al final.
Client::Tables::ItemSetTables NoSets()
{
    return Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize, std::byte{0xff}));
}

// La respuesta del F3 03 en la celda (x, y) de `map`, con la dirección 0, el PK 4 y el CtlCode 0x20.
Client::Protocol::FrontEnd::CharacterSelectResult ReplyAt(std::uint8_t map, std::uint8_t x, std::uint8_t y)
{
    Client::Protocol::FrontEnd::CharacterSelectResult reply;
    reply.map = map;
    reply.x = x;
    reply.y = y;
    reply.pk = 4;
    reply.controlCode = 0x20;
    return reply;
}

CrtRandom Advanced(CrtRandom random, int draws)
{
    for (int draw = 0; draw < draws; ++draw)
    {
        static_cast<void>(random.Next());
    }
    return random;
}

// Un lugar del pool entre los que mata 0x510f70 que no sea `taken`.
std::size_t SlotOtherThan(std::size_t wanted, std::size_t taken)
{
    return wanted == taken ? wanted + 1 : wanted;
}

struct ReferenceCell
{
    std::uint8_t map = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};
constexpr ReferenceCell Lorencia{.map = 0, .x = 133, .y = 127};
constexpr ReferenceCell Devias{.map = 2, .x = 220, .y = 30};

class ReturnToSelectionTest : public Client::Tests::GoldenDataTest
{
};

// Con el héroe en el mundo, en Lorencia y en Devias: 0x6070a0 suelta los objetos y los efectos sin tocar el
// terreno ni el pasto; 0x510f70(−1) mata a los 400 menos al de la clave 0xffff, también al héroe, y el 401
// queda; la orden de la escena 4, el mundo sin cargar, el nivel de la cámara en 0, el mundo en curso en −1 y
// el registro propio con +0x3c..+0x42 en 0 y su cierre de 3.848 rand(), lo único que saca. El héroe publicado
// sigue en su lugar, y la cámara copia su posición; la selección mira el terreno del juego (Q-C213).
TEST_F(ReturnToSelectionTest, TheReturnReleasesTheGameAndKeepsTheTerrain)
{
    for (const ReferenceCell& probe : {Lorencia, Devias})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        Returning tested(m_dataRoot, Seed);
        const Client::Tables::ItemSetTables sets = NoSets();
        WorldEntryResources resources = tested.Resources();
        resources.itemSetTables = &sets;
        SilentPublisher publisher;
        const auto entered =
            Client::Scenes::ApplyWorldEntry(ReplyAt(probe.map, probe.x, probe.y), PlayerKey, resources, publisher);
        ASSERT_TRUE(entered.HasValue()) << entered.Error();
        Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
        const std::size_t hero = runtime.hero;
        ASSERT_TRUE(tested.world.Actors()[hero].alive);
        ASSERT_FALSE(tested.world.Map().objects.empty());
        ASSERT_GT(tested.AliveEffects(), 0u);
        // Lo que dejó la carga del 54 de la selección (0x633f30) o el paso del juego en el estado 0x3d.
        runtime.drawingEnabled = true;
        tested.camera.level = 2;
        const std::size_t stranger = SlotOtherThan(7, hero);
        const std::size_t survivor = SlotOtherThan(9, hero);
        tested.world.Actors()[stranger].alive = true;
        tested.world.Actors()[stranger].key = OtherKey;
        tested.world.Actors()[survivor].alive = true;
        tested.world.Actors()[survivor].key = SurvivingKey;
        tested.world.Actors()[LastActor].alive = true;
        tested.world.Actors()[LastActor].key = OtherKey;
        Client::Kernel::OwnCharacterRecord& own = tested.world.OwnCharacter();
        own.speedAdjustBits = 3;
        own.word3e = 4;
        own.word40 = 5;
        own.word42 = 6;
        const auto* attributes = tested.states.terrain.current.attributes.get();
        const auto* heights = tested.states.terrain.current.heights.get();
        const std::vector<float> grass = tested.world.Map().grassOffsets;
        const CrtRandom before = tested.cloth.random;

        ASSERT_TRUE(ApplyReturnToSelection(resources).HasValue());

        EXPECT_TRUE(tested.world.Map().objects.empty());
        EXPECT_TRUE(tested.world.Map().objectRecords.empty());
        EXPECT_EQ(tested.AliveEffects(), 0u);
        EXPECT_EQ(tested.states.terrain.current.attributes.get(), attributes);
        EXPECT_EQ(tested.states.terrain.current.heights.get(), heights);
        EXPECT_TRUE(tested.states.terrain.current.mapping);
        EXPECT_TRUE(tested.states.terrain.current.lighting);
        EXPECT_EQ(tested.world.Map().grassOffsets, grass);
        EXPECT_FALSE(tested.world.Actors()[hero].alive);
        EXPECT_FALSE(tested.world.Actors()[stranger].alive);
        EXPECT_TRUE(tested.world.Actors()[survivor].alive);
        EXPECT_TRUE(tested.world.Actors()[LastActor].alive);
        EXPECT_EQ(runtime.hero, hero);
        EXPECT_EQ(runtime.orderedScene, Client::Scenes::SceneId::CharacterSelect);
        EXPECT_FALSE(runtime.drawingEnabled);
        EXPECT_EQ(runtime.map, -1);
        EXPECT_EQ(tested.camera.level, 0);
        EXPECT_EQ(own.speedAdjustBits, 0);
        EXPECT_EQ(own.word3e, 0);
        EXPECT_EQ(own.word40, 0);
        EXPECT_EQ(own.word42, 0);
        EXPECT_EQ(runtime.ownRecordDepth, 0);
        EXPECT_EQ(tested.cloth.random.State(), Advanced(before, OwnRecordClose).State());
        EXPECT_EQ(tested.cameraInput.hero, tested.world.Actors()[hero].position);
        EXPECT_EQ(tested.selection.Terrain(), &tested.states.terrain.current);
        EXPECT_EQ(tested.selection.Map(), -1);
    }
}

// Con el registro propio abierto por otro, el cierre no lo deja sin accesos y no saca rand(); los cuatro
// campos se escriben igual.
TEST(ReturnToSelectionWithoutDataTest, WithTheOwnRecordOpenTheReturnDrawsNoRand)
{
    Returning tested({}, Seed);
    tested.world.Runtime().ownRecordDepth = 1;
    tested.world.OwnCharacter().word40 = 5;
    const CrtRandom before = tested.cloth.random;

    ASSERT_TRUE(ApplyReturnToSelection(tested.Resources()).HasValue());

    EXPECT_EQ(tested.cloth.random.State(), before.State());
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 1);
    EXPECT_EQ(tested.world.OwnCharacter().word40, 0);
    EXPECT_EQ(tested.world.Runtime().orderedScene, Client::Scenes::SceneId::CharacterSelect);
}

// 0x4c26e7 → 0x657670 → 0x420cd0 (CL9 hito 4): la vuelta pone en 0 la pestaña, la ventana y las tres mascotas.
TEST(ReturnToSelectionWithoutDataTest, TheReturnForgetsThePets)
{
    const auto tested = std::make_unique<Returning>(std::filesystem::path{}, Seed);
    Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
    runtime.petTab = 1;
    runtime.panels.delegated.pet = 2;
    runtime.pets[1].level = 30;
    runtime.pets[0].experience = 12;

    const auto returned = ApplyReturnToSelection(tested->Resources());
    ASSERT_TRUE(returned.HasValue()) << returned.Error();

    EXPECT_EQ(runtime.petTab, 0);
    EXPECT_EQ(runtime.panels.delegated.pet, 0);
    EXPECT_EQ(runtime.pets[1].level, 0);
    EXPECT_EQ(runtime.pets[0].experience, 0u);
}

// 0x4c26e7 → 0x657670 → jmp 0x5eb960 (CL9 hito 7): la vuelta vacía las tiendas personales (0x78aba50, 0x6c931c,
// 0x78aba60, el título, los carteles y los dos mapas de los precios, que el próximo Open vuelve a armar), sin tocar la
// copia del dueño, la cuenta de los carteles ni si se dibujan.
TEST(ReturnToSelectionWithoutDataTest, TheReturnEmptiesThePersonalShops)
{
    const auto tested = std::make_unique<Returning>(std::filesystem::path{}, Seed);
    Client::Scenes::PersonalShopState& shops = tested->world.Runtime().social.personalShop;
    shops.ownOpen = 1;
    shops.mode = Client::Scenes::OtherShopView;
    shops.dialogMode = 2;
    shops.ownTitle = "Tienda";
    shops.titles[3] = Client::Scenes::ShopTitle{};
    shops.ownPrices[4] = 100;
    shops.otherPrices[5] = 200;
    shops.pricesBuilt = true;
    shops.ownerKey = 0x1234;
    shops.signBlink = 3;
    shops.signsShown = 0;

    const auto returned = ApplyReturnToSelection(tested->Resources());
    ASSERT_TRUE(returned.HasValue()) << returned.Error();

    EXPECT_EQ(shops.ownOpen, 0);
    EXPECT_EQ(shops.mode, Client::Scenes::NoShopView);
    EXPECT_EQ(shops.dialogMode, 0);
    EXPECT_TRUE(shops.ownTitle.empty());
    EXPECT_TRUE(shops.titles.empty());
    EXPECT_TRUE(shops.ownPrices.empty());
    EXPECT_TRUE(shops.otherPrices.empty());
    EXPECT_FALSE(shops.pricesBuilt);
    EXPECT_EQ(shops.ownerKey, 0x1234);
    EXPECT_EQ(shops.signBlink, 3);
    EXPECT_EQ(shops.signsShown, 0);
}

// 0x4c268a..0x4c26d2: las misiones vuelven a +4 = 0xff (el A0 se vuelve a pedir) y la ventana se cierra sin el 31.
TEST(ReturnToSelectionWithoutDataTest, TheQuestsGoBackToTheirStart)
{
    const auto tested = std::make_unique<Returning>(std::filesystem::path{}, Seed);
    auto& runtime = tested->world.Runtime();
    runtime.quest.characterClass = 1;
    runtime.quest.current = 2;
    runtime.quest.states[0] = 0xaa;
    runtime.panels.delegated.quest = Client::Scenes::QuestListWindow;

    ASSERT_TRUE(ApplyReturnToSelection(tested->Resources()).HasValue());

    EXPECT_EQ(runtime.quest.characterClass, Client::Scenes::QuestClassUnknown);
    EXPECT_EQ(runtime.quest.current, 0);
    EXPECT_EQ(runtime.quest.states[0], 0);
    EXPECT_EQ(runtime.panels.delegated.quest, 0);
}

// 0x4c268a..0x4c26d2: what the quest reset leaves (AR3.2 moves the quests out of WorldRuntime).
TEST(ReturnToSelectionWithoutDataTest, TheQuestResetLeavesTheEvolutionTheRecordsAndTheZen)
{
    const auto tested = std::make_unique<Returning>(std::filesystem::path{}, Seed);
    Client::Scenes::QuestState& quest = tested->world.Runtime().quest;
    quest.evolution = 1;
    quest.flags = {1, 2, 3, 4};
    quest.reached = 5;
    quest.byte21380 = 6;
    quest.byte21381 = 7;
    quest.byte21382 = 8;
    quest.dialog = 9;
    quest.currentState = 10;
    quest.byte21387 = 11;
    quest.zen = 12;
    const std::size_t records = quest.records.size();

    ASSERT_TRUE(ApplyReturnToSelection(tested->Resources()).HasValue());

    EXPECT_EQ(quest.evolution, 1);
    EXPECT_EQ(quest.flags, (std::array<std::uint8_t, 4>{0, 0, 0, 4}));
    EXPECT_EQ(quest.reached, 0);
    EXPECT_EQ(quest.byte21380, 6);
    EXPECT_EQ(quest.byte21381, 0);
    EXPECT_EQ(quest.byte21382, 0);
    EXPECT_EQ(quest.dialog, 0);
    EXPECT_EQ(quest.currentState, 10);
    EXPECT_EQ(quest.byte21387, 11);
    EXPECT_EQ(quest.zen, 12u);
    EXPECT_EQ(quest.records.size(), records);
}

// 0x4c2480 empties the guild's notice and leaves the rest of the guild.
TEST(ReturnToSelectionWithoutDataTest, TheReturnEmptiesOnlyTheGuildNotice)
{
    const auto tested = std::make_unique<Returning>(std::filesystem::path{}, Seed);
    Client::Scenes::GuildState& guild = tested->world.Runtime().guild;
    guild.tableCount = 1;
    guild.memberCount = 2;
    guild.score = 3;
    guild.requester = 4;
    guild.warOn = 1;
    guild.warType = 1;
    guild.warTeam = 2;
    guild.rival[0] = 'R';
    guild.ourScore = 5;
    guild.theirScore = 6;
    guild.rivalIndex = 7;
    guild.heroGuildIndex = 8;
    guild.heroGuildStatus = 9;
    guild.relation = 10;
    guild.createStep = 11;
    guild.kickMember = 12;
    guild.window.tab = 2;
    guild.window.members.emplace_back();
    guild.window.notice.emplace_back();
    guild.window.noticeList.scroll = 3;
    guild.window.unions.emplace_back();
    guild.window.dialog = 13;

    ASSERT_TRUE(ApplyReturnToSelection(tested->Resources()).HasValue());

    EXPECT_TRUE(guild.window.notice.empty());
    EXPECT_EQ(guild.window.noticeList.scroll, 0);
    EXPECT_EQ(guild.tableCount, 1u);
    EXPECT_EQ(guild.memberCount, 2);
    EXPECT_EQ(guild.score, 3);
    EXPECT_EQ(guild.requester, 4);
    EXPECT_EQ(guild.warOn, 1);
    EXPECT_EQ(guild.warType, 1);
    EXPECT_EQ(guild.warTeam, 2);
    EXPECT_EQ(guild.rival[0], 'R');
    EXPECT_EQ(guild.ourScore, 5);
    EXPECT_EQ(guild.theirScore, 6);
    EXPECT_EQ(guild.rivalIndex, 7);
    EXPECT_EQ(guild.heroGuildIndex, 8);
    EXPECT_EQ(guild.heroGuildStatus, 9);
    EXPECT_EQ(guild.relation, 10);
    EXPECT_EQ(guild.createStep, 11);
    EXPECT_EQ(guild.kickMember, 12);
    EXPECT_EQ(guild.window.tab, 2);
    EXPECT_EQ(guild.window.members.size(), 1u);
    EXPECT_EQ(guild.window.unions.size(), 1u);
    EXPECT_EQ(guild.window.dialog, 13u);
}

// 0x4c2480..0x4c249c restarts the siege clock but keeps its hour, minute and tick; the castle's other globals stay.
TEST(ReturnToSelectionWithoutDataTest, TheReturnRestartsTheSiegeClockAndKeepsTheCastle)
{
    const auto tested = std::make_unique<Returning>(std::filesystem::path{}, Seed);
    Client::Kernel::WorldRuntime& runtime = tested->world.Runtime();
    auto& clock = runtime.castleSiege.siegeBattleUi.clock;
    clock = {.blink = 0, .x = 1, .y = 2, .hours = 3, .minutes = 4, .blinkTime = 5.0f, .tick = 6};
    runtime.castleSiege.siegeBattleUi.gateKey = 7;
    runtime.castleSiege.siegeActive = true;
    runtime.castleSiege.previousSiegeActive = true;
    runtime.castleSiege.siegeMusicTime = 8.0f;
    runtime.castleSiege.castleByte7239d8 = 2;
    runtime.castleSiege.castleGatesClosed = 1;
    runtime.castleSiege.castleMarkX = 9.0f;
    runtime.castleSiege.castleMarkY = 10.0f;
    runtime.castleSiege.castleFlags7239e4 = 0x20000;
    runtime.castleSiege.siegePanelKind = std::uint8_t{1};
    runtime.castleSiege.siegePanel.selected = 11;
    runtime.castleSiege.castleSiegeInfo.emplace();
    runtime.castleSiege.data.bytes[0] = 12;
    runtime.castleSiege.huntZone.fee = 13;
    runtime.castleSiege.battle.ownerMarkRevision = 14;

    ASSERT_TRUE(ApplyReturnToSelection(tested->Resources()).HasValue());

    EXPECT_EQ(clock.blink, 1);
    EXPECT_EQ(clock.x, 243);
    EXPECT_EQ(clock.y, 400);
    EXPECT_EQ(clock.blinkTime, 0.0f);
    EXPECT_EQ(clock.hours, 3u);
    EXPECT_EQ(clock.minutes, 4u);
    EXPECT_EQ(clock.tick, 6u);
    EXPECT_EQ(runtime.castleSiege.siegeBattleUi.gateKey, 7);
    EXPECT_TRUE(runtime.castleSiege.siegeActive);
    EXPECT_TRUE(runtime.castleSiege.previousSiegeActive);
    EXPECT_EQ(runtime.castleSiege.siegeMusicTime, 8.0f);
    EXPECT_EQ(runtime.castleSiege.castleByte7239d8, 2);
    EXPECT_EQ(runtime.castleSiege.castleGatesClosed, 1);
    EXPECT_EQ(runtime.castleSiege.castleMarkX, 9.0f);
    EXPECT_EQ(runtime.castleSiege.castleMarkY, 10.0f);
    EXPECT_EQ(runtime.castleSiege.castleFlags7239e4, 0x20000u);
    EXPECT_EQ(runtime.castleSiege.siegePanelKind, 1);
    EXPECT_EQ(runtime.castleSiege.siegePanel.selected, 11);
    EXPECT_TRUE(runtime.castleSiege.castleSiegeInfo.has_value());
    EXPECT_EQ(runtime.castleSiege.data.bytes[0], 12);
    EXPECT_EQ(runtime.castleSiege.huntZone.fee, 13u);
    EXPECT_EQ(runtime.castleSiege.battle.ownerMarkRevision, 14u);
}

// 0x4c2480 empties only the personal shops of the social state: the trade, the party and the duel stay.
TEST(ReturnToSelectionWithoutDataTest, TheReturnKeepsTheTradeThePartyAndTheDuel)
{
    const auto tested = std::make_unique<Returning>(std::filesystem::path{}, Seed);
    Client::Scenes::SocialState& social = tested->world.Runtime().social;
    social.trade.otherLevel = 1;
    social.trade.ownMoney = 2;
    social.trade.ownOk = 1;
    social.party.requestKey = 3;
    social.duel.active = 1;
    social.duel.rivalScore = 4;
    social.confirmBox.id = 5;
    social.confirmBox.lines.emplace_back("line");
    social.dialogFieldLimit = 8;
    social.moneyField = 1;
    social.typedMoney = 6;

    ASSERT_TRUE(ApplyReturnToSelection(tested->Resources()).HasValue());

    EXPECT_EQ(social.trade.otherLevel, 1u);
    EXPECT_EQ(social.trade.ownMoney, 2u);
    EXPECT_EQ(social.trade.ownOk, 1);
    EXPECT_EQ(social.party.requestKey, 3);
    EXPECT_EQ(social.duel.active, 1);
    EXPECT_EQ(social.duel.rivalScore, 4);
    EXPECT_EQ(social.confirmBox.id, 5);
    EXPECT_EQ(social.confirmBox.lines.size(), 1u);
    EXPECT_EQ(social.dialogFieldLimit, 8);
    EXPECT_EQ(social.moneyField, 1);
    EXPECT_EQ(social.typedMoney, 6);
}

// With the trainer's box empty, 0x4c2480 leaves the NPC windows' grids and globals as they were.
TEST(ReturnToSelectionWithoutDataTest, TheReturnKeepsTheNpcWindows)
{
    const auto tested = std::make_unique<Returning>(std::filesystem::path{}, Seed);
    Client::Scenes::NpcWindowState& npc = tested->world.Runtime().npcWindows;
    npc.chaosRates[0] = 1;
    npc.chaosMixState = 2;
    npc.field7d2dc9d = 3;
    npc.warehouseLocked = 1;
    npc.field78abb1c = 4;

    ASSERT_TRUE(ApplyReturnToSelection(tested->Resources()).HasValue());

    EXPECT_EQ(npc.chaosRates[0], 1);
    EXPECT_EQ(npc.chaosMixState, 2);
    EXPECT_EQ(npc.field7d2dc9d, 3);
    EXPECT_EQ(npc.warehouseLocked, 1);
    EXPECT_EQ(npc.field78abb1c, 4);
}

// 0x4c2515..0x4c252d: la vuelta deja los cinco del picking en −1, también el personaje elegido y 0x6c8e38; y
// 0x4c254b, el personaje que se sigue (0x6c8e50).
TEST(ReturnToSelectionWithoutDataTest, TheReturnForgetsThePickedTargets)
{
    const auto tested = std::make_unique<Returning>(std::filesystem::path{}, Seed);
    tested->world.Runtime().selectedTargets = {3, 4, 5, 6, 1};
    tested->world.Runtime().movement.followTarget = 7;

    ASSERT_TRUE(ApplyReturnToSelection(tested->Resources()).HasValue());

    EXPECT_EQ(tested->world.Runtime().selectedTargets, (std::array<std::int32_t, 5>{-1, -1, -1, -1, -1}));
    EXPECT_EQ(tested->world.Runtime().movement.followTarget, -1);
}

// 0x510f70 compara la clave con signo (movsx en 0x510f8a): con −1, la clave 0x7fff muere y la 0xffff no;
// los muertos quedan muertos. El caballo del que muere se apaga (0x510fa1..0x510fc0); el del que queda, no.
TEST(ReturnToSelectionWithoutDataTest, OnlyTheKeyFFFFSurvives)
{
    Returning tested({}, Seed);
    const auto actors = tested.world.Actors();
    actors[1].alive = true;
    actors[1].key = 0x7fff;
    actors[2].alive = true;
    actors[2].key = SurvivingKey;
    actors[3].key = SurvivingKey;
    auto& helpers = tested.world.Helpers();
    helpers[0] = Client::Kernel::HelperState{.alive = true, .type = Client::Scenes::DarkHorseModel, .rider = 1};
    helpers[1] = Client::Kernel::HelperState{.alive = true, .type = Client::Scenes::DarkHorseModel, .rider = 2};

    ASSERT_TRUE(ApplyReturnToSelection(tested.Resources()).HasValue());

    EXPECT_FALSE(actors[1].alive);
    EXPECT_TRUE(actors[2].alive);
    EXPECT_FALSE(actors[3].alive);
    EXPECT_FALSE(helpers[0].alive);
    EXPECT_TRUE(helpers[1].alive);
}
TEST(ReturnToSelectionPoolsTest, ReleasesTheLiveFlagsWithoutReplacingRetainedRecords)
{
    Returning tested({}, Seed);
    tested.world.Fauna().Birds()[0].active = true;
    tested.world.Leaves().All()[0].alive = true;
    tested.world.Leaves().All()[0].type = 123;
    tested.world.DrawTail().damage[2].alive = true;
    tested.world.DrawTail().damage[2].number = 567;
    tested.world.DrawTail().floorItems[3].alive = true;
    tested.world.DrawTail().joints[4].alive = true;
    tested.world.DrawTail().ribbons[5].alive = true;
    ASSERT_TRUE(ApplyReturnToSelection(tested.Resources()).HasValue());
    EXPECT_FALSE(tested.world.Fauna().Birds()[0].active);
    EXPECT_FALSE(tested.world.Leaves().All()[0].alive);
    EXPECT_EQ(tested.world.Leaves().All()[0].type, 123);
    EXPECT_FALSE(tested.world.DrawTail().damage[2].alive);
    EXPECT_EQ(tested.world.DrawTail().damage[2].number, 567);
    EXPECT_FALSE(tested.world.DrawTail().floorItems[3].alive);
    EXPECT_FALSE(tested.world.DrawTail().joints[4].alive);
    EXPECT_FALSE(tested.world.DrawTail().ribbons[5].alive);
}
// 4c2546 y657686: observar el mundo desde el callback de red detecta envíos adelantados/tardíos.
TEST(ReturnToSelectionWithoutDataTest, TheTwoClosesObserveTheirOwnNativeResetCheckpoints)
{
    const auto tested = std::make_unique<Returning>(std::filesystem::path{}, Seed);
    auto& runtime = tested->world.Runtime();
    runtime.map = 30;
    runtime.movement.followTarget = 17;
    runtime.quest.current = 2;
    runtime.panels.delegated.quest = Client::Scenes::QuestListWindow;
    runtime.panels.delegated.trainer = 3;
    runtime.panels.delegated.field724584 = 9;
    runtime.pets[1].level = 30;
    auto& dialog = runtime.panels.delegated.panelDialog;
    dialog.opened = 77;
    dialog.owner = Client::UI::PanelDialogOwner::GuildJoin;
    dialog.timerMs = 777;
    runtime.chat.input.dialogFieldOn = true;
    tested->world.OwnCharacter().word3e = 55;
    auto resources = tested->Resources();
    std::size_t closes = 0;
    resources.sendWindowClose = [&]
    {
        EXPECT_EQ(runtime.map, -1);
        EXPECT_EQ(runtime.panels.delegated.quest, 0);
        EXPECT_EQ(runtime.pets[1].level, 30);
        if (closes++ == 0)
        {
            EXPECT_EQ(runtime.movement.followTarget, 17);
            EXPECT_EQ(tested->world.OwnCharacter().word3e, 55);
            EXPECT_EQ(runtime.quest.current, 2);
            EXPECT_EQ(dialog.opened, 77u);
            EXPECT_EQ(runtime.panels.delegated.trainer, 3);
            EXPECT_EQ(tested->cloth.random.State(), CrtRandom(Seed).State());
        }
        else
        {
            EXPECT_EQ(runtime.movement.followTarget, -1);
            EXPECT_EQ(tested->world.OwnCharacter().word3e, 0);
            EXPECT_EQ(runtime.quest.current, 0);
            EXPECT_EQ(dialog.opened, 0u);
            EXPECT_EQ(runtime.panels.delegated.trainer, 0);
            EXPECT_EQ(runtime.panels.delegated.field724584, 0);
            EXPECT_EQ(tested->cloth.random.State(), Advanced(CrtRandom(Seed), 0xf08).State());
        }
    };
    const auto result = ApplyReturnToSelection(resources);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(closes, 2u);
    EXPECT_EQ(runtime.pets[1].level, 0);
    EXPECT_EQ(dialog.opened, 0u);
    EXPECT_EQ(dialog.timerMs, 777u);
    EXPECT_TRUE(runtime.chat.input.dialogFieldOn); // 660090 no llama callback ni hace cleanup delEDIT.
}

class ResetLetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

TEST(ReturnToSelectionWithoutDataTest, AnOccupiedTrainerWarnsSkipsTheSecondCloseAndContinuesTheReset)
{
    const auto tested = std::make_unique<Returning>(std::filesystem::path{}, Seed);
    auto& runtime = tested->world.Runtime();
    runtime.npcWindows.trainerBox[31].id = 1;
    runtime.npcWindows.trainerBox[31].occupied = 0; // 449c10 mira eltipo, no occupied.
    runtime.panels.delegated.trainer = 3;
    runtime.panels.delegated.field724584 = 9;
    runtime.pets[1].level = 30;
    const ResetLetterMeasure measure;
    const auto texts = Client::Tests::MakeTextTable({{1211, "caja ocupada"}});
    auto resources = tested->Resources();
    resources.textMeasure = &measure;
    resources.texts = &texts;
    std::size_t closes = 0;
    resources.sendWindowClose = [&] { ++closes; };
    const auto result = ApplyReturnToSelection(resources);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(closes, 1u);
    EXPECT_EQ(runtime.panels.delegated.trainer, 3);
    EXPECT_EQ(runtime.panels.delegated.field724584, 9);
    EXPECT_EQ(runtime.pets[1].level, 0);
    ASSERT_EQ(runtime.chat.log.entries.size(), 1u);
    EXPECT_EQ(runtime.chat.log.entries[0].text, "caja ocupada");
    EXPECT_EQ(runtime.chat.log.entries[0].type, 2);
}

} // namespace
