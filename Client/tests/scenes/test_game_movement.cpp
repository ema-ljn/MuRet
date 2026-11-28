#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Pathfinding/PathCell.h"
#include "Network/Xor32Packet.h"
#include "Models/IModelLibrary.h"
#include "Movement/GameMovement.h"
#include "Scenes/InventoryMoves.h"
#include "Kernel/ActorState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/TextureSlot.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Tables/SkillTable.h"
#include "Data/Models/Model.h"
#include "Protocol/Movement/Movement.h"
#include "Movement/GameMovementState.h"
#include "UI/ChatInput.h"
#include "UI/OldControls.h"
#include "UI/OldWindows.h"
#include "Terrain/WorldTerrain.h"
#include "Network/Packet.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Items/Inventory.h"
#include "Scenes/EventGlobals.h"
#include "Scenes/NpcWindowState.h"
#include "UI/PanelRegistry.h"
#include "Audio/SoundId.h"
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
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::Scenes::HeroMovementContext;
using Client::Scenes::StepHeroMovement;

constexpr std::uint16_t TransformedModel = 0x154;

class MovementModels final : public Client::Models::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        if (model.Value() == Client::Tables::PlayerSkeletonModel)
            return &player;
        return model.Value() == TransformedModel ? &transformed : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }

    Client::Models::Skeleton player;
    Client::Models::Skeleton transformed;
};

class MovingHero
{
public:
    MovingHero()
    {
        world.Runtime().map = 0;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        terrain.mapping = std::make_unique<Client::Data::Terrain::TerrainMapping>();
        auto& hero = world.Actors()[0];
        hero.alive = true;
        hero.type = Client::Tables::PlayerSkeletonModel;
        hero.cellX = hero.targetX = 182;
        hero.cellY = hero.targetY = 115;
        hero.position = {18250.0f, 11550.0f, 0.0f};
        hero.hands[0].model = hero.hands[1].model = hero.wings.model = hero.pet.model = Client::Kernel::NoItemModel;
        hero.alpha = 1.0f;
        hero.byte88 = 1;
        hero.baseMovementSpeed = 10;
        skeleton.actions.resize(205);
        for (auto& action : skeleton.actions)
            action.keyCount = 10;
        models.player = skeleton;
        models.transformed.actions.resize(7);
        for (auto& action : models.transformed.actions)
            action.keyCount = 10;
    }
    HeroMovementContext Context()
    {
        return {.world = world,
                .random = random,
                .playerSkeleton = &skeleton,
                .skills = &skills,
                .models = &models,
                .terrain = &terrain};
    }
    WorldState world;
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random;
    Client::Models::Skeleton skeleton;
    MovementModels models;
    Client::Tables::SkillTable skills{std::vector<std::byte>(Client::Tables::SkillTableSize)};
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
    context.pickTarget = [&]() -> Client::Core::Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
    {
        ++picks;
        return std::optional{Client::Pathfinding::PathCell{179, 116}};
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
    tested.terrain.attributes->cells[115 * 256 + 180] = 2;
    tested.world.Runtime().movement.mouseUpdateTime = 6;
    auto context = tested.Context();
    context.input.leftPressed = true;
    context.input.leftDown = true;
    context.pickTarget = []() -> Client::Core::Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
    { return std::optional{Client::Pathfinding::PathCell{179, 116}}; };
    std::vector<std::byte> bytes;
    context.sendWalk = [&](std::span<const Client::Protocol::Movement::Cell> path,
                           Client::Protocol::Movement::Cell target,
                           float angle) -> Client::Core::Expected<void, std::string>
    {
        const auto packet = Client::Protocol::Movement::WriteWalk(path, target, angle);
        if (!packet.HasValue())
            return Client::Core::Unexpected(std::string("invalid movement"));
        const auto decoded = Client::Network::Xor32Decode(packet.Value().packet);
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
// a 6 y el izquierdo suelto; con el 3 (el número de serie), antes cierra la entrada (0x56b6a9..0x56b72a): el campo
// elegido en 0, el buffer del mensaje vacío, el EDIT de [0x5763504] vacío, el chat cerrado y 0x77f3b6b en 0, sin
// tocar el foco ni el buffer del susurro. En Lorencia y en Devias, cada caso con un héroe quieto, porque el anterior
// lo deja caminando.
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
        if (window == 3)
        {
            // Lo que deja abierta la entrada: un número a medio escribir, el foco en el EDIT y un susurro guardado.
            Client::UI::ChatInputState& input = out.tested->world.Runtime().chat.input;
            input.dialogEdit.text.SetText("AB12");
            input.focus = Client::UI::ChatFocus::Dialog;
            input.field = 1;
            input.messageBuffer[0] = 'm';
            input.messageLength = 1;
            input.whisperBuffer[0] = 'w';
            out.tested->world.Runtime().events.serialEntry = true;
        }
        auto context = out.tested->Context();
        context.input.leftPressed = true;
        context.input.leftDown = true;
        context.input.mouseX = 130;
        context.input.mouseY = 240;
        context.screen = {800, 600};
        context.pickTarget = []() -> Client::Core::Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
        { return std::optional{Client::Pathfinding::PathCell{179, 116}}; };
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
        const auto closed = walk(map, 3, serial);
        ASSERT_TRUE(closed.HasValue()) << closed.Error();
        const Client::Kernel::WorldRuntime& runtime = serial.tested->world.Runtime();
        EXPECT_EQ(serial.sent, (std::vector<std::string>{"D3", "97"}));
        EXPECT_EQ(runtime.panels.field78abb00, 0);
        EXPECT_TRUE(runtime.movement.cursorWarpRequest.has_value());
        EXPECT_EQ(runtime.chat.input.field, Client::UI::MessageField);
        EXPECT_EQ(runtime.chat.input.dialogEdit.text.Value(), "");
        EXPECT_EQ(runtime.chat.input.whisperBuffer[0], 'w');
        EXPECT_EQ(runtime.chat.input.messageBuffer[0], '\0');
        EXPECT_EQ(runtime.chat.input.messageLength, 0);
        EXPECT_EQ(runtime.chat.input.focus, Client::UI::ChatFocus::Dialog);
        EXPECT_FALSE(runtime.ui.chatOpen);
        EXPECT_FALSE(runtime.events.serialEntry);
    }
}

// 0x56b3b2 → 0x65cae0(0x20, 0): caminar con el comercio abierto lo cierra después del D3, con el C1 03 3D en C3 por la
// sesión (0x657fe1) y el inventario con él. En Lorencia y en Devias.
TEST(GameMovementTest, AWalkWithTheTradeOpenCancelsItAfterTheD3)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<MovingHero>();
        tested->world.Runtime().map = map;
        tested->world.Runtime().movement.mouseUpdateTime = 6;
        tested->world.Runtime().panels.trade = 1;
        tested->world.Runtime().panels.inventory = 1;
        std::vector<std::vector<std::byte>> sent;
        auto context = tested->Context();
        context.input.leftPressed = true;
        context.input.leftDown = true;
        context.pickTarget = []() -> Client::Core::Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
        { return std::optional{Client::Pathfinding::PathCell{179, 116}}; };
        context.sendWalk = [&sent](auto, auto, float) -> Client::Core::Expected<void, std::string>
        {
            sent.push_back({std::byte{0xd3}});
            return {};
        };
        context.sendGamePacket = [&sent](Client::Protocol::Outgoing outgoing)
        {
            const auto decoded = Client::Network::Xor32Decode(outgoing.packet);
            EXPECT_EQ(outgoing.delivery, Client::Network::Delivery::Encrypted);
            sent.emplace_back(decoded.Bytes().begin(), decoded.Bytes().end());
        };
        const auto result = StepHeroMovement(context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(sent.size(), 2u);
        EXPECT_EQ(sent[0], (std::vector<std::byte>{std::byte{0xd3}}));
        EXPECT_EQ(sent[1], (std::vector<std::byte>{std::byte{0xc1}, std::byte{0x03}, std::byte{0x3d}}));
        EXPECT_EQ(tested->world.Runtime().panels.trade, 0);
        EXPECT_EQ(tested->world.Runtime().panels.inventory, 0);
    }
}

// 0x56b3dd → 0x65cae0(0x40000, 0) → 0x657b73 (CL9 hito 8): caminar con la ventana 2 del gremio abierta (la del Guild
// Master) la cierra después del D3 (0x6570b0): el C1 04 54 00 en claro por la sesión y el sonido 0x1c. En Lorencia y en
// Devias.
TEST(GameMovementTest, AWalkWithTheSecondGuildWindowOpenClosesItAfterTheD3)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<MovingHero>();
        tested->world.Runtime().map = map;
        tested->world.Runtime().movement.mouseUpdateTime = 6;
        tested->world.Runtime().panels.delegated.secondGuild = 1;
        std::vector<std::vector<std::byte>> sent;
        auto context = tested->Context();
        context.input.leftPressed = true;
        context.input.leftDown = true;
        context.pickTarget = []() -> Client::Core::Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
        { return std::optional{Client::Pathfinding::PathCell{179, 116}}; };
        context.sendWalk = [&sent](auto, auto, float) -> Client::Core::Expected<void, std::string>
        {
            sent.push_back({std::byte{0xd3}});
            return {};
        };
        context.sendGamePacket = [&sent](Client::Protocol::Outgoing outgoing)
        {
            const auto decoded = Client::Network::Xor32Decode(outgoing.packet);
            EXPECT_EQ(outgoing.delivery, Client::Network::Delivery::Clear);
            sent.emplace_back(decoded.Bytes().begin(), decoded.Bytes().end());
        };
        const auto result = StepHeroMovement(context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(sent.size(), 2u);
        EXPECT_EQ(sent[0], (std::vector<std::byte>{std::byte{0xd3}}));
        EXPECT_EQ(sent[1],
                  (std::vector<std::byte>{std::byte{0xc1}, std::byte{0x04}, std::byte{0x54}, std::byte{0x00}}));
        EXPECT_EQ(tested->world.Runtime().panels.delegated.secondGuild, 0);
        EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
    }
}

// 0x56b3d2, 0x56b41e y 0x56b48a → 0x65cae0(id, 0) → 0x657ab0: caminar con la tienda del NPC, el almacén o la caja del
// caos abiertos los cierra después del D3, con su paquete en claro por la sesión de las ventanas (el C1 03 31 de
// 0x659063, el C1 03 82 de 0x658567 y el C1 03 87 de 0x658767), la grilla del NPC vacía y el inventario con ellos.
// Sin esos cierres prestados, el juego terminaba con "cerrar la ventana del NPC sin el mundo". En Lorencia y en
// Devias.
TEST(GameMovementTest, AWalkWithAnNpcWindowOpenClosesItAfterTheD3)
{
    using Client::UI::PanelState;
    struct NpcWindowCase
    {
        std::uint8_t PanelState::*open;
        std::byte closeCode;
    };
    constexpr std::array<NpcWindowCase, 3> windows{{{&PanelState::npcShop, std::byte{0x31}},
                                                    {&PanelState::warehouse, std::byte{0x82}},
                                                    {&PanelState::chaosBox, std::byte{0x87}}}};
    for (const NpcWindowCase& window : windows)
    {
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(std::to_integer<int>(window.closeCode));
            SCOPED_TRACE(map);
            auto tested = std::make_unique<MovingHero>();
            auto& runtime = tested->world.Runtime();
            runtime.map = map;
            runtime.movement.mouseUpdateTime = 6;
            runtime.panels.*window.open = 1;
            runtime.panels.inventory = 1;
            runtime.npcWindows.grid[5].id = 0x0c01;
            runtime.npcWindows.grid[5].occupied = 1;
            std::vector<std::vector<std::byte>> sent;
            auto context = tested->Context();
            context.input.leftPressed = true;
            context.input.leftDown = true;
            context.pickTarget =
                []() -> Client::Core::Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
            { return std::optional{Client::Pathfinding::PathCell{179, 116}}; };
            context.sendWalk = [&sent](auto, auto, float) -> Client::Core::Expected<void, std::string>
            {
                sent.push_back({std::byte{0xd3}});
                return {};
            };
            context.sendNpcWindow =
                [&sent](Client::Protocol::Outgoing outgoing) -> Client::Core::Expected<void, std::string>
            {
                EXPECT_EQ(outgoing.delivery, Client::Network::Delivery::Clear);
                sent.emplace_back(outgoing.packet.Bytes().begin(), outgoing.packet.Bytes().end());
                return {};
            };
            const auto result = StepHeroMovement(context);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            ASSERT_EQ(sent.size(), 2u);
            EXPECT_EQ(sent[0], (std::vector<std::byte>{std::byte{0xd3}}));
            EXPECT_EQ(sent[1], (std::vector<std::byte>{std::byte{0xc1}, std::byte{0x03}, window.closeCode}));
            EXPECT_EQ(runtime.panels.*window.open, 0);
            EXPECT_EQ(runtime.panels.inventory, 0);
            if (window.open != &PanelState::chaosBox)
            {
                EXPECT_EQ(runtime.npcWindows.grid[5].id, 0xffff);
                EXPECT_EQ(runtime.npcWindows.grid[5].occupied, 0u);
            }
        }
    }
}

TEST(GameMovementTest, ATransformedHeroUsesItsSevenActionModelFromClickThroughArrival)
{
    MovingHero tested;
    auto& hero = tested.world.Actors()[0];
    hero.type = TransformedModel;
    tested.world.Runtime().movement.mouseUpdateTime = 6;
    tested.world.ActionSpeeds().Set(2, 3.25f);
    auto context = tested.Context();
    context.playerSkeleton = nullptr;
    context.input.leftPressed = true;
    context.input.leftDown = true;
    context.pickTarget = []() -> Client::Core::Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
    { return std::optional{Client::Pathfinding::PathCell{183, 115}}; };
    bool sentD3 = false;
    context.sendWalk = [&](std::span<const Client::Protocol::Movement::Cell>, Client::Protocol::Movement::Cell,
                           float) -> Client::Core::Expected<void, std::string>
    {
        sentD3 = true;
        return {};
    };

    auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_TRUE(sentD3);
    ASSERT_EQ(hero.movement, 1);

    context.input.leftPressed = false;
    context.input.leftDown = false;
    result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(hero.animation.action, 2);
    EXPECT_FLOAT_EQ(tested.world.ActionSpeeds().Speed(2, 0.0f), 3.25f);

    for (int frame = 0; frame < 80 && hero.movement != 0; ++frame)
    {
        result = StepHeroMovement(context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
    }
    EXPECT_EQ(hero.movement, 0);
    EXPECT_EQ(hero.animation.action, 0);
    EXPECT_FLOAT_EQ(tested.world.ActionSpeeds().Speed(2, 0.0f), 3.25f);
}

TEST(GameMovementTest, ATransformedHeroUsesTheBaseLibrarySkeletonWhenAttackingOnArrival)
{
    MovingHero tested;
    auto& runtime = tested.world.Runtime();
    auto& hero = tested.world.Actors()[0];
    hero.type = TransformedModel;
    runtime.movement.mouseUpdateTime = 6;
    tested.world.ActionSpeeds().Set(2, 3.25f);
    auto& target = tested.world.Actors()[13];
    target.alive = true;
    target.type = 0x10;
    target.position = {18450.0f, 11550.0f, 0.0f};
    runtime.movement.attackTarget = 13;
    auto context = tested.Context();
    context.playerSkeleton = nullptr;
    context.input.leftPressed = true;
    context.input.leftDown = true;
    context.pickTarget = []() -> Client::Core::Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
    { return std::optional{Client::Pathfinding::PathCell{183, 115}}; };
    context.sendWalk = [](std::span<const Client::Protocol::Movement::Cell>, Client::Protocol::Movement::Cell,
                          float) -> Client::Core::Expected<void, std::string> { return {}; };
    bool sentAttack = false;
    context.sendAttack = [&](std::uint16_t, std::uint8_t, std::uint8_t) -> Client::Core::Expected<void, std::string>
    {
        sentAttack = true;
        return {};
    };

    auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(hero.movement, 1);
    hero.movementType = 3;
    context.input.leftPressed = false;
    context.input.leftDown = false;
    for (int frame = 0; frame < 80 && hero.movement != 0; ++frame)
    {
        result = StepHeroMovement(context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
    }

    ASSERT_TRUE(sentAttack);
    EXPECT_EQ(hero.animation.action, 3);
    EXPECT_EQ(hero.attackTarget, 13);
    EXPECT_EQ(hero.movement, 0);
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
    tested.world.Runtime().events.cameraTour.stage = 2;
    tested.world.Runtime().events.cameraTour.waiting = false;
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
void NameTheActor(Client::Kernel::ActorState& actor, std::string_view name)
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
        EXPECT_EQ(input.whisper.text.Value(), "Viejo07");
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
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

// La caja de agregar un amigo (0x49d0b0) abierta, con su id en +0xf0 de [0x5763514] y su EDIT en +0xb4. Los ids son
// inventados.
constexpr std::int32_t AddBoxId = 0x77;
constexpr std::int32_t AddBoxEditId = 0x78;

void OpenTheAddFriendBox(Client::Kernel::WorldRuntime& runtime, Client::UI::OldWindowKind kind)
{
    Client::UI::OldWindow box;
    box.kind = kind;
    box.base.id = AddBoxId;
    box.add.edit.base.id = AddBoxEditId;
    box.add.edit.text.SetText("previo");
    runtime.oldWindows.windows.emplace(AddBoxId, std::move(box));
    runtime.oldWindows.letter = AddBoxId;
}

// 0x59afdf..0x59b033: con la caja de agregar un amigo y el derecho, el nombre del elegido (+0x1d9) va a su EDIT con la
// vtable + 0x4c (0x467ee0, SetWindowTextA) y la vtable + 0x30 con 1 (0x4682a0: el foco de Windows, el id del EDIT en
// 0x5762ec0 y EM_SETSEL(−2, −1), el cursor al final). Después sigue en 0x59b036, que con el chat cerrado no hace nada.
TEST(GameMovementTest, TheRightClickWithTheAddFriendBoxPutsTheChosenNameInItsEdit)
{
    const auto tested = AHeroRightClickingTheChosen(0);
    auto& runtime = tested->world.Runtime();
    runtime.ui.chatOpen = false;
    OpenTheAddFriendBox(runtime, Client::UI::OldWindowKind::AddFriendBox);

    const auto result = StepHeroMovement(tested->Context());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const Client::UI::OldEdit& edit = runtime.oldWindows.windows.at(AddBoxId).add.edit;
    EXPECT_EQ(edit.text.Value(), "Rival01");
    EXPECT_EQ(edit.text.Cursor(), 7u);
    EXPECT_FALSE(edit.text.HasSelection());
    EXPECT_EQ(runtime.chat.input.focus, Client::UI::ChatFocus::OldWindow);
    EXPECT_EQ(runtime.oldWindows.focusedEdit, AddBoxEditId);
    EXPECT_EQ(runtime.chat.input.whisperBuffer[0], 'x');
}

// 0x59b003..0x59b020: sin mirar el −1, un derecho sin elegido lee el nombre del registro −1, antes de la tabla de
// personajes ([0x743a16c] − 0x438 + 0x1d9): lo que hay ahí depende del heap (VM). Corta, y el EDIT queda como estaba.
TEST(GameMovementTest, TheRightClickWithTheAddFriendBoxAndNoChosenIsCut)
{
    const auto tested = AHeroRightClickingTheChosen(0);
    auto& runtime = tested->world.Runtime();
    OpenTheAddFriendBox(runtime, Client::UI::OldWindowKind::AddFriendBox);
    runtime.selectedTargets[2] = -1;

    const auto result = StepHeroMovement(tested->Context());

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x59b020"), std::string::npos) << result.Error();
    EXPECT_EQ(runtime.oldWindows.windows.at(AddBoxId).add.edit.text.Value(), "previo");
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
    chosen.type = Client::Tables::PlayerSkeletonModel;
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
            context.pickTarget =
                []() -> Client::Core::Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
            { return std::optional{Client::Pathfinding::PathCell{179, 116}}; };
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
        tested->terrain.attributes->cells[115 * 256 + 184] = wall;
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

// El clic de ataque con el elegido fuera de la grilla: en la fila −1 la línea de 0x558800 sale de TerrainWall
// (0x55886d) y en la columna 256 la ve (el índice lineal cae en la fila de al lado) pero PathFinding2 (0x59d653) la
// toma como índice lineal; MuRet descarta el clic como el de una pared, con las lecturas de antes (07, Q-C1179). Con
// Crywolf en la etapa 3 o 4, en el mapa 34 y un altar (0x204..0x208) en 0x6c8e54, PathFinding2 le pone la bandera al
// A* (0x4eac0a..0x4eac41): sin distancia, el WORD de la meta en TerrainWall queda en 0 (el 0x02 del personaje que la
// ocupa también) y el camino llega a la celda del elegido. En la etapa 2, con otro NPC y en Lorencia y en Devias la
// bandera no está: la meta ocupada queda en 0x02 y el camino es el de la segunda pasada, sin llegar a la celda.
TEST(GameMovementTest, AChosenOffTheGridDiscardsTheClickAndACrywolfAltarClearsTheGoal)
{
    constexpr std::size_t AltarSlot = 9;
    constexpr std::uint16_t Occupied = 0x02;
    struct Case
    {
        std::string_view name;
        std::int32_t map;
        glm::vec2 position;
        std::uint8_t stage;
        std::uint16_t altar;
        bool clears;
        int draws;
        int walks;
    };
    const glm::vec2 near{18550.0f, 11550.0f};
    const std::array cases{
        Case{"la fila −1", 0, {18250.0f, -150.0f}, 0, 0, false, BeforeTheClick + 2 * CellDraws + OwnDraws + 60, 0},
        Case{"la fila −1 en Devias",
             2,
             {18250.0f, -150.0f},
             0,
             0,
             false,
             BeforeTheClick + 2 * CellDraws + OwnDraws + 60,
             0},
        Case{"la columna 256", 0, {25650.0f, 11550.0f}, 0, 0, false, BeforeTheClick + 4 * CellDraws + OwnDraws + 60, 0},
        Case{"la columna 256 en Devias",
             2,
             {25650.0f, 11550.0f},
             0,
             0,
             false,
             BeforeTheClick + 4 * CellDraws + OwnDraws + 60,
             0},
        Case{"un altar en Crywolf", 34, near, 3, 0x204, true, -1, 1},
        Case{"el último altar en la etapa 4", 34, near, 4, 0x208, true, -1, 1},
        Case{"otro NPC en Crywolf", 34, near, 3, 0x209, false, -1, 1},
        Case{"un altar en la etapa 2", 34, near, 2, 0x204, false, -1, 1},
        Case{"un altar en Lorencia", 0, near, 3, 0x204, false, -1, 1},
        Case{"un altar en Devias", 2, near, 3, 0x204, false, -1, 1}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        const auto tested = AHeroClickingAPlayer(185);
        auto& runtime = tested->world.Runtime();
        runtime.map = probe.map;
        runtime.crywolf.stage = probe.stage;
        tested->world.Actors()[ChosenSlot].position = {probe.position.x, probe.position.y, 0.0f};
        auto& goalWall = tested->terrain.attributes->cells[115 * 256 + 185];
        const bool onTheGrid = probe.position == near;
        if (onTheGrid)
            goalWall = Occupied;
        if (probe.altar != 0)
        {
            tested->world.Actors()[AltarSlot].type = probe.altar;
            runtime.movement.chosenNpc = static_cast<std::int32_t>(AltarSlot);
        }
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
        EXPECT_EQ(walks, probe.walks);
        EXPECT_EQ(runtime.movement.attackTarget, static_cast<std::int32_t>(ChosenSlot));
        if (onTheGrid)
        {
            const auto& path = tested->world.Actors()[0].path;
            ASSERT_GE(path.count, 2);
            const auto last = path.cells[path.count - 1u];
            EXPECT_EQ(goalWall, probe.clears ? 0 : Occupied);
            EXPECT_EQ(last.x == 185 && last.y == 115, probe.clears);
        }
        if (probe.draws >= 0)
        {
            EXPECT_EQ(tested->random.State(), Advanced(start, probe.draws).State());
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
        const auto decoded = Client::Network::Xor32Decode(packet.Value().packet);
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
    context.pickTarget = [&]() -> Client::Core::Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
    {
        ++picks;
        return std::optional<Client::Pathfinding::PathCell>{};
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
    context.pickTarget = [&]() -> Client::Core::Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
    {
        EXPECT_EQ(tested.random.State(), expected.State());
        return std::optional{Client::Pathfinding::PathCell{183, 115}};
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
    context.pickTarget = []() -> Client::Core::Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
    { return std::optional{Client::Pathfinding::PathCell{183, 115}}; };
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
        std::vector<Client::Protocol::Items::ItemMove> sent;
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
                                                              [&sent](const Client::Protocol::Items::ItemMove& move)
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
    context.pickTarget = [&]() -> Client::Core::Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
    {
        ++picks;
        return std::optional<Client::Pathfinding::PathCell>{};
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
    tested.terrain.attributes->cells.fill(0x40);
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
