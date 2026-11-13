#include "Core/Expected.h"
#include "GameLogic/PathCell.h"
#include "Scenes/GameMovement.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ActorState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Data/Tables/SkillTable.h"
#include "Protocol/Movement.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/WorldMap.h"
#include "World/WorldTerrain.h"
#include "Network/Packet.h"
#include "Network/Crypto/Xor32.h"
#include "Protocol/Inventory.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <span>
#include <vector>
#include <cstddef>
#include <gtest/gtest.h>

namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::HeroMovementContext;
using Client::Scenes::StepHeroMovement;
using Client::Scenes::WorldState;

class MovingHero
{
public:
    MovingHero()
    {
        world.Runtime().map = 0;
        auto& terrain = world.Map().terrain;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        terrain.mapping = std::make_unique<Client::Data::Terrain::TerrainMapping>();
        auto& hero = world.Actors()[0];
        hero.alive = true;
        hero.type = Client::Data::Models::PlayerSkeletonModel;
        hero.cellX = hero.targetX = 182;
        hero.cellY = hero.targetY = 115;
        hero.position = {18250.0f, 11550.0f, 0.0f};
        hero.hands[0].model = hero.hands[1].model = hero.wings.model = hero.pet.model = Client::Scenes::NoItemModel;
        hero.alpha = 1.0f;
        hero.byte88 = 1;
        hero.baseMovementSpeed = 10;
        skeleton.actions.resize(205);
        for (auto& action : skeleton.actions)
            action.keyCount = 10;
    }
    HeroMovementContext Context()
    {
        return {.world = world, .random = random, .playerSkeleton = &skeleton, .skills = &skills};
    }
    WorldState world;
    CrtRandom random;
    Client::Render::Models::Skeleton skeleton;
    Client::Data::Tables::SkillTable skills{std::vector<std::byte>(Client::Data::Tables::SkillTableSize)};
};

TEST(GameMovementTest, AStunnedHeroDoesNotTouchTheWaitCounter)
{
    WorldState world;
    CrtRandom random;
    world.Runtime().warpCounter = 50;
    world.Actors()[0].flags78 = 0x1000;
    const auto before = random.State();
    const auto result = StepHeroMovement(HeroMovementContext{.world = world, .random = random});
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(world.Runtime().warpCounter, 50);
    EXPECT_EQ(random.State(), before);
}

TEST(GameMovementTest, ADyingHeroDoesNotTouchTheWaitCounter)
{
    WorldState world;
    CrtRandom random;
    world.Runtime().warpCounter = 50;
    world.Actors()[0].dying = 1;
    const auto before = random.State();
    const auto result = StepHeroMovement(HeroMovementContext{.world = world, .random = random});
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(world.Runtime().warpCounter, 50);
    EXPECT_EQ(random.State(), before);
}

TEST(GameMovementTest, TheWaitCounterUsesTwoIndependentCloses)
{
    WorldState world;
    CrtRandom random;
    world.Runtime().warpCounter = 50;
    CrtRandom expected = random;
    for (int i = 0; i < 8; ++i)
        static_cast<void>(expected.Next());
    const auto result = StepHeroMovement(HeroMovementContext{.world = world, .random = random});
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(world.Runtime().warpCounter, 49);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().warpDepth, 0);
}

TEST(GameMovementTest, AnOrdinaryIdleStepClosesTheOwnRecord)
{
    MovingHero tested;
    CrtRandom expected = tested.random;
    // 57d114 llama5526f0 incluso con skill0: cierra60 bytes tras el registro propio.
    for (int i = 0; i < 8 + 3848 + 60; ++i)
        static_cast<void>(expected.Next());
    const auto result = StepHeroMovement(tested.Context());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.world.Runtime().movement.mouseUpdateTime, 1);
}

TEST(GameMovementTest, AClickBeforeTheWindowExpiresStaysPending)
{
    MovingHero tested;
    auto context = tested.Context();
    context.input.leftPressed = true;
    context.input.leftDown = true;
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(tested.world.Runtime().movement.pendingClick);
    EXPECT_EQ(tested.world.Runtime().movement.mouseUpdateTime, 1);
}

TEST(GameMovementTest, AShiftClickIsConsumedWithoutPicking)
{
    MovingHero tested;
    tested.world.Runtime().movement.mouseUpdateTime = 6;
    auto context = tested.Context();
    context.input.leftPressed = true;
    context.input.leftDown = true;
    context.input.shiftDown = true;
    int picks = 0;
    context.pickTarget = [&]() -> Client::Core::Expected<std::optional<Client::GameLogic::PathCell>, std::string>
    {
        ++picks;
        return std::optional{Client::GameLogic::PathCell{179, 116}};
    };
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(picks, 0);
    EXPECT_FALSE(tested.world.Runtime().movement.pendingClick);
    EXPECT_EQ(tested.world.Runtime().movement.mouseUpdateTime, 1);
}

TEST(GameMovementTest, TheMouseOnTheUiDoesNotAdvanceTheClickWindow)
{
    MovingHero tested;
    tested.world.Runtime().movement.mouseUpdateTime = 6;
    // 0x775857c: la UI del paso la prende con el mouse en la barra (y ≥ 432) o sobre un panel.
    tested.world.Runtime().ui.mouseOnUi = 1;
    auto context = tested.Context();
    context.input.mouseY = 432;
    context.input.leftPressed = true;
    context.input.leftDown = true;
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.world.Runtime().movement.mouseUpdateTime, 6);
    EXPECT_TRUE(tested.world.Runtime().movement.pendingClick);
}

TEST(GameMovementTest, AClickFindsAndSendsTheHistoricalD3)
{
    MovingHero tested;
    // El cable no captura ocupantes. Esta celda fuerza el camino documentado7,6,7;
    // en una grilla vacía el A* nativo puede elegir7,7,6 con la misma meta.
    tested.world.Map().terrain.attributes->cells[115 * 256 + 180] = 2;
    tested.world.Runtime().movement.mouseUpdateTime = 6;
    auto context = tested.Context();
    context.input.leftPressed = true;
    context.input.leftDown = true;
    context.pickTarget = []() -> Client::Core::Expected<std::optional<Client::GameLogic::PathCell>, std::string>
    { return std::optional{Client::GameLogic::PathCell{179, 116}}; };
    std::vector<std::byte> bytes;
    context.sendWalk = [&](std::span<const Client::Protocol::Movement::Cell> path,
                           Client::Protocol::Movement::Cell target,
                           float angle) -> Client::Core::Expected<void, std::string>
    {
        const auto packet = Client::Protocol::Movement::WriteWalk(path, target, angle);
        if (!packet.HasValue())
            return Client::Core::Unexpected(std::string("invalid movement"));
        const auto decoded = Client::Network::Crypto::Xor32Decode(packet.Value().packet);
        bytes.assign(decoded.Bytes().begin(), decoded.Bytes().end());
        return {};
    };
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    const std::vector<std::byte> expected{std::byte{0xc1}, std::byte{8},    std::byte{0xd3}, std::byte{0xb6},
                                          std::byte{0x73}, std::byte{0x73}, std::byte{0x76}, std::byte{0x70}};
    EXPECT_EQ(bytes, expected);
    EXPECT_EQ(tested.world.Actors()[0].movement, 1);
    EXPECT_EQ(tested.world.Actors()[0].movementType, 0);
    EXPECT_EQ(tested.world.Runtime().movement.mouseUpdateTimeMax, 16);
}
// CL9 hito 11, 0x56b4db..0x56b78f: con la ventana de los eventos (0x78abb00) abierta, después del D3 va el C1 03 97,
// 0x78abb00 vuelve a 0, el cursor queda pedido en SetCursorPos(x · ancho / 260, y · alto / 480) (Q-C1330), 0x6c8dcc
// a 6 y el izquierdo suelto; con el 3 (el número de serie), corta en 0x56b6a9 después del envío. En Lorencia y en
// Devias, cada caso con un héroe quieto, porque el anterior lo deja caminando.
TEST(GameMovementTest, AWalkWithTheEventWindowOpenLeavesItAndAsksForTheCursor)
{
    struct Walk
    {
        std::unique_ptr<MovingHero> tested;
        std::vector<std::string> sent;
    };
    const auto walk = [](std::int32_t map, std::int32_t window, Walk& out)
    {
        out.tested = std::make_unique<MovingHero>();
        out.tested->world.Runtime().map = map;
        out.tested->world.Runtime().movement.mouseUpdateTime = 6;
        out.tested->world.Runtime().panels.field78abb00 = window;
        auto context = out.tested->Context();
        context.input.leftPressed = true;
        context.input.leftDown = true;
        context.input.mouseX = 130;
        context.input.mouseY = 240;
        context.screenWidth = 800;
        context.screenHeight = 600;
        context.pickTarget = []() -> Client::Core::Expected<std::optional<Client::GameLogic::PathCell>, std::string>
        { return std::optional{Client::GameLogic::PathCell{179, 116}}; };
        context.sendWalk = [&out](auto, auto, float) -> Client::Core::Expected<void, std::string>
        {
            out.sent.emplace_back("D3");
            return {};
        };
        context.sendEventWindowLeave = [&out]() -> Client::Core::Expected<void, std::string>
        {
            out.sent.emplace_back("97");
            return {};
        };
        return StepHeroMovement(context);
    };
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        for (const std::int32_t window : {1, 2})
        {
            SCOPED_TRACE(window);
            Walk left;
            const auto result = walk(map, window, left);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            const Client::Scenes::GameMovementState& movement = left.tested->world.Runtime().movement;
            EXPECT_EQ(left.sent, (std::vector<std::string>{"D3", "97"}));
            ASSERT_TRUE(movement.cursorWarpRequest.has_value());
            EXPECT_EQ(*movement.cursorWarpRequest, (std::array<std::int32_t, 2>{400, 300}));
            EXPECT_EQ(left.tested->world.Runtime().panels.field78abb00, 0);
            EXPECT_EQ(movement.mouseUpdateTimeMax, 6);
            EXPECT_FALSE(movement.leftHeld);
        }

        Walk serial;
        const auto serialCut = walk(map, 3, serial);
        ASSERT_FALSE(serialCut.HasValue());
        EXPECT_NE(serialCut.Error().find("0x56b6a9"), std::string::npos) << serialCut.Error();
        EXPECT_EQ(serial.sent, (std::vector<std::string>{"D3", "97"}));
        EXPECT_FALSE(serial.tested->world.Runtime().movement.cursorWarpRequest.has_value());
    }
}

TEST(GameMovementTest, AWalkingHeroMovesByTwelveAndKeepsTheLogicalCellAhead)
{
    MovingHero tested;
    auto& hero = tested.world.Actors()[0];
    hero.movement = 1;
    hero.path.count = 2;
    hero.path.cells[0] = {182, 115};
    hero.path.cells[1] = {183, 115};
    const auto result = StepHeroMovement(tested.Context());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FLOAT_EQ(hero.position.x, 18262.0f);
    EXPECT_FLOAT_EQ(hero.position.y, 11550.0f);
    EXPECT_EQ(hero.cellX, 183);
    EXPECT_EQ(hero.animation.action, 13);
}

TEST(GameMovementTest, AClickDuringTheWarpWaitRemainsPending)
{
    MovingHero tested;
    tested.world.Runtime().warpCounter = 50;
    auto context = tested.Context();
    context.input.leftPressed = true;
    context.input.leftDown = true;
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(tested.world.Runtime().movement.pendingClick);
}
TEST(GameMovementTest, TheHeroLooksAtTheMouseBeforeMoving)
{
    MovingHero tested;
    auto context = tested.Context();
    context.input.mouseX = 640;
    context.input.mouseY = 180;
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FLOAT_EQ(tested.world.Actors()[0].headTurn.target.x, 300.0f);
    EXPECT_FLOAT_EQ(tested.world.Actors()[0].headTurn.target.y, 0.0f);
}

TEST(GameMovementTest, ACameraTourStopsBeforeTouchingTheWaitCounter)
{
    MovingHero tested;
    tested.world.Runtime().warpCounter = 50;
    tested.world.Runtime().movement.cameraTour = true;
    tested.world.Actors()[0].run = 20;
    const auto result = StepHeroMovement(tested.Context());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.world.Runtime().warpCounter, 50);
    EXPECT_EQ(tested.world.Actors()[0].run, 0);
}

TEST(GameMovementTest, ASittingHeroWithACharacterUnderTheMouseSkipsTheWaitCounter)
{
    MovingHero tested;
    tested.world.Runtime().warpCounter = 50;
    tested.world.Runtime().selectedTargets[2] = 7;
    tested.world.Actors()[0].flags7c = 0x200000;
    const auto result = StepHeroMovement(tested.Context());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.world.Runtime().warpCounter, 50);
}

TEST(GameMovementTest, TheOwnRecordAndSkillDepthsAreIndependent)
{
    for (const auto depths : {std::array<int, 2>{1, 0}, {0, 1}, {1, 1}})
    {
        MovingHero tested;
        tested.world.Runtime().ownRecordDepth = static_cast<std::uint8_t>(depths[0]);
        tested.world.Runtime().movement.skillDepths[0] = static_cast<std::uint8_t>(depths[1]);
        CrtRandom expected = tested.random;
        const int draws = 8 + (depths[0] ? 0 : 3848) + (depths[1] ? 0 : 60);
        for (int i = 0; i < draws; ++i)
            static_cast<void>(expected.Next());
        const auto result = StepHeroMovement(tested.Context());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested.random.State(), expected.State());
    }
}

TEST(GameMovementTest, TheSkillSelectorReadsLittleEndianBytesInsteadOfWords)
{
    MovingHero tested;
    tested.world.OwnCharacter().words6c[0] = 0x04030201;
    tested.world.Actors()[0].skillIndex = 1;
    tested.world.Runtime().movement.skillDepths[2] = 1;
    CrtRandom expected = tested.random;
    for (int i = 0; i < 8 + 3848; ++i)
        static_cast<void>(expected.Next());
    const auto result = StepHeroMovement(tested.Context());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.random.State(), expected.State());
}
TEST(GameMovementTest, HeadYawTruncatesBeforeAddingTheIntegerOffset)
{
    MovingHero tested;
    tested.world.Actors()[0].angles.z = 90.75f;
    auto context = tested.Context();
    context.input.mouseX = 640;
    context.input.mouseY = 180;
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FLOAT_EQ(tested.world.Actors()[0].headTurn.target.x, 315.0f);
}

TEST(GameMovementTest, ActionThreeEOverridesTheHeadTargetWithZero)
{
    MovingHero tested;
    tested.world.Actors()[0].animation.action = 0x3e;
    tested.world.Actors()[0].headTurn.target = {20, 30};
    const auto result = StepHeroMovement(tested.Context());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FLOAT_EQ(tested.world.Actors()[0].headTurn.target.x, 0.0f);
    EXPECT_FLOAT_EQ(tested.world.Actors()[0].headTurn.target.y, 0.0f);
}
TEST(GameMovementTest, TheShockActionConsumesTheClickWithoutStartingAnotherPath)
{
    MovingHero tested;
    tested.world.Actors()[0].animation.action = 0xc2;
    tested.world.Runtime().movement.mouseUpdateTime = 6;
    auto context = tested.Context();
    context.input.leftPressed = true;
    context.input.leftDown = true;
    int picks = 0;
    context.pickTarget = [&]() -> Client::Core::Expected<std::optional<Client::GameLogic::PathCell>, std::string>
    {
        ++picks;
        return std::optional<Client::GameLogic::PathCell>{};
    };
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(picks, 0);
    EXPECT_EQ(tested.world.Runtime().movement.mouseUpdateTime, 7);
    EXPECT_FALSE(tested.world.Runtime().movement.pendingClick);
}

TEST(GameMovementTest, TheActionPrefixFadesBeforeItsOwnAndSkillReads)
{
    MovingHero tested;
    auto& hero = tested.world.Actors()[0];
    hero.byte80 = 1;
    hero.alpha = 0.15f;
    auto context = tested.Context();
    context.skills = nullptr;
    const auto result = StepHeroMovement(context);
    EXPECT_FALSE(result.HasValue());
    EXPECT_FLOAT_EQ(hero.alpha, static_cast<float>(static_cast<double>(0.15f) - static_cast<double>(0.1f)));
    EXPECT_EQ(hero.byte80, 2);
}

TEST(GameMovementTest, TheBlockedActionPrefixStillReadsSkillButRetainsItsInputClock)
{
    MovingHero tested;
    tested.world.Runtime().movement.movementBlocked = true;
    tested.world.Runtime().movement.actionInputTime = 42;
    auto context = tested.Context();
    context.input.worldTime = 300;
    CrtRandom expected = tested.random;
    for (int i = 0; i < 8 + 3848 + 60; ++i)
        static_cast<void>(expected.Next());
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_FLOAT_EQ(tested.world.Runtime().movement.actionInputTime, 42.0f);
}

TEST(GameMovementTest, AShiftClickClosesItsOwnRecordBeforeTheActionPrefix)
{
    for (const std::uint8_t depth : {std::uint8_t{0}, std::uint8_t{1}})
    {
        MovingHero tested;
        tested.world.Runtime().ownRecordDepth = depth;
        tested.world.Runtime().movement.mouseUpdateTime = 6;
        auto context = tested.Context();
        context.input.leftPressed = true;
        context.input.leftDown = true;
        context.input.shiftDown = true;
        CrtRandom expected = tested.random;
        for (int i = 0; i < 8 + (depth == 0 ? 2 * 3848 : 0) + 60; ++i)
            static_cast<void>(expected.Next());
        const auto result = StepHeroMovement(context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested.random.State(), expected.State());
        EXPECT_EQ(tested.world.Runtime().ownRecordDepth, depth);
    }
}

TEST(GameMovementTest, AClickClosesBeforePickingAndReadsTwoWindowsAfterSending)
{
    MovingHero tested;
    tested.world.Runtime().movement.mouseUpdateTime = 6;
    auto context = tested.Context();
    context.input.leftPressed = true;
    context.input.leftDown = true;
    CrtRandom expected = tested.random;
    for (int i = 0; i < 8 + 3848; ++i)
        static_cast<void>(expected.Next());
    context.pickTarget = [&]() -> Client::Core::Expected<std::optional<Client::GameLogic::PathCell>, std::string>
    {
        EXPECT_EQ(tested.random.State(), expected.State());
        return std::optional{Client::GameLogic::PathCell{183, 115}};
    };
    context.sendWalk = [&](auto, auto, float) -> Client::Core::Expected<void, std::string>
    {
        for (int i = 0; i < 12; ++i)
            static_cast<void>(expected.Next());
        EXPECT_EQ(tested.random.State(), expected.State());
        return {};
    };
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    for (int i = 0; i < 2 + 3848 + 60; ++i)
        static_cast<void>(expected.Next());
    EXPECT_EQ(tested.random.State(), expected.State());
}

// Lo que pasa con un clic según las manos del registro propio: walked dice si salió el D3.
struct HandsClick
{
    Client::Core::Expected<void, std::string> result;
    bool walked = false;
};

HandsClick ClickWithTheHands(std::uint16_t right, std::uint16_t left, std::int32_t cursorItemCount = 0,
                             const Client::Scenes::InventoryServices* inventory = nullptr)
{
    MovingHero tested;
    tested.world.OwnCharacter().equipment[0].id = right;
    tested.world.OwnCharacter().equipment[1].id = left;
    tested.world.Runtime().cursorItemCount = cursorItemCount;
    tested.world.Runtime().movement.mouseUpdateTime = 6;
    auto context = tested.Context();
    context.inventory = inventory;
    context.input.leftPressed = true;
    context.input.leftDown = true;
    context.pickTarget = []() -> Client::Core::Expected<std::optional<Client::GameLogic::PathCell>, std::string>
    { return std::optional{Client::GameLogic::PathCell{183, 115}}; };
    HandsClick click;
    context.sendWalk = [&](auto, auto, float) -> Client::Core::Expected<void, std::string>
    {
        click.walked = true;
        return {};
    };
    click.result = StepHeroMovement(context);
    return click;
}

constexpr std::uint16_t EmptyHand = 0xffff;

// 0x59bbe9..0x59bd38: la derecha es +0xb6c y la izquierda +0xbb4. Con la espada en la derecha y la
// izquierda vacía, el caso más común, el clic camina.
TEST(GameMovementTest, AWeaponInTheRightHandWithTheLeftEmptyWalks)
{
    for (const std::uint16_t right : {std::uint16_t{0x0000}, std::uint16_t{0x0205}, std::uint16_t{0x0a00}})
    {
        const auto click = ClickWithTheHands(right, EmptyHand);
        ASSERT_TRUE(click.result.HasValue()) << click.result.Error();
        EXPECT_TRUE(click.walked) << right;
    }
}

// 0x59bd3e..0x59bf56: con la derecha vacía y un arma de los grupos 0 a 2 o del 5 en la izquierda, el original la pasa
// al cursor, viste al héroe y manda el C1 0E 24 del lugar 1 al 0 (Scenes::MoveTheLeftWeaponToTheRight); después sigue
// con el clic (0x59cd23). Sin quien vista al héroe corta en 0x59bf23, con el registro ya cerrado.
TEST(GameMovementTest, AWeaponOnlyInTheLeftHandGoesToTheRight)
{
    for (const std::uint16_t left :
         {std::uint16_t{0x0000}, std::uint16_t{0x05ff}, std::uint16_t{0x0a00}, std::uint16_t{0x0bff}})
    {
        SCOPED_TRACE(left);
        const auto cut = ClickWithTheHands(EmptyHand, left);
        ASSERT_FALSE(cut.result.HasValue());
        EXPECT_TRUE(cut.result.Error().starts_with("0x59bf23:")) << cut.result.Error();
        EXPECT_FALSE(cut.walked);

        int dressed = 0;
        std::vector<Client::Protocol::Inventory::ItemMove> sent;
        const Client::Scenes::InventoryServices inventory{.items = nullptr,
                                                          .dressTheHero =
                                                              [&dressed]
                                                          {
                                                              ++dressed;
                                                              return Client::Core::Expected<void, std::string>{};
                                                          },
                                                          .replaceTheRaven = {},
                                                          .systemMessage = {},
                                                          .sendItemMove =
                                                              [&sent](const Client::Protocol::Inventory::ItemMove& move)
                                                          {
                                                              sent.push_back(move);
                                                              return Client::Core::Expected<void, std::string>{};
                                                          }};
        const auto click = ClickWithTheHands(EmptyHand, left, 0, &inventory);
        ASSERT_TRUE(click.result.HasValue()) << click.result.Error();
        EXPECT_EQ(dressed, 1);
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_EQ(sent.front().sourceSlot, 1);
        EXPECT_EQ(sent.front().targetSlot, 0);
        EXPECT_EQ(sent.front().item[0], static_cast<std::byte>(left & 0xff));
    }
}

// Fuera de esos grupos (las lanzas, los arcos, los escudos), con la derecha ocupada o con un ítem en el
// cursor ([0x7879fbc] > 0), el original sigue a 0x59cd23.
TEST(GameMovementTest, TheOtherHandsAndAnItemOnTheCursorWalk)
{
    for (const std::uint16_t left :
         {std::uint16_t{0x0600}, std::uint16_t{0x0800}, std::uint16_t{0x0c00}, std::uint16_t{0x0c05}, EmptyHand})
    {
        const auto click = ClickWithTheHands(EmptyHand, left);
        ASSERT_TRUE(click.result.HasValue()) << click.result.Error();
        EXPECT_TRUE(click.walked) << left;
    }
    const auto twoWeapons = ClickWithTheHands(0x0000, 0x0001);
    ASSERT_TRUE(twoWeapons.result.HasValue()) << twoWeapons.result.Error();
    EXPECT_TRUE(twoWeapons.walked);
    const auto onTheCursor = ClickWithTheHands(EmptyHand, 0x0000, 1);
    ASSERT_TRUE(onTheCursor.result.HasValue()) << onTheCursor.result.Error();
    EXPECT_TRUE(onTheCursor.walked);
}

TEST(GameMovementTest, AContinuouslyHeldClickExpiresAtOneHourUntilReleased)
{
    MovingHero tested;
    auto& state = tested.world.Runtime().movement;
    state.mouseUpdateTime = 6;
    auto context = tested.Context();
    context.input.leftDown = true;
    context.input.worldTime = 3'600'000.0f;
    int picks = 0;
    context.pickTarget = [&]() -> Client::Core::Expected<std::optional<Client::GameLogic::PathCell>, std::string>
    {
        ++picks;
        return std::optional<Client::GameLogic::PathCell>{};
    };
    ASSERT_TRUE(StepHeroMovement(context).HasValue());
    EXPECT_EQ(picks, 0);
    EXPECT_FALSE(state.leftHeld);
    EXPECT_FALSE(state.pendingClick);
    ASSERT_TRUE(StepHeroMovement(context).HasValue());
    EXPECT_EQ(picks, 0);
    context.input.leftDown = false;
    ASSERT_TRUE(StepHeroMovement(context).HasValue());
    EXPECT_FLOAT_EQ(state.heldSince, 3'600'000.0f);
    context.input.leftDown = true;
    context.input.leftPressed = true;
    ASSERT_TRUE(StepHeroMovement(context).HasValue());
    EXPECT_EQ(picks, 1);
}

TEST(GameMovementTest, IdleInputRefreshesTheHoldClockOnlyWhenTheCadenceAllowsIt)
{
    MovingHero tested;
    auto context = tested.Context();
    context.input.worldTime = 100;
    auto& state = tested.world.Runtime().movement;
    state.heldSince = 42;
    ASSERT_TRUE(StepHeroMovement(context).HasValue());
    EXPECT_FLOAT_EQ(state.heldSince, 42);
    state.mouseUpdateTime = 6;
    ASSERT_TRUE(StepHeroMovement(context).HasValue());
    EXPECT_FLOAT_EQ(state.heldSince, 100);
    state.movementBlocked = true;
    context.input.worldTime = 200;
    ASSERT_TRUE(StepHeroMovement(context).HasValue());
    EXPECT_FLOAT_EQ(state.heldSince, 100);
}

TEST(GameMovementTest, TheMovingHeroUsesTheFixedHeightPublishedByTheCamera)
{
    MovingHero tested;
    auto& hero = tested.world.Actors()[0];
    hero.movement = 1;
    hero.path.count = 2;
    hero.path.cells[0] = {182, 115};
    hero.path.cells[1] = {183, 115};
    tested.world.Map().terrain.attributes->cells.fill(0x40);
    auto context = tested.Context();
    context.fixedHeight = 1201;
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FLOAT_EQ(hero.position.z, 1201);
}

TEST(GameMovementTest, TheIdleFacingPacketPrecedesGatesAndTheActionRecord)
{
    MovingHero tested;
    tested.world.Runtime().movement.movementTarget = 39;
    auto context = tested.Context();
    context.input.mouseX = 420;
    context.input.mouseY = 180;
    CrtRandom expected = tested.random;
    for (int i = 0; i < 8; ++i)
        static_cast<void>(expected.Next());
    int packets = 0;
    context.sendFacing = [&](std::uint8_t direction) -> Client::Core::Expected<void, std::string>
    {
        ++packets;
        EXPECT_EQ(direction, 4);
        EXPECT_FLOAT_EQ(tested.world.Actors()[0].angles.z, 135);
        EXPECT_EQ(tested.random.State(), expected.State());
        return {};
    };
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(packets, 1);
    EXPECT_EQ(tested.world.Runtime().movement.movementTarget, 0);
    for (int i = 0; i < 3848 + 60; ++i)
        static_cast<void>(expected.Next());
    EXPECT_EQ(tested.random.State(), expected.State());
}
} // namespace
