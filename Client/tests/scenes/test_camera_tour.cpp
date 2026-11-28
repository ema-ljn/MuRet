#include "Events/Crywolf/CameraTour.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Models/Cloth.h"
#include "Actors/ActorMotion.h"
#include "Kernel/ActorState.h"
#include "Events/EventGlobals.h"
#include "Assets/FaunaModels.h"
#include "Kernel/DrawTailState.h"
#include "FrontEnd/LoginModels.h"
#include "Monsters/MonsterCreation.h"
#include "FrontEnd/SelectionState.h"
#include "Session/ModuleStates.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Particles/Particles.h"
#include "Kernel/WorldMap.h"
#include "Effects/ModelEffects.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

// CL9 milestone 11, phases 3 and 5 (06 § El recorrido de la cámara): CCameraMove against the ASM of c32bbd5f. Only
// BD 00 with stage 2 starts it (0x403590, 0x4033f0, 0x4033d0, 0x404c90, 0x403300 and 0x403d60); the script (0x4035f0)
// moves the camera (0x403d90, 0x4034e0, 0x403430, 0x5fc800) and the characters from 10000 on (0x4039f0, 0x403760,
// 0x4036d0, 0x403900). The map does not matter: Lorencia, Devias and Crywolf give the same. The spawns use the repo's
// Data.
namespace
{
using Client::Crt::CrtRandom;
using Client::Events::CameraTour;
using Client::Events::Crywolf::CameraTourContext;
using Client::Kernel::ActorState;

constexpr std::uint32_t Seed = 0x7015;
constexpr std::array<std::int32_t, 3> Maps{0, 2, 34};

float CellCenter(std::int32_t cell)
{
    return static_cast<float>(cell * 100 + 50);
}

// The world, the UI that 0x403390 releases and the entry's resources, on the heap: WorldState weighs about 350 KB.
struct Tour
{
    Tour()
    {
        states.assets.fauna.TextureTable().Bind(store);
        models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        ActorState& hero = world.Actors()[0];
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        hero.key = 7;
        world.Runtime().hero = 0;
    }

    [[nodiscard]] CameraTour& State()
    {
        return world.Runtime().events.cameraTour;
    }

    // The tour running from script stage `scriptStage`, with the camera still.
    void Running(std::int32_t scriptStage, bool arrived = true, std::int32_t step = 0)
    {
        CameraTour& tour = State();
        tour.stage = 2;
        tour.waiting = false;
        tour.scriptStage = scriptStage;
        tour.arrived = arrived;
        tour.step = step;
    }

    // A script character built by hand in record `slot`, with the Player.bmd skeleton.
    ActorState& ScriptActor(std::size_t slot, std::int32_t key, glm::vec3 position, float angle = 0.0f)
    {
        ActorState& actor = world.Actors()[slot];
        actor = Client::Tests::UnarmedPlayer();
        actor.alive = true;
        actor.key = static_cast<std::uint16_t>(key);
        actor.position = position;
        actor.angles.z = angle;
        return actor;
    }

    [[nodiscard]] Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = world,
                .selection = selection,
                .dataRoot = dataRoot,
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = models,
                .log = log,
                .states = &states};
    }

    [[nodiscard]] Client::Core::Expected<void, std::string> Step(std::uint32_t now = 0)
    {
        resources.emplace(Resources());
        return Client::Events::Crywolf::StepTheCameraTour(
            CameraTourContext{.world = world,
                              .random = cloth.random,
                              .now = now,
                              .resources = withResources ? &*resources : nullptr,
                              .height = [](float, float) { return 0.0; },
                              .releaseTheUi =
                                  [this]()
                              {
                                  ++released;
                                  return Client::Core::Expected<void, std::string>{};
                              }});
    }

    // Before the world: FaunaModels releases its textures when destroyed.
    Client::Tests::FakeTextureStore store;
    Client::Kernel::WorldState world;
    Client::Session::ModuleStates states;
    std::filesystem::path dataRoot;
    Client::FrontEnd::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::FrontEnd::SingleModelLibrary models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    std::optional<Client::Scenes::WorldEntryResources> resources;
    bool withResources = true;
    int released = 0;
};

// With stage 2, 0x404c90(5000) starts its clock on the first step and fires at 5000 ms (signed, <=): the wait goes off
// and 0x4033d0 gives true. In the same step, 0x403390 and script stage 0 (0x403e00): the leg to (0x71, 0xe8) at 300
// per step, with +0x144 back at 0 and +0x140 at 0 although the clock raised it.
TEST(CameraTourTest, TheSecondStageStartsTheTourFiveSecondsLater)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<Tour>();
        tested->world.Runtime().map = map;
        CameraTour& tour = tested->State();
        tour.stage = 2;
        ASSERT_TRUE(tested->Step(1000).HasValue());
        EXPECT_EQ(tour.timerStart, 1000u);
        EXPECT_TRUE(tour.waiting);
        ASSERT_TRUE(tested->Step(5999).HasValue());
        EXPECT_TRUE(tour.waiting);
        EXPECT_FALSE(Client::Events::Crywolf::CameraTourRunning(tour));
        EXPECT_EQ(tested->released, 0);
        ASSERT_TRUE(tested->Step(6000).HasValue());
        EXPECT_FALSE(tour.waiting);
        EXPECT_TRUE(tour.timerRestart);
        EXPECT_TRUE(Client::Events::Crywolf::CameraTourRunning(tour));
        EXPECT_EQ(tested->released, 1);
        EXPECT_EQ(tour.scriptStage, 0);
        EXPECT_EQ(tour.step, 0);
        EXPECT_TRUE(tour.moving);
        EXPECT_TRUE(tour.begin);
        EXPECT_EQ(tour.speed, 300.0f);
        EXPECT_EQ(tour.target, glm::vec3(CellCenter(0x71), CellCenter(0xe8), 50.0f));
        // Esc (0x623edc) marks it as cut short: 0x4033d0 goes back to false and the step does nothing.
        tour.skipped = true;
        EXPECT_TRUE(tested->Step(7000).HasValue());
        EXPECT_EQ(tested->released, 1);
    }
}

// Other stages do not start it: the clock does not run and 0x4033d0 asks for 2. Stage 3 undoes it: 0x403d60 deletes
// the script characters (0x510ff0) and 0x403300 resets everything. Without the game UI, 0x403390 stops.
TEST(CameraTourTest, OtherStagesDoNotStartItAndTheThirdUndoesIt)
{
    auto tested = std::make_unique<Tour>();
    CameraTour& tour = tested->State();
    for (const std::int32_t stage : {0, 1, 4, 5, 6})
    {
        SCOPED_TRACE(stage);
        tour = CameraTour{};
        tour.stage = stage;
        for (const std::uint32_t now : {1000u, 10000u, 100000u})
            ASSERT_TRUE(tested->Step(now).HasValue());
        EXPECT_TRUE(tour.waiting);
        EXPECT_TRUE(tour.timerRestart);
        EXPECT_EQ(tour.step, 0);
    }
    tested->ScriptActor(3, 10000, {});
    tested->ScriptActor(4, 10001, {});
    tested->ScriptActor(5, 10002, {});
    tour.stage = 3;
    tour.waiting = false;
    tour.skipped = true;
    tour.scriptStage = 4;
    tour.nextActorKey = 10002;
    tour.turning[1] = false;
    tour.actionCounts[1] = 2;
    ASSERT_TRUE(tested->Step(1000).HasValue());
    EXPECT_FALSE(tested->world.Actors()[3].alive);
    EXPECT_FALSE(tested->world.Actors()[4].alive);
    EXPECT_TRUE(tested->world.Actors()[5].alive);
    EXPECT_EQ(tour.stage, 0);
    EXPECT_TRUE(tour.waiting);
    EXPECT_FALSE(tour.skipped);
    EXPECT_EQ(tour.scriptStage, 0);
    EXPECT_EQ(tour.nextActorKey, 10000);
    EXPECT_TRUE(tour.turning[1]);
    EXPECT_EQ(tour.actionCounts[1], 0);
    EXPECT_EQ(tour.speed, 100.0f);
    tested->Running(4);
    CameraTourContext bare{.world = tested->world, .random = tested->cloth.random};
    const auto cut = Client::Events::Crywolf::StepTheCameraTour(bare);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4035d3"), std::string::npos) << cut.Error();
}

// 0x4034e0 with 0x403430: the first leg starts from the hero (+0x144 becomes 1) and the camera moves travelled ·
// heading from the start, 300 per step; with travelled at the length, it reaches the target and the stage 1 script
// (0x403e30) runs on the next step: its first spawn (0x4039f0) stops without the entry's resources.
TEST(CameraTourTest, TheCameraLeavesFromTheHeroAndArrivesAtTheCell)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<Tour>();
        tested->world.Runtime().map = map;
        tested->withResources = false;
        const glm::vec3 hero{CellCenter(0x71), CellCenter(0xe8) - 900.0f, 50.0f};
        tested->world.Actors()[0].position = hero;
        CameraTour& tour = tested->State();
        tested->Running(0);
        ASSERT_TRUE(tested->Step().HasValue());
        for (int step = 0; step < 4; ++step)
        {
            SCOPED_TRACE(step);
            ASSERT_TRUE(tested->Step().HasValue());
            EXPECT_EQ(tour.scriptStage, 1);
            EXPECT_FALSE(tour.begin);
            EXPECT_EQ(tour.direction, glm::vec3(0.0f, 1.0f, 0.0f));
            EXPECT_EQ(tour.distance, 900.0f);
            if (step < 3)
            {
                EXPECT_TRUE(tour.moving);
                EXPECT_EQ(tour.start, hero);
                EXPECT_EQ(tour.position, hero + glm::vec3(0.0f, 300.0f * static_cast<float>(step), 0.0f));
                EXPECT_EQ(tour.travelled, 300.0f * static_cast<float>(step + 1));
            }
        }
        EXPECT_FALSE(tour.moving);
        EXPECT_TRUE(tour.arrived);
        EXPECT_EQ(tour.travelled, 0.0f);
        EXPECT_EQ(tour.position, tour.target);
        EXPECT_EQ(tour.start, tour.target);
        const auto created = tested->Step();
        ASSERT_FALSE(created.HasValue());
        EXPECT_NE(created.Error().find("0x403a11"), std::string::npos) << created.Error();
        EXPECT_EQ(tested->released, 6);
    }
}

// With +0x36 at 0 (the stage script ended), each stage moves to the next leg with 0x403d90: +0x140 at 0, +0x144 one
// more and the leg's speed. Stage 5 has no leg: it deletes the characters and marks the tour as finished.
TEST(CameraTourTest, EachStageHeadsToTheNextCellWhenItsScriptEnds)
{
    struct Leg
    {
        std::int32_t stage;
        std::int32_t x;
        std::int32_t y;
        float speed;
    };
    for (const Leg& leg :
         {Leg{1, 0x72, 0xdc, 40.0f}, Leg{2, 0x72, 0xa0, 300.0f}, Leg{3, 0x79, 0x4b, 300.0f}, Leg{4, 0x79, 0x57, 40.0f}})
    {
        SCOPED_TRACE(leg.stage);
        auto tested = std::make_unique<Tour>();
        tested->Running(leg.stage, false, 9);
        ASSERT_TRUE(tested->Step().HasValue());
        const CameraTour& tour = tested->State();
        EXPECT_EQ(tour.scriptStage, leg.stage + 1);
        EXPECT_EQ(tour.step, 0);
        EXPECT_TRUE(tour.moving);
        EXPECT_EQ(tour.speed, leg.speed);
        EXPECT_EQ(tour.target, glm::vec3(CellCenter(leg.x), CellCenter(leg.y), 50.0f));
    }
    // What leaves +0x36 at 0: the step after the last of each script.
    for (const auto& [stage, step] :
         {std::pair{1, 0x20}, std::pair{2, 8}, std::pair{3, 2}, std::pair{4, 1}, std::pair{5, 3}})
    {
        SCOPED_TRACE(stage);
        auto tested = std::make_unique<Tour>();
        tested->Running(stage, true, step);
        ASSERT_TRUE(tested->Step().HasValue());
        EXPECT_FALSE(tested->State().arrived);
        EXPECT_EQ(tested->State().scriptStage, stage);
    }
    auto tested = std::make_unique<Tour>();
    tested->ScriptActor(9, 10003, {});
    tested->Running(5, false);
    tested->State().nextActorKey = 10027;
    ASSERT_TRUE(tested->Step().HasValue());
    EXPECT_FALSE(tested->world.Actors()[9].alive);
    EXPECT_TRUE(tested->State().skipped);
    EXPECT_EQ(tested->State().scriptStage, 5);
    EXPECT_FALSE(Client::Events::Crywolf::CameraTourRunning(tested->State()));
}

// 0x404c90 in the script: stage 4 waits 3000 ms and stage 1, at step 7, 0x294; when it fires, +0x140 one more.
TEST(CameraTourTest, TheScriptTimersAdvanceTheStep)
{
    for (const auto& [stage, step, period] : {std::tuple{4, 0, 3000u}, std::tuple{1, 7, 0x294u}})
    {
        SCOPED_TRACE(stage);
        auto tested = std::make_unique<Tour>();
        tested->Running(stage, true, step);
        ASSERT_TRUE(tested->Step(500).HasValue());
        ASSERT_TRUE(tested->Step(500 + period - 1).HasValue());
        EXPECT_EQ(tested->State().step, step);
        ASSERT_TRUE(tested->Step(500 + period).HasValue());
        EXPECT_EQ(tested->State().step, step + 1);
        EXPECT_TRUE(tested->State().timerRestart);
    }
}

// 0x403760 at step 4 of stage 1: in its cell (ftol(ftol(x) · 0.01f)), the center, action 0 and +0x38 at 1; otherwise,
// while it turns (+0x38), by 3 degrees toward the angle of 0x4036d0 (180 south of the cell) with action 0; once facing,
// it stops turning, +0x386 with the speed, action 2 and 0x508a80. With the three in their cell, +0x140 one more.
TEST(CameraTourTest, TheScriptActorsTurnThenWalkToTheirCell)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<Tour>();
        tested->world.Runtime().map = map;
        tested->Running(1, true, 4);
        ActorState& there = tested->ScriptActor(1, 10000, {0x72 * 100 + 20.0f, 0xea * 100 + 70.0f, 0.0f}, 45.0f);
        ActorState& turning = tested->ScriptActor(2, 0x2711, {CellCenter(0x70), CellCenter(0xe8) - 500.0f, 0.0f});
        ActorState& facing =
            tested->ScriptActor(3, 0x2712, {CellCenter(0x74), CellCenter(0xe8) - 500.0f, 0.0f}, 180.0f);
        CameraTour& tour = tested->State();
        tour.turning[2] = true;
        ActorState walked = facing;
        walked.baseMovementSpeed = 0xc;
        ASSERT_TRUE(Client::Actors::UpdateActorPosition(
                        walked, {tested->world.Runtime(), tested->cloth.random, [](float, float) { return 0.0; }})
                        .HasValue());
        const auto before = tested->cloth.random.State();
        ASSERT_TRUE(tested->Step().HasValue());
        EXPECT_EQ(tested->cloth.random.State(), before);
        EXPECT_EQ(there.position, glm::vec3(CellCenter(0x72), CellCenter(0xea), 0.0f));
        EXPECT_EQ(there.animation.action, 0);
        EXPECT_TRUE(tour.turning[0]);
        EXPECT_EQ(turning.angles.z, 3.0f);
        EXPECT_EQ(turning.byte38a, 0);
        EXPECT_EQ(turning.animation.action, 0);
        EXPECT_TRUE(tour.turning[1]);
        EXPECT_FALSE(tour.turning[2]);
        EXPECT_EQ(facing.byte38a, 1);
        EXPECT_EQ(facing.baseMovementSpeed, 0xc);
        EXPECT_EQ(facing.animation.action, 2);
        EXPECT_EQ(facing.position, walked.position);
        EXPECT_EQ(tour.step, 4);
        // Once it stops turning it walks even with its back turned; with the three in their cell, step 5.
        turning.position = {CellCenter(0x70), CellCenter(0xe8), 0.0f};
        facing.position = {CellCenter(0x74) - 49.0f, CellCenter(0xe8) + 49.0f, 0.0f};
        ASSERT_TRUE(tested->Step().HasValue());
        EXPECT_EQ(tour.step, 5);
        EXPECT_TRUE(tour.turning[2]);
    }
}

// 0x403900: the action changes once per turn (+0x111 different), with +0x114 at 0, and counts in +0x78; with the
// requested count it advances, +0x78 at 0 and +0x140 one more. Without the key among the live ones it uses record 399
// (Q-C1349). With the effect (stage 5), the 0xed is created after counting.
TEST(CameraTourTest, TheScriptActionsCountOnlyWhenTheyChange)
{
    for (const bool present : {true, false})
    {
        SCOPED_TRACE(present);
        auto tested = std::make_unique<Tour>();
        ActorState& actor = present ? tested->ScriptActor(6, 10000, {}) : tested->world.Actors()[399];
        actor.animation.frame = 5.5f;
        tested->Running(1, true, 0x1e);
        ASSERT_TRUE(tested->Step().HasValue());
        EXPECT_EQ(actor.animation.action, 8);
        EXPECT_EQ(actor.animation.frame, 0.0f);
        EXPECT_EQ(tested->State().actionCounts[0], 1);
        EXPECT_EQ(tested->State().step, 0x1e);
        ASSERT_TRUE(tested->Step().HasValue());
        EXPECT_EQ(tested->State().actionCounts[0], 0);
        EXPECT_EQ(tested->State().step, 0x1f);
    }
    auto tested = std::make_unique<Tour>();
    ActorState& first = tested->ScriptActor(6, 0x2729, {});
    tested->ScriptActor(7, 0x272a, {});
    tested->Running(5, true, 1);
    const auto acted = tested->Step();
    ASSERT_TRUE(acted.HasValue()) << acted.Error();
    EXPECT_EQ(tested->world.Map().effects.Effects()[0].type, 0xed);
    EXPECT_EQ(first.animation.action, 3);
    EXPECT_EQ(tested->State().actionCounts[0x19], 1);
}

// The script spawns with the repo's Data, in Lorencia, Devias and Crywolf.
class CameraTourActorsTest : public Client::Tests::GoldenDataTest
{
protected:
    [[nodiscard]] std::unique_ptr<Tour> Load(std::int32_t map) const
    {
        auto tour = std::make_unique<Tour>();
        auto loaded = Client::Terrain::LoadWorldTerrain(m_dataRoot, static_cast<std::uint8_t>(map));
        EXPECT_TRUE(loaded.HasValue()) << loaded.Error();
        if (loaded.HasValue())
            tour->states.terrain.current = std::move(loaded).Value();
        tour->world.Runtime().map = map;
        tour->dataRoot = m_dataRoot;
        return tour;
    }
};

// 0x404830 at step 1: the 27 altar spawns (0x5141e0 with keys from 10000 on), without sparks or speed, with the spawns'
// rand() and nothing else; the key in +0x1f4, the hands at 0xffff and the alpha at 0. Two advance: +0x140 becomes 3.
TEST_F(CameraTourActorsTest, TheAltarsComeFromTheMonsterCreation)
{
    struct Spawn
    {
        std::int32_t type;
        std::uint8_t x;
        std::uint8_t y;
    };
    constexpr std::array<Spawn, 27> Spawns{
        {{0x154, 0x6e, 0x4d}, {0x154, 0x7d, 0x4d}, {0x155, 0x5a, 0x25}, {0x155, 0x6c, 0x49}, {0x155, 0x6d, 0x4b},
         {0x155, 0x6e, 0x49}, {0x155, 0x6f, 0x4b}, {0x155, 0x70, 0x49}, {0x155, 0x7b, 0x49}, {0x155, 0x7c, 0x4b},
         {0x155, 0x7d, 0x49}, {0x155, 0x7e, 0x4b}, {0x155, 0x7f, 0x49}, {0x155, 0xb0, 0x14}, {0x155, 0x75, 0x4d},
         {0x155, 0x77, 0x4d}, {0x155, 0x79, 0x4d}, {0x154, 0x77, 0x53}, {0x158, 0x76, 0x4f}, {0x158, 0x78, 0x4f},
         {0x158, 0x77, 0x5a}, {0x159, 0x74, 0x51}, {0x159, 0x77, 0x51}, {0x159, 0x7a, 0x51}, {0x154, 0x77, 0x57},
         {0x15c, 0x74, 0x5a}, {0x15c, 0x7a, 0x5a}}};
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        auto twin = Load(map);
        for (std::size_t index = 0; index < Spawns.size(); ++index)
        {
            const auto created =
                Client::Monsters::CreateMonster(Spawns[index].type, Spawns[index].x, Spawns[index].y,
                                                static_cast<std::int32_t>(10000 + index), twin->Resources());
            ASSERT_TRUE(created.HasValue()) << created.Error();
        }
        auto tested = Load(map);
        tested->Running(3, true, 1);
        ASSERT_TRUE(tested->Step().HasValue());
        EXPECT_EQ(tested->cloth.random.State(), twin->cloth.random.State());
        EXPECT_EQ(tested->State().step, 3);
        EXPECT_EQ(tested->State().nextActorKey, 10027);
        for (std::size_t slot = 0; slot < 400; ++slot)
        {
            const ActorState& expected = twin->world.Actors()[slot];
            const ActorState& actor = tested->world.Actors()[slot];
            ASSERT_EQ(actor.alive, expected.alive) << slot;
            if (!actor.alive || slot == 0)
                continue;
            EXPECT_EQ(actor.key, expected.key) << slot;
            EXPECT_EQ(actor.type, expected.type) << slot;
            EXPECT_EQ(actor.position, expected.position) << slot;
            EXPECT_EQ(actor.hands[0].model, 0xffff) << slot;
            EXPECT_EQ(actor.hands[1].model, 0xffff) << slot;
            EXPECT_EQ(actor.angles.z, 0.0f) << slot;
            EXPECT_EQ(actor.alpha, 0.0f) << slot;
        }
    }
}

// 0x403e30: the spawns in 34 complete their sparks; outside 34 the script model is still missing.
TEST_F(CameraTourActorsTest, TheGuardsCompleteTheirArrivalInCrywolf)
{
    constexpr std::int32_t CrywolfMap = 34;
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        auto barrier = Load(map);
        barrier->Running(1);
        const auto first = barrier->Step();
        if (map == CrywolfMap)
        {
            ASSERT_TRUE(first.HasValue()) << first.Error();
            EXPECT_EQ(barrier->State().step, 1);
        }
        auto twin = Load(map);
        ASSERT_TRUE(Client::Monsters::CreateMonster(0x154, 0x71, 0xee, 10000, twin->Resources()).HasValue());
        auto tested = Load(map);
        tested->Running(1, true, 1);
        const auto cut = tested->Step();
        if (map == CrywolfMap)
        {
            ASSERT_TRUE(cut.HasValue()) << cut.Error();
            for (int index = 0; index < 34; ++index)
                static_cast<void>(twin->cloth.random.Next());
        }
        else
        {
            ASSERT_FALSE(cut.HasValue());
            EXPECT_NE(cut.Error().find("0x403abe"), std::string::npos) << cut.Error();
        }
        EXPECT_EQ(tested->cloth.random.State(), twin->cloth.random.State());
        EXPECT_EQ(tested->State().nextActorKey, 10001);
        EXPECT_EQ(tested->State().step, map == CrywolfMap ? 2 : 1);
        if (map != CrywolfMap)
            continue;
        EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x243});
        const auto* skeleton = tested->states.assets.fauna.SkeletonOf(Client::Models::ModelId{0x1ae});
        ASSERT_NE(skeleton, nullptr);
        ASSERT_GT(skeleton->actions.size(), 2u);
        EXPECT_EQ(skeleton->actions[2].speed, std::bit_cast<float>(0x3e6872b0u));
    }
}

// 0x43114d/0x43116e and 0x403ad5..0x403cfb: with partial capacities the skipped spawn does not end the caller.
TEST_F(CameraTourActorsTest, TheBarrierArrivalContinuesAcrossPartialAndFullPools)
{
    struct Capacity
    {
        std::size_t joints;
        std::size_t particles;
        int draws;
    };
    for (const Capacity capacity : {Capacity{500, 3000, 34}, Capacity{5, 3000, 28}, Capacity{0, 3000, 22},
                                    Capacity{500, 0, 16}, Capacity{0, 0, 4}})
    {
        SCOPED_TRACE(capacity.joints);
        SCOPED_TRACE(capacity.particles);
        auto tested = Load(34);
        auto twin = Load(34);
        // The character creation has its own accesses/RNG; this real twin fixes only that prefix.
        const auto base = Client::Monsters::CreateMonster(0x15d, 0x72, 0xee, 10000, twin->Resources());
        ASSERT_TRUE(base.HasValue()) << base.Error();
        CrtRandom expected(twin->cloth.random.State());
        for (int draw = 0; draw < capacity.draws; ++draw)
            static_cast<void>(expected.Next());
        auto& joints = tested->world.DrawTail().joints;
        for (std::size_t index = capacity.joints; index < joints.size(); ++index)
            joints[index].alive = true;
        auto particles = tested->world.Particles().All();
        for (std::size_t index = capacity.particles; index < particles.size(); ++index)
            particles[index].alive = true;
        tested->Running(1);
        const auto stepped = tested->Step();
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested->State().step, 1);
        EXPECT_EQ(tested->State().nextActorKey, 10001);
        EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x243});
        EXPECT_EQ(tested->cloth.random.State(), expected.State());
        if (capacity.joints >= 10)
        {
            EXPECT_EQ(joints[0].type, 0x618);
            EXPECT_EQ(joints[1].subtype, 3);
            EXPECT_EQ(joints[2].type, 0x615);
            EXPECT_EQ(joints[2].subtype, 7);
            EXPECT_EQ(joints[6].type, 0x614);
            EXPECT_EQ(joints[6].subtype, 17);
        }
        if (capacity.particles >= 3)
        {
            EXPECT_EQ(particles[0].type, 0x5f6);
            EXPECT_EQ(particles[2].subtype, 1);
        }
    }
}
} // namespace
