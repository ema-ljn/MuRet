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
#include "UI/ChatInput.h"
#include "Scenes/WorldMap.h"
#include "World/WorldTerrain.h"
#include "Network/Packet.h"
#include "Network/Crypto/Xor32.h"
#include "Protocol/Inventory.h"
#include <glm/ext/vector_float2.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <vector>
#include <utility>
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

// Un nombre inventado para el personaje elegido y otro para el lugar del historial.
void NameTheActor(Client::Scenes::ActorState& actor, std::string_view name)
{
    actor.name.fill(std::byte{0});
    for (std::size_t i = 0; i < name.size(); ++i)
        actor.name[i] = static_cast<std::byte>(name[i]);
}

// Lo que pide 0x59b036..0x59b086: el chat abierto, el héroe en la zona segura, un elegido con +0x376 en 0xff y el
// derecho.
std::unique_ptr<MovingHero> AHeroRightClickingTheChosen(std::int32_t map)
{
    auto tested = std::make_unique<MovingHero>();
    auto& runtime = tested->world.Runtime();
    runtime.map = map;
    runtime.ui.chatOpen = true;
    runtime.ui.rightClick = true;
    runtime.selectedTargets[2] = 5;
    tested->world.Actors()[0].safeZone = 1;
    auto& chosen = tested->world.Actors()[5];
    chosen.alive = true;
    chosen.word376 = 0xff;
    NameTheActor(chosen, "Rival01");
    auto& input = runtime.chat.input;
    input.whisperBuffer.fill('x');
    auto& slot = input.whisperNames[static_cast<std::size_t>(input.whisperNameAt)];
    slot.fill('y');
    Client::UI::CopyIntoTheBuffer(slot, "Viejo07");
    return tested;
}

// 0x59b036..0x59b185 (Q-C804): el derecho sobre el elegido se gasta, el nombre va al buffer del susurro (vaciado entero
// antes) y al lugar del historial, con su largo y el sonido 0x19; el EDIT del susurro toma lo que había en el lugar.
TEST(GameMovementTest, TheRightClickOnTheChosenWritesItsNameAndLeavesTheOldOneInTheEdit)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroRightClickingTheChosen(map);
        const auto result = StepHeroMovement(tested->Context());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        const auto& runtime = tested->world.Runtime();
        const auto& input = runtime.chat.input;
        const auto& slot = input.whisperNames[static_cast<std::size_t>(input.whisperNameAt)];
        EXPECT_FALSE(runtime.ui.rightClick);
        EXPECT_EQ(Client::UI::CStringOf(input.whisperBuffer), "Rival01");
        EXPECT_TRUE(
            std::all_of(input.whisperBuffer.begin() + 7, input.whisperBuffer.end(), [](char c) { return c == '\0'; }));
        EXPECT_EQ(input.whisperLength, 7);
        EXPECT_EQ(Client::UI::CStringOf(slot), "Rival01");
        // strcpy: después del 0, el lugar conserva lo que tenía.
        EXPECT_EQ(slot[8], 'y');
        EXPECT_EQ(input.whisper.Value(), "Viejo07");
        EXPECT_EQ(runtime.lastSoundId, 0x19);
    }
}

// 0x59b092..0x59b0a1: en Chaos Castle el derecho se gasta y nada más; con +0x376 distinto de 0xff (un jugador del 12,
// 0xffff de 0x511290) 0x59b036 no lo gasta y lo gasta 0x57cfc0 al final (0x57d1b1), sin el nombre.
TEST(GameMovementTest, TheRightClickOnTheChosenNeedsItsWordAndAMapOutsideChaosCastle)
{
    {
        const auto tested = AHeroRightClickingTheChosen(18);
        const auto result = StepHeroMovement(tested->Context());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        const auto& input = tested->world.Runtime().chat.input;
        EXPECT_FALSE(tested->world.Runtime().ui.rightClick);
        EXPECT_EQ(Client::UI::CStringOf(input.whisperNames[static_cast<std::size_t>(input.whisperNameAt)]), "Viejo07");
        EXPECT_EQ(input.whisperBuffer[0], 'x');
        EXPECT_FALSE(tested->world.Runtime().lastSoundId.has_value());
    }
    {
        const auto tested = AHeroRightClickingTheChosen(0);
        tested->world.Actors()[5].word376 = 0xffff;
        const auto result = StepHeroMovement(tested->Context());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_FALSE(tested->world.Runtime().ui.rightClick);
        EXPECT_EQ(tested->world.Runtime().chat.input.whisperBuffer[0], 'x');
        EXPECT_FALSE(tested->world.Runtime().lastSoundId.has_value());
    }
}

// 0x59afdf..0x59b001: la ventana de la carta (+0xf0) cuenta solo si 0x490a00 la encuentra; sin ella, el nombre no va
// al susurro y el derecho lo gasta 0x57cfc0 (0x57d1b1).
TEST(GameMovementTest, ALetterIdWithoutItsWindowDoesNotTakeTheRightClick)
{
    const auto tested = AHeroRightClickingTheChosen(0);
    tested->world.Runtime().ui.chatOpen = false;
    tested->world.Runtime().oldWindows.letter = 0x77;
    const auto result = StepHeroMovement(tested->Context());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FALSE(tested->world.Runtime().ui.rightClick);
    EXPECT_EQ(tested->world.Runtime().chat.input.whisperBuffer[0], 'x');
    EXPECT_FALSE(tested->world.Runtime().lastSoundId.has_value());
}

// El clic de ataque (CL9 hito 3): el héroe de MovingHero en (182, 115) y otro jugador elegido en el lugar 5, en
// `cellX`. Las claves son inventadas.
constexpr std::size_t ChosenSlot = 5;
constexpr std::uint16_t ChosenKey = 0x0456;
constexpr int OwnDraws = 3848;
constexpr int CellDraws = 4;
// Los dos cierres del contador de la espera (0x59af50) y el registro propio que cierra el clic (0x59bd01).
constexpr int BeforeTheClick = 2 * CellDraws + OwnDraws;
// Después de mandar el camino: las dos ventanas que miran SendMove y 0x57cfc0 (el registro propio y Skill.bmd).
constexpr int AfterTheWalk = 2 + OwnDraws + 60;

std::unique_ptr<MovingHero> AHeroClickingAPlayer(std::uint8_t cellX)
{
    auto tested = std::make_unique<MovingHero>();
    auto& runtime = tested->world.Runtime();
    runtime.movement.mouseUpdateTime = 6;
    runtime.selectedTargets[2] = static_cast<std::int32_t>(ChosenSlot);
    auto& chosen = tested->world.Actors()[ChosenSlot];
    chosen.alive = true;
    chosen.type = Client::Data::Models::PlayerSkeletonModel;
    chosen.byte88 = 1;
    chosen.key = ChosenKey;
    chosen.position = {static_cast<float>(cellX) * 100.0f + 50.0f, 11550.0f, 0.0f};
    return tested;
}

CrtRandom Advanced(CrtRandom random, int draws)
{
    for (int draw = 0; draw < draws; ++draw)
        static_cast<void>(random.Next());
    return random;
}

struct Walk
{
    int count = 0;
    Client::Protocol::Movement::Cell target{};
    std::uint32_t randomState = 0;
};

// 0x59cd2e..0x59cd43 y 0x59d3de..0x59d8b6: fuera de la zona segura, un jugador elegido con Ctrl abajo (0x558e7f) se
// ataca: el blanco a 0x6c8ebc, 0x6c8e38 en 1, +0x379 en 3 y su celda a 0x77e1ba8; se ve (0x558800), el camino sale y,
// sin arma de tiro, SendMove con su celda de meta. Sin Ctrl, o en la zona segura, el mismo clic camina al terreno.
TEST(GameMovementTest, AClickOnAPlayerWithCtrlWalksToItAndRemembersIt)
{
    for (const bool control : {true, false})
    {
        for (const std::uint8_t safeZone : {std::uint8_t{0}, std::uint8_t{1}})
        {
            SCOPED_TRACE(::testing::Message() << "Ctrl " << control << ", zona segura " << int{safeZone});
            const auto tested = AHeroClickingAPlayer(185);
            tested->world.Actors()[0].safeZone = safeZone;
            auto context = tested->Context();
            context.input.leftPressed = true;
            context.input.leftDown = true;
            context.input.controlDown = control;
            context.pickTarget = []() -> Client::Core::Expected<std::optional<Client::GameLogic::PathCell>, std::string>
            { return std::optional{Client::GameLogic::PathCell{179, 116}}; };
            const CrtRandom start = tested->random;
            Walk walk;
            context.sendWalk = [&](std::span<const Client::Protocol::Movement::Cell>,
                                   Client::Protocol::Movement::Cell target,
                                   float) -> Client::Core::Expected<void, std::string>
            {
                ++walk.count;
                walk.target = target;
                walk.randomState = tested->random.State();
                return {};
            };
            const auto result = StepHeroMovement(context);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            const auto& runtime = tested->world.Runtime();
            const auto& hero = tested->world.Actors()[0];
            EXPECT_EQ(walk.count, 1);
            EXPECT_EQ(runtime.movement.mouseUpdateTime, 1);
            EXPECT_EQ(hero.movement, 1);
            if (!control || safeZone != 0)
            {
                EXPECT_EQ(walk.target.x, 179);
                EXPECT_EQ(walk.target.y, 116);
                EXPECT_EQ(hero.movementType, 0);
                EXPECT_EQ(runtime.movement.attackTarget, -1);
                EXPECT_EQ(runtime.selectedTargets[4], -1);
                continue;
            }
            EXPECT_EQ(walk.target.x, 185);
            EXPECT_EQ(walk.target.y, 115);
            EXPECT_EQ(hero.movementType, 3);
            EXPECT_EQ(runtime.movement.attackTarget, static_cast<std::int32_t>(ChosenSlot));
            EXPECT_EQ(runtime.selectedTargets[4], 1);
            EXPECT_EQ(runtime.movement.targetX, 185u);
            EXPECT_EQ(runtime.movement.targetY, 115u);
            ASSERT_EQ(hero.path.count, 4u);
            EXPECT_EQ(hero.path.cells[3].x, 185);
            // La Y y la X para ver, otra vez para el camino, y las manos.
            const CrtRandom atTheWalk = Advanced(start, BeforeTheClick + 4 * CellDraws + OwnDraws);
            EXPECT_EQ(walk.randomState, atTheWalk.State());
            EXPECT_EQ(tested->random.State(), Advanced(atTheWalk, AfterTheWalk).State());
        }
    }
}

// 0x558800: una celda con el WORD en 4 o más y sin 0x20, 0x40 ni 0x80 entre el héroe y el elegido tapa: el blanco ya
// quedó anotado, pero no hay camino y el clic termina como siempre (0x59e248). Con 0x20 se ve.
TEST(GameMovementTest, AWallBetweenStopsTheAttackAfterRememberingTheTarget)
{
    for (const std::uint16_t wall : {std::uint16_t{4}, std::uint16_t{0x24}})
    {
        SCOPED_TRACE(wall);
        const auto tested = AHeroClickingAPlayer(185);
        tested->world.Map().terrain.attributes->cells[115 * 256 + 184] = wall;
        auto context = tested->Context();
        context.input.leftPressed = true;
        context.input.leftDown = true;
        context.input.controlDown = true;
        int walks = 0;
        context.sendWalk = [&](std::span<const Client::Protocol::Movement::Cell>, Client::Protocol::Movement::Cell,
                               float) -> Client::Core::Expected<void, std::string>
        {
            ++walks;
            return {};
        };
        const CrtRandom start = tested->random;
        const auto result = StepHeroMovement(context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(tested->world.Runtime().movement.attackTarget, static_cast<std::int32_t>(ChosenSlot));
        EXPECT_EQ(tested->world.Actors()[0].movementType, 3);
        EXPECT_EQ(tested->world.Runtime().movement.mouseUpdateTime, 1);
        if (wall == 4)
        {
            EXPECT_EQ(walks, 0);
            EXPECT_EQ(tested->random.State(), Advanced(start, BeforeTheClick + 2 * CellDraws + OwnDraws + 60).State());
        }
        else
        {
            EXPECT_EQ(walks, 1);
        }
    }
}

// 0x59d856..0x59d8bb y 0x561c50: con el arco en la izquierda el camino no se manda; sin flechas en la derecha,
// 0x561840 las busca (el texto 251, tipo 2, sin ninguna) y MoveHero sale (0x59e2c0) sin sumar el contador ni
// pasar por 0x57cfc0.
TEST(GameMovementTest, ABowWithoutArrowsLeavesMoveHero)
{
    const auto tested = AHeroClickingAPlayer(185);
    tested->world.OwnCharacter().equipment[0].id = 0xffff;
    tested->world.OwnCharacter().equipment[1].id = 0x800;
    std::vector<std::size_t> texts;
    const Client::Scenes::InventoryServices inventory{
        .systemMessage = [&](std::size_t index, std::int32_t type) -> Client::Core::Expected<void, std::string>
        {
            texts.push_back(index);
            EXPECT_EQ(type, 2);
            return {};
        }};
    auto context = tested->Context();
    context.inventory = &inventory;
    context.input.leftPressed = true;
    context.input.leftDown = true;
    context.input.controlDown = true;
    int walks = 0;
    context.sendWalk = [&](std::span<const Client::Protocol::Movement::Cell>, Client::Protocol::Movement::Cell,
                           float) -> Client::Core::Expected<void, std::string>
    {
        ++walks;
        return {};
    };
    const CrtRandom start = tested->random;
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(walks, 0);
    EXPECT_EQ(texts, std::vector<std::size_t>{251});
    EXPECT_EQ(tested->world.Runtime().movement.mouseUpdateTime, 0);
    EXPECT_EQ(tested->world.Actors()[0].path.count, 4u);
    // Las celdas, las manos y los dos accesos de 0x561c50.
    EXPECT_EQ(tested->random.State(), Advanced(start, BeforeTheClick + 4 * CellDraws + 3 * OwnDraws).State());
}

// 0x59cd49..0x59d3d7 y 0x59d65f..0x59d678: el elfo que camina con el arco manda antes su celda lógica (C1 05 DF X Y,
// la que ya va adelante); con flechas, 0x5650c0(héroe, héroe, 1) pega desde ahí si está en alcance (seis celdas con
// el arco): el D3 de girar y el D7 con la clave del elegido.
TEST(GameMovementTest, AWalkingElfWithABowSendsItsCellAndShoots)
{
    const auto tested = AHeroClickingAPlayer(188);
    auto& hero = tested->world.Actors()[0];
    hero.characterClass = 2;
    hero.movement = 1;
    hero.path.count = 4;
    for (std::uint8_t cell = 0; cell < 4; ++cell)
        hero.path.cells[cell] = {static_cast<std::uint8_t>(182 + cell), 115};
    auto& own = tested->world.OwnCharacter();
    own.equipment[0].id = 0x80f;
    own.equipment[0].durability = 20;
    own.equipment[1].id = 0x800;
    auto context = tested->Context();
    context.input.leftPressed = true;
    context.input.leftDown = true;
    context.input.controlDown = true;
    std::vector<std::pair<std::uint8_t, std::uint8_t>> cells;
    context.sendElfCell = [&](std::uint8_t x, std::uint8_t y) -> Client::Core::Expected<void, std::string>
    {
        cells.emplace_back(x, y);
        return {};
    };
    std::vector<std::vector<std::byte>> sent;
    context.sendWalk = [&](std::span<const Client::Protocol::Movement::Cell> path,
                           Client::Protocol::Movement::Cell target,
                           float angle) -> Client::Core::Expected<void, std::string>
    {
        const auto packet = Client::Protocol::Movement::WriteWalk(path, target, angle);
        EXPECT_TRUE(packet.HasValue());
        const auto decoded = Client::Network::Crypto::Xor32Decode(packet.Value().packet);
        sent.emplace_back(decoded.Bytes().begin(), decoded.Bytes().end());
        return {};
    };
    std::vector<std::uint16_t> keys;
    context.sendAttack = [&](std::uint16_t key, std::uint8_t action,
                             std::uint8_t) -> Client::Core::Expected<void, std::string>
    {
        keys.push_back(key);
        EXPECT_EQ(action, 0x78);
        return {};
    };
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    const std::pair<std::uint8_t, std::uint8_t> elfCell{std::uint8_t{183}, std::uint8_t{115}};
    EXPECT_EQ(cells, std::vector{elfCell});
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].size(), 6u);
    EXPECT_EQ(sent[0][3], std::byte{183});
    EXPECT_EQ(keys, std::vector<std::uint16_t>{ChosenKey});
    EXPECT_EQ(hero.byte381, 1);
    EXPECT_EQ(hero.movement, 0);
    // 0x5653c1 pone el contador en su tope antes de la suma de 0x59e248.
    const auto& movement = tested->world.Runtime().movement;
    EXPECT_EQ(movement.mouseUpdateTime, movement.mouseUpdateTimeMax + 1);
}

// 0x59b89e..0x59b8cd: con el ataque automático, el otro elegido (0x6c8e38) en 1 y un elegido, el clic vuelve solo,
// sin botón; fuera del mapa 6 y de Chaos Castle. 0x59b22f: lo mismo con 0x6c8e38 distinto de −1 pone la cabeza en 0 y
// el héroe quieto no gira (0x59b3c4).
TEST(GameMovementTest, TheAutomaticAttackClicksAgainAndHoldsTheHead)
{
    for (const std::int32_t map : {0, 6, 18})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroClickingAPlayer(185);
        auto& runtime = tested->world.Runtime();
        runtime.map = map;
        runtime.selectedTargets[4] = 1;
        tested->world.Actors()[ChosenSlot].byte88 = 2;
        auto context = tested->Context();
        context.input.mouseX = 640;
        context.input.mouseY = 180;
        int walks = 0;
        context.sendWalk = [&](std::span<const Client::Protocol::Movement::Cell>, Client::Protocol::Movement::Cell,
                               float) -> Client::Core::Expected<void, std::string>
        {
            ++walks;
            return {};
        };
        int facings = 0;
        context.sendFacing = [&](std::uint8_t) -> Client::Core::Expected<void, std::string>
        {
            ++facings;
            return {};
        };
        const auto result = StepHeroMovement(context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        const auto& hero = tested->world.Actors()[0];
        if (map == 0)
        {
            EXPECT_EQ(walks, 1);
            EXPECT_EQ(hero.movementType, 3);
            EXPECT_EQ(hero.headTurn.target, (glm::vec2{0.0f, 0.0f}));
            EXPECT_EQ(facings, 0);
        }
        else
        {
            EXPECT_EQ(walks, 0);
            EXPECT_EQ(hero.movementType, 0);
            EXPECT_FLOAT_EQ(hero.headTurn.target.x, 300.0f);
        }
    }
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
