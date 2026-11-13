#include "Scenes/NpcTalk.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Data/Text/TextTable.h"
#include "GameLogic/Path.h"
#include "GameLogic/PathCell.h"
#include "Protocol/Movement.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameMovement.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/MessageArea.h"
#include "UI/PanelRegistry.h"
#include "World/TerrainWalls.h"
#include "World/WorldTerrain.h"

#include "support/GoldenDir.h"
#include "support/SceneDataFiles.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// Hablarle a un NPC (CL9 hito 2): el clic de MoveHero sobre el NPC elegido (0x59da75..0x59dc59) y la llegada
// (0x5650c0 con +0x379 en 2, 0x5674c8..0x5695b3) con el C1 30. Textos y claves inventados; el mundo en Lorencia.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::HeroMovementContext;
using Client::Scenes::WorldState;

constexpr std::size_t NpcSlot = 7;
constexpr std::uint16_t TalkingWord = 0xf0;
constexpr std::uint16_t NpcKey = 0x8123;
constexpr std::size_t LevelText = 663;
constexpr std::size_t FlagText = 1830;
// El registro propio (0x40b4d0) y un C3 (el relleno y el serial).
constexpr int OwnRecordRandoms = 3848;
constexpr int C3Randoms = 2;
// Lo que saca el paso antes del clic (los dos cierres del contador de 0x6c8c10 y el registro del clic) y después del
// D3 (el comercio y la tienda de NPC de 0x56b397.., el registro propio y Skill.bmd del prefijo de la acción).
constexpr int BeforeTheClickRandoms = 8 + OwnRecordRandoms;
constexpr int AfterTheWalkRandoms = 2 + OwnRecordRandoms + 60;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

class TypedLetters final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

class TalkingHero
{
public:
    TalkingHero()
    {
        Client::Scenes::WorldRuntime& runtime = world.Runtime();
        runtime.map = 0;
        // Las misiones ya tienen la clase: sin el A0.
        runtime.quest.characterClass = 0;
        auto& terrain = world.Map().terrain;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        terrain.mapping = std::make_unique<Client::Data::Terrain::TerrainMapping>();
        ActorState& hero = Hero();
        hero.alive = true;
        hero.type = Client::Data::Models::PlayerSkeletonModel;
        hero.cellX = hero.targetX = 182;
        hero.cellY = hero.targetY = 115;
        hero.position = {18250.0f, 11550.0f, 0.0f};
        hero.hands[0].model = hero.hands[1].model = hero.wings.model = hero.pet.model = Client::Scenes::NoItemModel;
        hero.alpha = 1.0f;
        hero.byte88 = 1;
        hero.baseMovementSpeed = 10;
        ActorState& npc = Npc();
        npc.alive = true;
        npc.type = 0xf9;
        npc.position = {18550.0f, 11550.0f, 0.0f};
        npc.angles.z = 90.0f;
        npc.word376 = TalkingWord;
        npc.key = NpcKey;
        skeleton.actions.resize(205);
        for (auto& action : skeleton.actions)
            action.keyCount = 10;
    }

    ActorState& Hero()
    {
        return world.Actors()[0];
    }
    ActorState& Npc()
    {
        return world.Actors()[NpcSlot];
    }

    HeroMovementContext Context()
    {
        HeroMovementContext context{.world = world, .random = random, .playerSkeleton = &skeleton, .skills = &skills};
        context.sendWalk = [this](std::span<const Client::Protocol::Movement::Cell> /*path*/,
                                  Client::Protocol::Movement::Cell target,
                                  float /*angle*/) -> Client::Core::Expected<void, std::string>
        {
            events.push_back(std::format("D3 {} {}", target.x, target.y));
            randomAtTheWalk = random;
            return {};
        };
        context.sendTalk = [this](std::int16_t key)
        { events.push_back(std::format("30 {:04x}", static_cast<std::uint16_t>(key))); };
        context.requestQuestList = [this]() { events.push_back("A0"); };
        context.sendWindowClose = [this]() { events.push_back("31"); };
        context.texts = &texts;
        context.measure = &letters;
        return context;
    }

    // Un clic con el NPC elegido (0x6c8e2c) y la ventana de MouseUpdateTime vencida.
    Client::Core::Expected<void, std::string> Click()
    {
        world.Runtime().selectedTargets[1] = static_cast<std::int32_t>(NpcSlot);
        world.Runtime().movement.mouseUpdateTime = 6;
        auto context = Context();
        context.input.leftPressed = true;
        context.input.leftDown = true;
        context.pickTarget = [this]() -> Client::Core::Expected<std::optional<Client::GameLogic::PathCell>, std::string>
        {
            events.emplace_back("pick");
            return std::optional<Client::GameLogic::PathCell>{};
        };
        return Client::Scenes::StepHeroMovement(context);
    }

    // La llegada con el NPC en 0x6c8e54 y el héroe caminando (+0x378 en 1).
    Client::Core::Expected<void, std::string> Arrive()
    {
        world.Runtime().movement.chosenNpc = static_cast<std::int32_t>(NpcSlot);
        Hero().movement = 1;
        return Client::Scenes::TalkToTheNpc(Hero(), Context());
    }

    bool Said(std::string_view text, std::int32_t type)
    {
        return std::ranges::any_of(world.Runtime().chat.area.rows, [&](const Client::UI::MessageAreaRow& row)
                                   { return row.text == text && row.type == type; });
    }

    WorldState world;
    CrtRandom random;
    std::optional<CrtRandom> randomAtTheWalk;
    Client::Render::Models::Skeleton skeleton;
    Client::Data::Tables::SkillTable skills{std::vector<std::byte>(Client::Data::Tables::SkillTableSize)};
    TypedLetters letters;
    Client::Data::Text::TextTable texts =
        Client::Tests::MakeTextTable({{LevelText, "hace falta el nivel %d"}, {FlagText, "no con eso puesto"}});
    std::vector<std::string> events;
};

// Lo que saca 0x4f2250 al parar al héroe del fixture, desde `start`, sobre un gemelo.
CrtRandom AfterTheStop(const CrtRandom& start)
{
    const auto twin = std::make_unique<TalkingHero>();
    twin->random = start;
    const Client::Scenes::ActorStopContext stop{twin->world.Runtime(),
                                                Client::Scenes::SceneId::Game,
                                                twin->skeleton,
                                                nullptr,
                                                twin->random,
                                                &twin->world.Map().terrain.attributes->cells,
                                                {}};
    EXPECT_TRUE(Client::Scenes::SetActorStop(twin->Hero(), stop).HasValue());
    return twin->random;
}

// 0x59da75..0x59dc45: el clic va hasta la celda del NPC, con +0x379 en 2 y el NPC, su tipo y su ángulo guardados; la
// tienda de NPC saca su rand() antes de las celdas del héroe.
TEST(NpcTalkTest, AClickOnTheChosenNpcWalksToItsCell)
{
    const auto tested = std::make_unique<TalkingHero>();
    const CrtRandom start = tested->random;
    const auto clicked = tested->Click();
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    EXPECT_EQ(tested->events, (std::vector<std::string>{"D3 185 115"}));
    ASSERT_TRUE(tested->randomAtTheWalk.has_value());
    EXPECT_EQ(tested->randomAtTheWalk->State(), Advanced(start, BeforeTheClickRandoms + 1 + 8).State());
    EXPECT_EQ(tested->random.State(), Advanced(start, BeforeTheClickRandoms + 1 + 8 + AfterTheWalkRandoms).State());
    const auto& movement = tested->world.Runtime().movement;
    EXPECT_EQ(tested->Hero().movementType, Client::Scenes::NpcTalkMovement);
    EXPECT_EQ(tested->Hero().movement, 1);
    EXPECT_EQ(movement.attackTarget, 7);
    EXPECT_EQ(movement.chosenNpc, 7);
    EXPECT_EQ(movement.chosenNpcType, 0xf9);
    EXPECT_EQ(movement.chosenNpcAngle, 0x42b40000u);
    EXPECT_EQ(movement.targetX, 185u);
    EXPECT_EQ(movement.targetY, 115u);
    // 0x59dc48: sigue en 0x59e248, que suma MouseUpdateTime.
    EXPECT_EQ(movement.mouseUpdateTime, 1);
}

// 0x59dab8: con el bit 0x10000000 de +0x7c el clic camina igual, pero +0x379 queda como estaba.
TEST(NpcTalkTest, TheBlockingFlagWalksWithoutTheTalk)
{
    const auto tested = std::make_unique<TalkingHero>();
    tested->Hero().flags7c = Client::Scenes::NpcTalkBlockedFlag;
    const auto clicked = tested->Click();
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    EXPECT_EQ(tested->events, (std::vector<std::string>{"D3 185 115"}));
    EXPECT_EQ(tested->Hero().movementType, 0);
    EXPECT_EQ(tested->world.Runtime().movement.chosenNpc, 7);
}

// 0x59da82..0x59daa9: con la tienda de NPC o el almacén abiertos, el clic sigue en el terreno.
TEST(NpcTalkTest, TheShopOrTheWarehouseSendTheClickToTheGround)
{
    for (const bool shop : {true, false})
    {
        SCOPED_TRACE(shop);
        const auto tested = std::make_unique<TalkingHero>();
        auto& panels = tested->world.Runtime().panels;
        (shop ? panels.npcShop : panels.warehouse) = 1;
        const auto clicked = tested->Click();
        ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
        EXPECT_EQ(tested->events, (std::vector<std::string>{"pick"}));
        EXPECT_EQ(tested->world.Runtime().movement.chosenNpc, -1);
        EXPECT_EQ(tested->world.Runtime().movement.attackTarget, -1);
    }
}

// 0x59dc4d: sin camino (el NPC en la celda del héroe), la llegada es en el lugar y le habla.
TEST(NpcTalkTest, WithoutAPathTheHeroTalksInPlace)
{
    const auto tested = std::make_unique<TalkingHero>();
    tested->Npc().position = tested->Hero().position;
    const auto clicked = tested->Click();
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    EXPECT_EQ(tested->events, (std::vector<std::string>{"30 8123"}));
    EXPECT_EQ(tested->world.Runtime().movement.chosenNpc, -1);
    EXPECT_EQ(tested->world.Runtime().movement.mouseUpdateTimeMax, 12);
    EXPECT_EQ(tested->world.Runtime().movement.mouseUpdateTime, 1);
    EXPECT_EQ(tested->Hero().movementType, Client::Scenes::NpcTalkMovement);
}

// 0x59b2f7: el héroe camina hasta el NPC y, al terminar el camino, 0x5650c0 con +0x379 en 2 le habla.
TEST(NpcTalkTest, TheHeroTalksWhenItArrives)
{
    const auto tested = std::make_unique<TalkingHero>();
    ASSERT_TRUE(tested->Click().HasValue());
    ActorState& hero = tested->Hero();
    ASSERT_EQ(hero.movement, 1);
    constexpr int StepLimit = 100;
    for (int step = 0; step < StepLimit && hero.movement != 0; ++step)
    {
        const auto stepped = Client::Scenes::StepHeroMovement(tested->Context());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    }
    EXPECT_EQ(tested->events, (std::vector<std::string>{"D3 185 115", "30 8123"}));
    EXPECT_EQ(hero.movement, 0);
    EXPECT_EQ(hero.cellX, 185);
    EXPECT_EQ(tested->world.Runtime().movement.chosenNpc, -1);
    EXPECT_EQ(tested->world.Runtime().movement.mouseUpdateTimeMax, 12);
}

// 0x59dc32: un NPC fuera del mapa corta.
TEST(NpcTalkTest, AnNpcOutsideTheMapIsCut)
{
    const auto tested = std::make_unique<TalkingHero>();
    tested->Npc().position.x = 25700.0f;
    const auto clicked = tested->Click();
    ASSERT_FALSE(clicked.HasValue());
    EXPECT_NE(clicked.Error().find("0x59dc32"), std::string::npos) << clicked.Error();
}

// 0x4eac0a..0x4eac41 desde 0x59dc32: con Crywolf activo, el altar recién elegido (0x59db1f) le pone la bandera al A*,
// que pone en 0 el word de su celda (0x4eae0d) aunque sea pared: el camino llega a la celda del altar. Con la etapa 2
// la meta queda y el clic va al más cercano.
TEST(NpcTalkTest, TheWalkToACrywolfAltarClearsItsCell)
{
    for (const std::uint8_t stage : {std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{4}})
    {
        SCOPED_TRACE(stage);
        const auto tested = std::make_unique<TalkingHero>();
        tested->world.Runtime().map = 34;
        tested->world.Runtime().crywolf.stage = stage;
        tested->Npc().type = 0x205;
        auto& walls = tested->world.Map().terrain.attributes->cells;
        const std::size_t altar = Client::Data::Terrain::CellIndex(185, 115);
        walls[altar] = 0x06;
        const auto clicked = tested->Click();
        ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
        EXPECT_EQ(tested->events, (std::vector<std::string>{"D3 185 115"}));
        const auto& path = tested->Hero().path;
        ASSERT_GE(path.count, 2);
        const Client::GameLogic::PathCell last = path.cells[path.count - 1U];
        if (stage == 2)
        {
            EXPECT_EQ(walls[altar], 0x06);
            // El más cercano de 0x4eb0f4..0x4eb3fc: el primero con la heurística 4 es la diagonal de arriba.
            EXPECT_EQ(last, (Client::GameLogic::PathCell{184, 114}));
        }
        else
        {
            EXPECT_EQ(walls[altar], 0);
            EXPECT_EQ(last, (Client::GameLogic::PathCell{185, 115}));
        }
    }
}

// 0x59e21f → 0x4eac00 (Q-C1351): el clic en el suelo también lleva la bandera si 0x6c8e54 todavía tiene un altar (un
// camino a un altar que el clic cortó). En el 34 con la etapa 4 la celda de pared del clic queda en 0 y el camino
// llega; en Lorencia y Devias, el más cercano.
TEST(NpcTalkTest, AGroundClickWithAChosenAltarAlsoClearsItsCell)
{
    for (const std::int32_t map : {0, 2, 34})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<TalkingHero>();
        auto& runtime = tested->world.Runtime();
        runtime.map = map;
        runtime.crywolf.stage = 4;
        runtime.movement.chosenNpc = static_cast<std::int32_t>(NpcSlot);
        tested->Npc().type = 0x205;
        auto& walls = tested->world.Map().terrain.attributes->cells;
        const std::size_t clicked = Client::Data::Terrain::CellIndex(186, 115);
        walls[clicked] = 0x04;
        runtime.movement.mouseUpdateTime = 6;
        auto context = tested->Context();
        context.input.leftPressed = true;
        context.input.leftDown = true;
        context.pickTarget = []() -> Client::Core::Expected<std::optional<Client::GameLogic::PathCell>, std::string>
        { return std::optional<Client::GameLogic::PathCell>{Client::GameLogic::PathCell{186, 115}}; };
        const auto stepped = Client::Scenes::StepHeroMovement(context);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested->events, (std::vector<std::string>{"D3 186 115"}));
        const auto& path = tested->Hero().path;
        ASSERT_GE(path.count, 2);
        const Client::GameLogic::PathCell last = path.cells[path.count - 1U];
        if (map == 34)
        {
            EXPECT_EQ(walls[clicked], 0);
            EXPECT_EQ(last, (Client::GameLogic::PathCell{186, 115}));
        }
        else
        {
            EXPECT_EQ(walls[clicked], 0x04);
            EXPECT_EQ(last, (Client::GameLogic::PathCell{185, 114}));
        }
        EXPECT_EQ(runtime.movement.chosenNpc, static_cast<std::int32_t>(NpcSlot));
    }
}

// 0x5674c8..0x5695b3: parar al héroe, el registro propio, el C3 30 con la clave y 0x6c8e54 en −1; el máximo de
// MouseUpdateTime en 12 y las banderas de los paneles en 0.
TEST(NpcTalkTest, TheArrivalStopsTheHeroAndSendsTheTalk)
{
    const auto tested = std::make_unique<TalkingHero>();
    auto& runtime = tested->world.Runtime();
    runtime.panels.repairMode = 2;
    runtime.panels.field78abb09 = 1;
    runtime.byte6eeda8 = 1;
    const CrtRandom expected = Advanced(AfterTheStop(tested->random), OwnRecordRandoms + C3Randoms);
    const auto arrived = tested->Arrive();
    ASSERT_TRUE(arrived.HasValue()) << arrived.Error();
    EXPECT_EQ(tested->events, (std::vector<std::string>{"30 8123"}));
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->Hero().movement, 0);
    EXPECT_EQ(runtime.movement.mouseUpdateTimeMax, 12);
    EXPECT_EQ(runtime.movement.chosenNpc, -1);
    EXPECT_EQ(runtime.panels.repairMode, 0);
    EXPECT_EQ(runtime.panels.field78abb09, 0);
    EXPECT_EQ(runtime.byte6eeda8, 0);
}

// 0x5674f4 y 0x56751b: sin NPC elegido solo para; con un +0x376 de menos de 0xcd, 0x6c8e54 en −1 sin el registro.
TEST(NpcTalkTest, WithoutANpcOrWithAnotherWordTheArrivalOnlyStops)
{
    {
        const auto tested = std::make_unique<TalkingHero>();
        const CrtRandom expected = AfterTheStop(tested->random);
        tested->Hero().movement = 1;
        ASSERT_TRUE(Client::Scenes::TalkToTheNpc(tested->Hero(), tested->Context()).HasValue());
        EXPECT_TRUE(tested->events.empty());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Hero().movement, 0);
    }
    {
        const auto tested = std::make_unique<TalkingHero>();
        tested->Npc().word376 = 0xcc;
        tested->world.Runtime().byte6eeda8 = 1;
        const CrtRandom expected = AfterTheStop(tested->random);
        ASSERT_TRUE(tested->Arrive().HasValue());
        EXPECT_TRUE(tested->events.empty());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->world.Runtime().movement.chosenNpc, -1);
        EXPECT_EQ(tested->world.Runtime().byte6eeda8, 1);
    }
}

// 0x567618: el 0xee no le habla a un héroe de menos de nivel 10: el aviso con el sprintf y 0x6c8e54 queda.
TEST(NpcTalkTest, TheLevelLimitedNpcNeedsLevelTen)
{
    for (const std::uint16_t level : {std::uint16_t{9}, std::uint16_t{10}})
    {
        SCOPED_TRACE(level);
        const auto tested = std::make_unique<TalkingHero>();
        tested->Npc().word376 = 0xee;
        tested->world.OwnCharacter().level = level;
        ASSERT_TRUE(tested->Arrive().HasValue());
        if (level < 10)
        {
            EXPECT_TRUE(tested->events.empty());
            EXPECT_TRUE(tested->Said("hace falta el nivel 10", Client::UI::SystemMessage));
            EXPECT_EQ(tested->world.Runtime().movement.chosenNpc, 7);
        }
        else
        {
            EXPECT_EQ(tested->events, (std::vector<std::string>{"30 8123"}));
            EXPECT_EQ(tested->world.Runtime().movement.chosenNpc, -1);
        }
    }
}

// 0x567aaf..0x567ae5: el 0x101 con el bit 0x2000000 de +0x78 del héroe da el aviso de error, sin el 30 ni 0x6eeda8.
TEST(NpcTalkTest, TheFlagLimitedNpcRefusesTheFlaggedHero)
{
    const auto tested = std::make_unique<TalkingHero>();
    tested->Npc().word376 = 0x101;
    tested->Hero().flags78 = 0x2000000;
    tested->world.Runtime().byte6eeda8 = 1;
    ASSERT_TRUE(tested->Arrive().HasValue());
    EXPECT_TRUE(tested->events.empty());
    EXPECT_TRUE(tested->Said("no con eso puesto", Client::UI::ErrorMessage));
    EXPECT_EQ(tested->world.Runtime().movement.chosenNpc, -1);
    EXPECT_EQ(tested->world.Runtime().byte6eeda8, 1);
}

// 0x567652..0x56766b y 0x569586: 0x78abb09 con 0xf3, 0xf6 y 0xfb; 0x6eeda8 con 0xe5.
TEST(NpcTalkTest, SomeWordsLeaveTheirFlags)
{
    for (const std::uint16_t word : std::to_array<std::uint16_t>({0xf3, 0xf6, 0xfb, 0xe5, 0xcd}))
    {
        SCOPED_TRACE(word);
        const auto tested = std::make_unique<TalkingHero>();
        tested->Npc().word376 = word;
        ASSERT_TRUE(tested->Arrive().HasValue());
        const auto& runtime = tested->world.Runtime();
        EXPECT_EQ(runtime.panels.field78abb09, word == 0xf3 || word == 0xf6 || word == 0xfb ? 1 : 0);
        EXPECT_EQ(runtime.byte6eeda8, word == 0xe5 ? 1 : 0);
        EXPECT_EQ(tested->events, (std::vector<std::string>{"30 8123"}));
    }
}

// 0x567672..0x567a85: la ventana de las misiones se cierra con el C1 03 31 y, sin la clase en las misiones, va el A0
// antes del 30, cada C3 con sus dos rand().
TEST(NpcTalkTest, TheQuestWindowClosesAndTheQuestListGoesFirst)
{
    const auto tested = std::make_unique<TalkingHero>();
    auto& runtime = tested->world.Runtime();
    runtime.panels.delegated.quest = 1;
    runtime.quest.characterClass = 0xff;
    const CrtRandom expected = Advanced(AfterTheStop(tested->random), OwnRecordRandoms + C3Randoms + C3Randoms);
    const auto arrived = tested->Arrive();
    ASSERT_TRUE(arrived.HasValue()) << arrived.Error();
    EXPECT_EQ(tested->events, (std::vector<std::string>{"31", "A0", "30 8123"}));
    EXPECT_EQ(runtime.panels.delegated.quest, 0);
    EXPECT_EQ(tested->random.State(), expected.State());
}

// 0x5674d2 y 0x567b00..0x5688fd: con Crywolf activo el héroe no se para; frente a un altar, una elfa no habla (lo que
// muestra, abajo) y los demás sí. En el 34 sin el evento, un altar no habla, pero 0x6c8e54 vuelve a −1; en Lorencia y
// en Devias, con cualquier etapa, un modelo de altar es un NPC más.
TEST(NpcTalkTest, CrywolfAltarsTalkOnlyWhenTheyCan)
{
    struct Case
    {
        std::int32_t map = 34;
        std::uint8_t stage = 0;
        std::uint16_t model = 0;
        std::uint8_t characterClass = 0;
        bool talks = false;
    };
    for (const Case& tested : {Case{34, 3, 0x204, 2, false}, Case{34, 4, 0x208, 0, true}, Case{34, 3, 0x1f0, 2, true},
                               Case{34, 0, 0x206, 0, false}, Case{34, 0, 0x209, 0, true}, Case{0, 3, 0x204, 2, true},
                               Case{2, 4, 0x206, 2, true}})
    {
        SCOPED_TRACE(std::format("{} {} {:#x} {}", tested.map, tested.stage, tested.model, tested.characterClass));
        const auto hero = std::make_unique<TalkingHero>();
        auto& runtime = hero->world.Runtime();
        runtime.map = tested.map;
        runtime.crywolf.stage = tested.stage;
        hero->Npc().type = tested.model;
        hero->Hero().characterClass = tested.characterClass;
        hero->texts = Client::Tests::MakeTextTable(
            {{2002, "altar tomado"}, {2003, "altar sin contratos"}, {2006, "quedan %d"}, {2007, "contratar?"}});
        const CrtRandom start = hero->random;
        const auto arrived = hero->Arrive();
        ASSERT_TRUE(arrived.HasValue()) << arrived.Error();
        EXPECT_EQ(hero->events, tested.talks ? std::vector<std::string>{"30 8123"} : std::vector<std::string>{});
        EXPECT_EQ(runtime.movement.chosenNpc, -1);
        const bool crywolf = tested.map == 34 && tested.stage != 0;
        EXPECT_EQ(hero->Hero().movement, crywolf ? 1 : 0);
        if (crywolf)
        {
            EXPECT_EQ(hero->random.State(), Advanced(start, OwnRecordRandoms + (tested.talks ? C3Randoms : 0)).State());
        }
    }
}

// 0x567b4c..0x567c23 (CL9 hito 11): una elfa frente a un altar, con Crywolf activo, no manda el C1 30: con el altar
// contratado, 0x435b00(0x34); sin contratos, 0x435b00(0x35); si no, 0x435b00(0x38) con los contratos que quedan y la
// pregunta 0x39 con la clave del altar. Las tres siguen en 0x569573 (0x6eeda8 por el +0x376 y 0x6c8e54 en −1).
TEST(NpcTalkTest, AnElfAtACrywolfAltarSeesItsStateInsteadOfTalking)
{
    struct Case
    {
        std::uint8_t altar = 0;
        std::string_view first;
        std::string_view second;
        std::uint8_t window = 0;
        std::int32_t value = 0;
    };
    constexpr std::uint16_t AltarKey = 0x8140;
    for (const Case& tested : {Case{0x13, "altar tomado", "", 2, 0}, Case{0x20, "altar sin contratos", "", 2, 0},
                               Case{0x03, "quedan 3", "contratar?", 1, static_cast<std::int16_t>(AltarKey)}})
    {
        SCOPED_TRACE(std::format("{:#x}", tested.altar));
        for (const std::uint8_t stage : {std::uint8_t{3}, std::uint8_t{4}})
        {
            SCOPED_TRACE(stage);
            const auto hero = std::make_unique<TalkingHero>();
            auto& runtime = hero->world.Runtime();
            runtime.map = 34;
            runtime.crywolf.stage = stage;
            runtime.crywolf.ui.altars[2] = tested.altar;
            hero->Npc().type = 0x206;
            hero->Npc().key = AltarKey;
            hero->Npc().word376 = 0xe5;
            hero->Hero().characterClass = 2;
            hero->texts = Client::Tests::MakeTextTable(
                {{2002, "altar tomado"}, {2003, "altar sin contratos"}, {2006, "quedan %d"}, {2007, "contratar?"}});
            const CrtRandom start = hero->random;
            const auto arrived = hero->Arrive();
            ASSERT_TRUE(arrived.HasValue()) << arrived.Error();
            EXPECT_TRUE(hero->events.empty());
            const auto& ui = runtime.crywolf.ui;
            EXPECT_EQ(std::string_view(ui.contractLines[0].data()), tested.first);
            EXPECT_EQ(std::string_view(ui.contractLines[1].data()), tested.second);
            EXPECT_EQ(ui.byte723a44, tested.window);
            EXPECT_EQ(ui.dwords723bdc[1], tested.value);
            EXPECT_EQ(runtime.movement.chosenNpc, -1);
            EXPECT_EQ(runtime.byte6eeda8, 1);
            EXPECT_EQ(hero->Hero().movement, 1);
            EXPECT_EQ(hero->random.State(), Advanced(start, OwnRecordRandoms).State());
        }
    }
}

// El camino hasta un altar de Crywolf con el Data del repo (0x59dc32 → 0x4eac00 → 0x4ead70), en Lorencia, Devias y
// Crywolf, con la etapa 4 en los tres. El altar está en una celda que el .att tiene de pared (0x04, sola entre celdas
// libres), con el 0x02 de personaje (0x509165), y la elfa cuatro celdas al este. Solo en el 34 la bandera (0x4eac41)
// pone en 0 la celda del altar: el A* exacto llega a ella y la llegada (0x567b00) muestra la oferta del contrato. En el
// 0 y el 2 el exacto falla en la meta (0x4eae38), el más cercano para en la diagonal de arriba y la llegada le habla
// con el C1 30.
class NpcTalkDataTest : public Client::Tests::GoldenDataTest
{
};

TEST_F(NpcTalkDataTest, OnlyInCrywolfTheWalkReachesTheAltarCell)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t altarX;
        std::uint8_t altarY;
    };
    constexpr std::int32_t CrywolfMap = 34;
    constexpr std::uint8_t Away = 4;
    for (const Case& probe : {Case{0, 172, 47}, Case{2, 181, 109}, Case{CrywolfMap, 116, 114}})
    {
        SCOPED_TRACE(probe.map);
        const auto tested = std::make_unique<TalkingHero>();
        auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, static_cast<std::uint8_t>(probe.map));
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        tested->world.Map().terrain = std::move(loaded).Value();
        auto& runtime = tested->world.Runtime();
        runtime.map = probe.map;
        runtime.crywolf.stage = 4;
        const auto heroX = static_cast<std::uint8_t>(probe.altarX + Away);
        const auto center = [](std::uint8_t cell) { return (static_cast<float>(cell) * 100.0f) + 50.0f; };
        ActorState& hero = tested->Hero();
        hero.cellX = hero.targetX = heroX;
        hero.cellY = hero.targetY = probe.altarY;
        hero.position = {center(heroX), center(probe.altarY), 0.0f};
        hero.characterClass = 2;
        ActorState& altar = tested->Npc();
        altar.type = 0x206;
        altar.key = 0x013e;
        altar.position = {center(probe.altarX), center(probe.altarY), 0.0f};
        auto& walls = tested->world.Map().terrain.attributes->cells;
        const std::size_t cell = Client::Data::Terrain::CellIndex(probe.altarX, probe.altarY);
        ASSERT_EQ(walls[cell], Client::World::BlockedWallBit);
        for (std::uint8_t x = probe.altarX + 1; x <= heroX; ++x)
            ASSERT_LT(walls[Client::Data::Terrain::CellIndex(x, probe.altarY)], 2) << int{x};
        Client::World::MarkCharacterWall(walls, probe.altarX, probe.altarY);
        tested->texts = Client::Tests::MakeTextTable({{2006, "quedan %d"}, {2007, "contratar?"}});

        const auto clicked = tested->Click();
        ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
        const Client::GameLogic::Path path = hero.path;
        ASSERT_GE(path.count, 2);
        const Client::GameLogic::PathCell last = path.cells[path.count - 1U];
        if (probe.map == CrywolfMap)
        {
            EXPECT_EQ(walls[cell], 0);
            EXPECT_EQ(path.count, Away + 1);
            EXPECT_EQ(last, (Client::GameLogic::PathCell{probe.altarX, probe.altarY}));
        }
        else
        {
            EXPECT_EQ(walls[cell], Client::World::BlockedWallBit | Client::World::CharacterWallBit);
            // El más cercano: la diagonal de arriba, como en TheWalkToACrywolfAltarClearsItsCell.
            EXPECT_EQ(last, (Client::GameLogic::PathCell{static_cast<std::uint8_t>(probe.altarX + 1),
                                                         static_cast<std::uint8_t>(probe.altarY - 1)}));
        }
        constexpr int StepLimit = 100;
        for (int step = 0; step < StepLimit && hero.movement != 0; ++step)
        {
            const auto stepped = Client::Scenes::StepHeroMovement(tested->Context());
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        EXPECT_EQ(hero.movement, 0);
        EXPECT_EQ(hero.cellX, last.x);
        EXPECT_EQ(hero.cellY, last.y);
        EXPECT_EQ(runtime.movement.chosenNpc, -1);
        const std::string walk = std::format("D3 {} {}", probe.altarX, probe.altarY);
        const auto& ui = runtime.crywolf.ui;
        if (probe.map == CrywolfMap)
        {
            EXPECT_EQ(tested->events, (std::vector<std::string>{walk}));
            EXPECT_EQ(std::string_view(ui.contractLines[0].data()), "quedan 2");
            EXPECT_EQ(std::string_view(ui.contractLines[1].data()), "contratar?");
            EXPECT_EQ(ui.byte723a44, 1);
            EXPECT_EQ(ui.dwords723bdc[1], 0x013e);
        }
        else
        {
            EXPECT_EQ(tested->events, (std::vector<std::string>{walk, "30 013e"}));
            EXPECT_EQ(ui.byte723a44, 0);
        }
    }
}
} // namespace
