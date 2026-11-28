#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Pathfinding/PathCell.h"
#include "Network/Xor32Packet.h"
#include "Models/IModelLibrary.h"
#include "Movement/GameMovement.h"
#include "Session/HeroMovementHooks.h"
#include "Items/InventoryMoves.h"
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
#include "Chat/ChatInput.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindows.h"
#include "Terrain/WorldTerrain.h"
#include "Network/Packet.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Items/Inventory.h"
#include "Events/EventGlobals.h"
#include "Npc/NpcWindowState.h"
#include "Ui/PanelRegistry.h"
#include "Audio/SoundId.h"
#include "Ui/GameUiState.h"
#include "Actors/PlayerActionSpeeds.h"
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
const Client::Session::HeroMovementHooks MovementHooks{};
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::Movement::HeroMovementContext;
using Client::Movement::StepHeroMovement;

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
                .terrain = &terrain,
                .hooks = &MovementHooks};
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
    const auto result =
        StepHeroMovement(HeroMovementContext{.world = world, .random = random, .hooks = &MovementHooks});
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
    const auto result =
        StepHeroMovement(HeroMovementContext{.world = world, .random = random, .hooks = &MovementHooks});
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
    const auto result =
        StepHeroMovement(HeroMovementContext{.world = world, .random = random, .hooks = &MovementHooks});
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(world.Runtime().warpCounter, 49);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().warpDepth, 0);
}

TEST(GameMovementTest, AnOrdinaryIdleStepClosesTheOwnRecord)
{
    MovingHero tested;
    CrtRandom expected = tested.random;
    // 0x57d114 calls 0x5526f0 even with skill 0: it closes 60 bytes after the own record.
    for (int i = 0; i < 8 + 3848 + 60; ++i)
        static_cast<void>(expected.Next());
    const auto result = StepHeroMovement(tested.Context());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.world.Runtime().mouse.updateTime, 1);
}

TEST(GameMovementTest, AClickBeforeTheWindowExpiresStaysPending)
{
    MovingHero tested;
    auto context = tested.Context();
    context.input.leftPressed = true;
    context.input.leftDown = true;
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(tested.world.Runtime().mouse.pendingClick);
    EXPECT_EQ(tested.world.Runtime().mouse.updateTime, 1);
}

TEST(GameMovementTest, AShiftClickIsConsumedWithoutPicking)
{
    MovingHero tested;
    tested.world.Runtime().mouse.updateTime = 6;
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
    EXPECT_FALSE(tested.world.Runtime().mouse.pendingClick);
    EXPECT_EQ(tested.world.Runtime().mouse.updateTime, 1);
}

TEST(GameMovementTest, TheMouseOnTheUiDoesNotAdvanceTheClickWindow)
{
    MovingHero tested;
    tested.world.Runtime().mouse.updateTime = 6;
    // 0x775857c: the step's UI sets it with the mouse on the bar (y ≥ 432) or over a panel.
    tested.world.Runtime().ui.mouseOnUi = 1;
    auto context = tested.Context();
    context.input.mouseY = 432;
    context.input.leftPressed = true;
    context.input.leftDown = true;
    const auto result = StepHeroMovement(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.world.Runtime().mouse.updateTime, 6);
    EXPECT_TRUE(tested.world.Runtime().mouse.pendingClick);
}

TEST(GameMovementTest, AClickFindsAndSendsTheHistoricalD3)
{
    MovingHero tested;
    // The cable does not capture occupants. This cell forces the documented path 7,6,7; on an empty grid the native A*
    // can pick 7,7,6 with the same goal.
    tested.terrain.attributes->cells[115 * 256 + 180] = 2;
    tested.world.Runtime().mouse.updateTime = 6;
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
    EXPECT_EQ(tested.world.Runtime().mouse.updateTimeMax, 16);
}
// CL9 milestone 11, 0x56b4db..0x56b78f: with the event window (0x78abb00) open, the C1 03 97 goes after the D3,
// 0x78abb00 goes back to 0, the cursor stays requested at SetCursorPos(x · width / 260, y · height / 480) (Q-C1330),
// 0x6c8dcc to 6 and the left button released; with 3 (the serial number), it first closes the entry
// (0x56b6a9..0x56b72a): the chosen field at 0, the message buffer empty, the [0x5763504] EDIT empty, the chat closed
// and 0x77f3b6b at 0, without touching the focus or the whisper buffer. Each case with an idle hero, because the
// previous one leaves it walking.
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
        out.tested->world.Runtime().mouse.updateTime = 6;
        out.tested->world.Runtime().panels.field78abb00 = window;
        if (window == 3)
        {
            // What the open entry leaves: a half-typed number, the focus on the EDIT and a stored whisper.
            Client::Ui::ChatInputState& input = out.tested->world.Runtime().chatInput;
            input.dialogEdit.text.SetText("AB12");
            input.focus = Client::Ui::ChatFocus::Dialog;
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
            const Client::Movement::GameMovementState& movement = left.tested->world.Runtime().movement;
            const Client::Kernel::MouseState& mouse = left.tested->world.Runtime().mouse;
            EXPECT_EQ(left.sent, (std::vector<std::string>{"D3", "97"}));
            ASSERT_TRUE(movement.cursorWarpRequest.has_value());
            EXPECT_EQ(*movement.cursorWarpRequest, (std::array<std::int32_t, 2>{400, 300}));
            EXPECT_EQ(left.tested->world.Runtime().panels.field78abb00, 0);
            EXPECT_EQ(mouse.updateTimeMax, 6);
            EXPECT_FALSE(mouse.leftHeld);
        }

        Walk serial;
        const auto closed = walk(map, 3, serial);
        ASSERT_TRUE(closed.HasValue()) << closed.Error();
        const Client::Kernel::WorldRuntime& runtime = serial.tested->world.Runtime();
        EXPECT_EQ(serial.sent, (std::vector<std::string>{"D3", "97"}));
        EXPECT_EQ(runtime.panels.field78abb00, 0);
        EXPECT_TRUE(runtime.movement.cursorWarpRequest.has_value());
        EXPECT_EQ(runtime.chatInput.field, Client::Ui::MessageField);
        EXPECT_EQ(runtime.chatInput.dialogEdit.text.Value(), "");
        EXPECT_EQ(runtime.chatInput.whisperBuffer[0], 'w');
        EXPECT_EQ(runtime.chatInput.messageBuffer[0], '\0');
        EXPECT_EQ(runtime.chatInput.messageLength, 0);
        EXPECT_EQ(runtime.chatInput.focus, Client::Ui::ChatFocus::Dialog);
        EXPECT_FALSE(runtime.ui.chatOpen);
        EXPECT_FALSE(runtime.events.serialEntry);
    }
}

// 0x56b3b2 → 0x65cae0(0x20, 0): walking with the trade open closes it after the D3, with the C1 03 3D in C3 through the
// session (0x657fe1) and the inventory with it.
TEST(GameMovementTest, AWalkWithTheTradeOpenCancelsItAfterTheD3)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<MovingHero>();
        tested->world.Runtime().map = map;
        tested->world.Runtime().mouse.updateTime = 6;
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

// 0x56b3dd → 0x65cae0(0x40000, 0) → 0x657b73 (CL9 milestone 8): walking with guild window 2 open (the Guild Master's)
// closes it after the D3 (0x6570b0): the C1 04 54 00 in clear through the session and sound 0x1c.
TEST(GameMovementTest, AWalkWithTheSecondGuildWindowOpenClosesItAfterTheD3)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<MovingHero>();
        tested->world.Runtime().map = map;
        tested->world.Runtime().mouse.updateTime = 6;
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

// 0x56b3d2, 0x56b41e and 0x56b48a → 0x65cae0(id, 0) → 0x657ab0: walking with the NPC shop, the warehouse or the chaos
// box open closes them after the D3, with their packet in clear through the windows session (0x659063's C1 03 31,
// 0x658567's C1 03 82 and 0x658767's C1 03 87), the NPC grid empty and the inventory with them.
TEST(GameMovementTest, AWalkWithAnNpcWindowOpenClosesItAfterTheD3)
{
    using Client::Ui::PanelState;
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
            runtime.mouse.updateTime = 6;
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
    tested.world.Runtime().mouse.updateTime = 6;
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
    runtime.mouse.updateTime = 6;
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
    EXPECT_TRUE(tested.world.Runtime().mouse.pendingClick);
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

// An invented name for the chosen character and another for the history slot.
void NameTheActor(Client::Kernel::ActorState& actor, std::string_view name)
{
    actor.name.fill(std::byte{0});
    for (std::size_t i = 0; i < name.size(); ++i)
        actor.name[i] = static_cast<std::byte>(name[i]);
}

// What 0x59b036..0x59b086 asks for: the chat open, the hero in the safe zone, a chosen one with +0x376 at 0xff and the
// right click.
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
    auto& input = runtime.chatInput;
    input.whisperBuffer.fill('x');
    auto& slot = input.whisperNames[static_cast<std::size_t>(input.whisperNameAt)];
    slot.fill('y');
    Client::Ui::CopyIntoTheBuffer(slot, "Viejo07");
    return tested;
}

// 0x59b036..0x59b185 (Q-C804): the right click on the chosen one is consumed, the name goes to the whisper buffer
// (fully emptied first) and to the history slot, with its length and sound 0x19; the whisper EDIT takes what was in
// the slot.
TEST(GameMovementTest, TheRightClickOnTheChosenWritesItsNameAndLeavesTheOldOneInTheEdit)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = AHeroRightClickingTheChosen(map);
        const auto result = StepHeroMovement(tested->Context());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        const auto& runtime = tested->world.Runtime();
        const auto& input = runtime.chatInput;
        const auto& slot = input.whisperNames[static_cast<std::size_t>(input.whisperNameAt)];
        EXPECT_FALSE(runtime.ui.rightClick);
        EXPECT_EQ(Client::Ui::CStringOf(input.whisperBuffer), "Rival01");
        EXPECT_TRUE(
            std::all_of(input.whisperBuffer.begin() + 7, input.whisperBuffer.end(), [](char c) { return c == '\0'; }));
        EXPECT_EQ(input.whisperLength, 7);
        EXPECT_EQ(Client::Ui::CStringOf(slot), "Rival01");
        // strcpy: after the 0, the slot keeps what it had.
        EXPECT_EQ(slot[8], 'y');
        EXPECT_EQ(input.whisper.text.Value(), "Viejo07");
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
    }
}

// 0x59b092..0x59b0a1: in Chaos Castle the right click is consumed and nothing else; with +0x376 other than 0xff (a
// player's 12, 0x511290's 0xffff) 0x59b036 does not consume it and 0x57cfc0 consumes it at the end (0x57d1b1), without
// the name.
TEST(GameMovementTest, TheRightClickOnTheChosenNeedsItsWordAndAMapOutsideChaosCastle)
{
    {
        const auto tested = AHeroRightClickingTheChosen(18);
        const auto result = StepHeroMovement(tested->Context());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        const auto& input = tested->world.Runtime().chatInput;
        EXPECT_FALSE(tested->world.Runtime().ui.rightClick);
        EXPECT_EQ(Client::Ui::CStringOf(input.whisperNames[static_cast<std::size_t>(input.whisperNameAt)]), "Viejo07");
        EXPECT_EQ(input.whisperBuffer[0], 'x');
        EXPECT_FALSE(tested->world.Runtime().lastSoundId.has_value());
    }
    {
        const auto tested = AHeroRightClickingTheChosen(0);
        tested->world.Actors()[5].word376 = 0xffff;
        const auto result = StepHeroMovement(tested->Context());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_FALSE(tested->world.Runtime().ui.rightClick);
        EXPECT_EQ(tested->world.Runtime().chatInput.whisperBuffer[0], 'x');
        EXPECT_FALSE(tested->world.Runtime().lastSoundId.has_value());
    }
}

// 0x59afdf..0x59b001: the letter window (+0xf0) counts only if 0x490a00 finds it; without it, the name does not go to
// the whisper and 0x57cfc0 consumes the right click (0x57d1b1).
TEST(GameMovementTest, ALetterIdWithoutItsWindowDoesNotTakeTheRightClick)
{
    const auto tested = AHeroRightClickingTheChosen(0);
    tested->world.Runtime().ui.chatOpen = false;
    tested->world.Runtime().oldWindows.letter = 0x77;
    const auto result = StepHeroMovement(tested->Context());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FALSE(tested->world.Runtime().ui.rightClick);
    EXPECT_EQ(tested->world.Runtime().chatInput.whisperBuffer[0], 'x');
    EXPECT_FALSE(tested->world.Runtime().lastSoundId.has_value());
}

// The add-friend box (0x49d0b0) open, with its id in +0xf0 of [0x5763514] and its EDIT in +0xb4. The ids are invented.
constexpr std::int32_t AddBoxId = 0x77;
constexpr std::int32_t AddBoxEditId = 0x78;

void OpenTheAddFriendBox(Client::Kernel::WorldRuntime& runtime, Client::Ui::OldWindowKind kind)
{
    Client::Ui::OldWindow box;
    box.kind = kind;
    box.base.id = AddBoxId;
    box.add.edit.base.id = AddBoxEditId;
    box.add.edit.text.SetText("previo");
    runtime.oldWindows.windows.emplace(AddBoxId, std::move(box));
    runtime.oldWindows.letter = AddBoxId;
}

// 0x59afdf..0x59b033: with the add-friend box and the right click, the chosen one's name (+0x1d9) goes to its EDIT with
// the vtable + 0x4c (0x467ee0, SetWindowTextA) and the vtable + 0x30 with 1 (0x4682a0: the Windows focus, the EDIT id
// in 0x5762ec0 and EM_SETSEL(−2, −1), the cursor at the end). Then it goes on at 0x59b036, which with the chat closed
// does nothing.
TEST(GameMovementTest, TheRightClickWithTheAddFriendBoxPutsTheChosenNameInItsEdit)
{
    const auto tested = AHeroRightClickingTheChosen(0);
    auto& runtime = tested->world.Runtime();
    runtime.ui.chatOpen = false;
    OpenTheAddFriendBox(runtime, Client::Ui::OldWindowKind::AddFriendBox);

    const auto result = StepHeroMovement(tested->Context());

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const Client::Ui::OldEdit& edit = runtime.oldWindows.windows.at(AddBoxId).add.edit;
    EXPECT_EQ(edit.text.Value(), "Rival01");
    EXPECT_EQ(edit.text.Cursor(), 7u);
    EXPECT_FALSE(edit.text.HasSelection());
    EXPECT_EQ(runtime.chatInput.focus, Client::Ui::ChatFocus::OldWindow);
    EXPECT_EQ(runtime.oldWindows.focusedEdit, AddBoxEditId);
    EXPECT_EQ(runtime.chatInput.whisperBuffer[0], 'x');
}

// 0x59b003..0x59b020: without checking −1, a right click with no chosen one reads the name of record −1, before the
// character table ([0x743a16c] − 0x438 + 0x1d9): what is there depends on the heap (VM). It stops, and the EDIT stays
// as it was.
TEST(GameMovementTest, TheRightClickWithTheAddFriendBoxAndNoChosenIsCut)
{
    const auto tested = AHeroRightClickingTheChosen(0);
    auto& runtime = tested->world.Runtime();
    OpenTheAddFriendBox(runtime, Client::Ui::OldWindowKind::AddFriendBox);
    runtime.selectedTargets[2] = -1;

    const auto result = StepHeroMovement(tested->Context());

    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x59b020"), std::string::npos) << result.Error();
    EXPECT_EQ(runtime.oldWindows.windows.at(AddBoxId).add.edit.text.Value(), "previo");
}

// The attack click (CL9 milestone 3): MovingHero's hero at (182, 115) and another player chosen in slot 5, at `cellX`.
// The keys are invented.
constexpr std::size_t ChosenSlot = 5;
constexpr std::uint16_t ChosenKey = 0x0456;
constexpr int OwnDraws = 3848;
constexpr int CellDraws = 4;
// The two closes of the wait counter (0x59af50) and the own record the click closes (0x59bd01).
constexpr int BeforeTheClick = 2 * CellDraws + OwnDraws;
// After sending the path: the two windows SendMove checks, and 0x57cfc0 (the own record and Skill.bmd).
constexpr int AfterTheWalk = 2 + OwnDraws + 60;

std::unique_ptr<MovingHero> AHeroClickingAPlayer(std::uint8_t cellX)
{
    auto tested = std::make_unique<MovingHero>();
    auto& runtime = tested->world.Runtime();
    runtime.mouse.updateTime = 6;
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

// 0x59cd2e..0x59cd43 and 0x59d3de..0x59d8b6: outside the safe zone, a chosen player with Ctrl down (0x558e7f) is
// attacked: the target to 0x6c8ebc, 0x6c8e38 at 1, +0x379 at 3 and its cell to 0x77e1ba8; it is seen (0x558800), the
// path is found and, without a ranged weapon, SendMove with its cell as the goal. Without Ctrl, or in the safe zone,
// the same click walks to the terrain.
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
            EXPECT_EQ(runtime.mouse.updateTime, 1);
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
            // The Y and the X to see, again for the path, and the hands.
            const CrtRandom atTheWalk = Advanced(start, BeforeTheClick + 4 * CellDraws + OwnDraws);
            EXPECT_EQ(walk.randomState, atTheWalk.State());
            EXPECT_EQ(tested->random.State(), Advanced(atTheWalk, AfterTheWalk).State());
        }
    }
}

// 0x558800: a cell with the WORD at 4 or more and without 0x20, 0x40 or 0x80 between the hero and the chosen one
// blocks: the target is already noted, but there is no path and the click ends as always (0x59e248). With 0x20 it is
// seen.
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
        EXPECT_EQ(tested->world.Runtime().mouse.updateTime, 1);
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

// The attack click with the chosen one off the grid: on row −1 0x558800's line leaves TerrainWall (0x55886d) and on
// column 256 it sees it (the linear index falls on the next row) but PathFinding2 (0x59d653) takes it as a linear
// index; MuRet discards the click as a wall's, with the earlier reads (07, Q-C1179). With Crywolf at stage 3 or 4, on
// map 34 and an altar (0x204..0x208) in 0x6c8e54, PathFinding2 gives the A* the flag (0x4eac0a..0x4eac41): no
// distance, the goal's WORD in TerrainWall stays at 0 (also the 0x02 of the character on it) and the path reaches the
// chosen one's cell. At stage 2, with another NPC and in Lorencia and Devias there is no flag: the occupied goal stays
// at 0x02 and the path is the second pass's, without reaching the cell.
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
            runtime.chosenNpc.index = static_cast<std::int32_t>(AltarSlot);
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

// 0x59d856..0x59d8bb and 0x561c50: with the bow in the left the path is not sent; without arrows in the right,
// 0x561840 looks for them (text 251, type 2, with none) and MoveHero leaves (0x59e2c0) without adding to the counter
// or going through 0x57cfc0.
TEST(GameMovementTest, ABowWithoutArrowsLeavesMoveHero)
{
    const auto tested = AHeroClickingAPlayer(185);
    tested->world.OwnCharacter().equipment[0].id = 0xffff;
    tested->world.OwnCharacter().equipment[1].id = 0x800;
    std::vector<std::size_t> texts;
    const Client::Items::InventoryServices inventory{
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
    EXPECT_EQ(tested->world.Runtime().mouse.updateTime, 0);
    EXPECT_EQ(tested->world.Actors()[0].path.count, 4u);
    // The cells, the hands and 0x561c50's two accesses.
    EXPECT_EQ(tested->random.State(), Advanced(start, BeforeTheClick + 4 * CellDraws + 3 * OwnDraws).State());
}

// 0x59cd49..0x59d3d7 and 0x59d65f..0x59d678: the elf walking with the bow first sends its logical cell (C1 05 DF X Y,
// the one already ahead); with arrows, 0x5650c0(hero, hero, 1) hits from there if in range (six cells with the bow):
// the D3 to turn and the D7 with the chosen one's key.
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
    // 0x5653c1 sets the counter to its cap before 0x59e248's sum.
    const Client::Kernel::MouseState& mouse = tested->world.Runtime().mouse;
    EXPECT_EQ(mouse.updateTime, mouse.updateTimeMax + 1);
}

// 0x59b89e..0x59b8cd: with the auto attack, the other chosen (0x6c8e38) at 1 and a chosen one, the click comes back by
// itself, without a button; outside map 6 and Chaos Castle. 0x59b22f: the same with 0x6c8e38 other than −1 sets the
// head to 0 and the idle hero does not turn (0x59b3c4).
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
        tested.world.Runtime().skillDepths[0] = static_cast<std::uint8_t>(depths[1]);
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
    tested.world.Runtime().skillDepths[2] = 1;
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
    tested.world.Runtime().mouse.updateTime = 6;
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
    EXPECT_EQ(tested.world.Runtime().mouse.updateTime, 7);
    EXPECT_FALSE(tested.world.Runtime().mouse.pendingClick);
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
        tested.world.Runtime().mouse.updateTime = 6;
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
    tested.world.Runtime().mouse.updateTime = 6;
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

// What happens on a click depending on the own record's hands: walked says whether the D3 went out.
struct HandsClick
{
    Client::Core::Expected<void, std::string> result;
    bool walked = false;
};

HandsClick ClickWithTheHands(std::uint16_t right, std::uint16_t left, std::int32_t cursorItemCount = 0,
                             const Client::Items::InventoryServices* inventory = nullptr)
{
    MovingHero tested;
    tested.world.OwnCharacter().equipment[0].id = right;
    tested.world.OwnCharacter().equipment[1].id = left;
    tested.world.Runtime().cursorItemCount = cursorItemCount;
    tested.world.Runtime().mouse.updateTime = 6;
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

// 0x59bbe9..0x59bd38: the right is +0xb6c and the left +0xbb4. With the sword in the right and the left empty, the most
// common case, the click walks.
TEST(GameMovementTest, AWeaponInTheRightHandWithTheLeftEmptyWalks)
{
    for (const std::uint16_t right : {std::uint16_t{0x0000}, std::uint16_t{0x0205}, std::uint16_t{0x0a00}})
    {
        const auto click = ClickWithTheHands(right, EmptyHand);
        ASSERT_TRUE(click.result.HasValue()) << click.result.Error();
        EXPECT_TRUE(click.walked) << right;
    }
}

// 0x59bd3e..0x59bf56: with the right empty and a weapon of groups 0 to 2 or 5 in the left, the original moves it to
// the cursor, dresses the hero and sends the C1 0E 24 from slot 1 to 0 (Items::MoveTheLeftWeaponToTheRight); then it
// goes on with the click (0x59cd23). Without someone to dress the hero it stops at 0x59bf23, with the record already
// closed.
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
        const Client::Items::InventoryServices inventory{.items = nullptr,
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

// Outside those groups (spears, bows, shields), with the right occupied or with an item on the cursor
// ([0x7879fbc] > 0), the original goes on to 0x59cd23.
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
    Client::Kernel::MouseState& mouse = tested.world.Runtime().mouse;
    mouse.updateTime = 6;
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
    EXPECT_FALSE(mouse.leftHeld);
    EXPECT_FALSE(mouse.pendingClick);
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
    Client::Kernel::MouseState& mouse = tested.world.Runtime().mouse;
    state.heldSince = 42;
    ASSERT_TRUE(StepHeroMovement(context).HasValue());
    EXPECT_FLOAT_EQ(state.heldSince, 42);
    mouse.updateTime = 6;
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
