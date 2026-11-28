#include "Models/IModelLibrary.h"
#include "Monsters/ModelBody.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Models/Model.h"
#include "Math/OriginalAngles.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Render/TextureSlot.h"
#include "Kernel/CharacterHead.h"
#include "Actors/CharacterStep.h"
#include "Session/CharacterStepHooks.h"
#include "Assets/FaunaModels.h"
#include "Effects/ActorEffectHooks.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Terrain/GroundMarks.h"
#include "Terrain/WorldTerrain.h"
#include "Terrain/TerrainLighting.h"
#include "support/FakeTextureStore.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/SyntheticModels.h"
#include "support/TemporaryFolder.h"
#include "support/TextureFiles.h"
#include "CastleSiege/CastleSiegeState.h"
#include "Effects/ModelEffects.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The body of monsters and NPCs (P-C52 d): 0x4f9580, 0x4fcbb0, 0x42d890, 0x4ea190, 0x4ea0e0 and the per-type cuts of
// 0x4fcee0 and 0x505de0.
namespace
{
const Client::Session::CharacterStepHooks StepHooks{};
const Client::Effects::ActorEffectHooks EffectHooks{};
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Models::ActionInfo;
using Client::Models::Skeleton;
using Client::Monsters::ModelActionEnd;

constexpr std::uint32_t Seed = 0x2468ace1u;

void Skip(CrtRandom& random, int draws)
{
    for (int draw = 0; draw < draws; ++draw)
        static_cast<void>(random.Next());
}

Skeleton EightActions()
{
    Skeleton skeleton;
    skeleton.actions.assign(8, ActionInfo{.repeats = true, .speed = 0.25f, .keyCount = 10});
    return skeleton;
}

ActorState Monster(std::uint16_t type, std::uint8_t action)
{
    ActorState actor;
    actor.alive = true;
    actor.type = type;
    actor.animation.action = action;
    actor.animation.frame = 4.0f;
    return actor;
}

TEST(ModelBodyTest, TheSpeedOfTheOtherModelsByType)
{
    using Client::Monsters::ModelSpeedOf;
    EXPECT_EQ(ModelSpeedOf(0xa8, 0, 0.0f, 0.3f), 0.6f);
    EXPECT_EQ(ModelSpeedOf(0xac, 6, 7.0f, 0.3f), 0.6f);
    // 0x4f96f7..0x4f9712: the 0x192 in action 6 only past frame 6.
    EXPECT_EQ(ModelSpeedOf(0x192, 6, 6.5f, 0.3f), 0.3f * 4.0f);
    EXPECT_EQ(ModelSpeedOf(0x192, 6, 6.0f, 0.3f), 0.3f);
    EXPECT_EQ(ModelSpeedOf(0x192, 5, 7.0f, 0.3f), 0.3f);
    EXPECT_EQ(ModelSpeedOf(0x1a9, 6, 0.0f, 0.3f), 0.3f * 0.5f);
    EXPECT_EQ(ModelSpeedOf(0x1a9, 1, 0.0f, 0.3f), 0.3f);
    EXPECT_EQ(ModelSpeedOf(0x153, 6, 9.0f, 0.3f), 0.3f);
}

// 0x4fcbb0: monsters go back to 0 from 1, 3, 4, 5, 8, 9 and 0xb without rand(); 2 and 7 go on; 6 with +0x38a at 0 sets
// it to 1 and requests the remains (0x4fcc3c, 0x52e810), and with it at 1 does nothing.
TEST(ModelBodyTest, TheMonstersGoBackToRestWithoutRandom)
{
    const Skeleton skeleton = EightActions();
    Client::Kernel::WorldRuntime runtime;
    for (const std::uint8_t action : std::array<std::uint8_t, 4>{1, 3, 4, 5})
    {
        SCOPED_TRACE(static_cast<int>(action));
        ActorState actor = Monster(0x153, action);
        CrtRandom random(Seed);
        const auto end = Client::Monsters::FinishTheModelAction(actor, skeleton, runtime, random);
        ASSERT_TRUE(end.HasValue()) << end.Error();
        EXPECT_EQ(end.Value(), ModelActionEnd::Done);
        EXPECT_EQ(actor.animation.action, 0);
        EXPECT_EQ(actor.animation.frame, 0.0f);
        EXPECT_EQ(random.State(), Seed);
    }
    for (const std::uint8_t action : std::array<std::uint8_t, 2>{2, 7})
    {
        ActorState actor = Monster(0x1c9, action);
        CrtRandom random(Seed);
        ASSERT_TRUE(Client::Monsters::FinishTheModelAction(actor, skeleton, runtime, random).HasValue());
        EXPECT_EQ(actor.animation.action, action);
        EXPECT_EQ(random.State(), Seed);
    }
    ActorState dying = Monster(0x160, 6);
    CrtRandom random(Seed);
    const auto left = Client::Monsters::FinishTheModelAction(dying, skeleton, runtime, random);
    ASSERT_TRUE(left.HasValue()) << left.Error();
    EXPECT_EQ(left.Value(), ModelActionEnd::LeaveRemains);
    EXPECT_EQ(dying.byte38a, 1);
    const auto again = Client::Monsters::FinishTheModelAction(dying, skeleton, runtime, random);
    ASSERT_TRUE(again.HasValue()) << again.Error();
    EXPECT_EQ(again.Value(), ModelActionEnd::Done);
    EXPECT_EQ(dying.animation.action, 6);
    EXPECT_EQ(random.State(), Seed);
}

// 0x4fcc77..0x4fccf7: 0x1ce and 0x1cf draw with rand() % 16 (below 12 action 0, otherwise 1 + rand() % 2); the other
// non-monsters, with rand() % 2; NewFace does not draw; 0xc5 sets the action byte to 1 without 0x4ea030; 0x28 on map 1
// goes to 0.
TEST(ModelBodyTest, TheNpcsDrawTheirNextAction)
{
    const Skeleton skeleton = EightActions();
    Client::Kernel::WorldRuntime runtime;
    for (std::uint32_t seed = 1; seed < 40; ++seed)
    {
        SCOPED_TRACE(seed);
        CrtRandom expected(seed);
        const int first = expected.Next() % 16;
        const int action = first < 12 ? 0 : 1 + expected.Next() % 2;
        ActorState smith = Monster(0x1ce, 3);
        CrtRandom random(seed);
        ASSERT_TRUE(Client::Monsters::FinishTheModelAction(smith, skeleton, runtime, random).HasValue());
        EXPECT_EQ(smith.animation.action, action);
        EXPECT_EQ(random.State(), expected.State());

        CrtRandom other(seed);
        const int otherAction = CrtRandom(seed).Next() % 2;
        ActorState girl = Monster(0x1cd, 3);
        ASSERT_TRUE(Client::Monsters::FinishTheModelAction(girl, skeleton, runtime, other).HasValue());
        EXPECT_EQ(girl.animation.action, otherAction);
        EXPECT_EQ(other.State(), Client::Tests::AfterDraws(seed, 1).State());
    }
    ActorState face = Monster(0xa9, 3);
    CrtRandom random(Seed);
    ASSERT_TRUE(Client::Monsters::FinishTheModelAction(face, skeleton, runtime, random).HasValue());
    EXPECT_EQ(face.animation.action, 3);
    ActorState named = Monster(0xc5, 3);
    ASSERT_TRUE(Client::Monsters::FinishTheModelAction(named, skeleton, runtime, random).HasValue());
    EXPECT_EQ(named.animation.action, 1);
    EXPECT_EQ(named.animation.frame, 4.0f);
    EXPECT_EQ(random.State(), Seed);
    runtime.map = 1;
    ActorState statue = Monster(0x28, 3);
    ASSERT_TRUE(Client::Monsters::FinishTheModelAction(statue, skeleton, runtime, random).HasValue());
    EXPECT_EQ(statue.animation.action, 0);
    EXPECT_EQ(random.State(), Seed);
}

// 0x42d890 on map 30, by +0x376: 221 and 222 to 0; 277 with +0x78 at 0x20000 to 2 and +0x42c at 0, with +0x78 at 0 to 0
// and +0x42c at 1; 286 and 287 from 5 to 4 with 0x7239d8 at 2, +0x110 at 1 and +0x100 at 0x12c. They return 1 and
// 0x4fcbb0 advances again; outside 30, nothing.
TEST(ModelBodyTest, TheCastleObjectsFinishByTheirKind)
{
    const Skeleton skeleton = EightActions();
    Client::Kernel::WorldRuntime runtime;
    runtime.map = 30;
    CrtRandom random(Seed);
    ActorState guard = Monster(0x1c0, 3);
    guard.word376 = 221;
    auto end = Client::Monsters::FinishTheModelAction(guard, skeleton, runtime, random);
    ASSERT_TRUE(end.HasValue()) << end.Error();
    EXPECT_EQ(end.Value(), ModelActionEnd::AdvanceAgain);
    EXPECT_EQ(guard.animation.action, 0);

    ActorState carried = Monster(0x1c0, 3);
    carried.word376 = 277;
    carried.flags78 = 0x20000;
    ASSERT_EQ(Client::Monsters::FinishTheModelAction(carried, skeleton, runtime, random).Value(),
              ModelActionEnd::AdvanceAgain);
    EXPECT_EQ(carried.animation.action, 2);
    EXPECT_EQ(carried.byte42c, 0);
    carried.flags78 = 0;
    ASSERT_EQ(Client::Monsters::FinishTheModelAction(carried, skeleton, runtime, random).Value(),
              ModelActionEnd::AdvanceAgain);
    EXPECT_EQ(carried.animation.action, 0);
    EXPECT_EQ(carried.byte42c, 1);

    ActorState gate = Monster(0x1c0, 5);
    gate.word376 = 286;
    gate.animation.previousAction = 1;
    gate.animation.previousKey = 7;
    ASSERT_EQ(Client::Monsters::FinishTheModelAction(gate, skeleton, runtime, random).Value(),
              ModelActionEnd::AdvanceAgain);
    EXPECT_EQ(gate.animation.action, 4);
    EXPECT_EQ(gate.animation.previousAction, 4);
    EXPECT_EQ(gate.animation.previousKey, 0);
    EXPECT_EQ(gate.animation.frame, 0.0f);
    EXPECT_EQ(runtime.castleSiege.castleByte7239d8, 2);
    EXPECT_EQ(gate.byte110, 1);
    EXPECT_EQ(gate.dword100, 0x12c);
    // Action 3 is not 0x42d890's: it goes on to the monsters, which send it back to 0.
    gate.word376 = 287;
    gate.animation.action = 3;
    ASSERT_EQ(Client::Monsters::FinishTheModelAction(gate, skeleton, runtime, random).Value(), ModelActionEnd::Done);
    EXPECT_EQ(gate.animation.action, 0);
    EXPECT_EQ(random.State(), Seed);

    runtime.map = 0;
    ActorState outside = Monster(0x1c0, 2);
    outside.word376 = 221;
    ASSERT_EQ(Client::Monsters::FinishTheModelAction(outside, skeleton, runtime, random).Value(), ModelActionEnd::Done);
    EXPECT_EQ(outside.animation.action, 2);
}

// 0x4ea190 and 0x4ea0e0: action 2 looks at the +0x3a4 character if it is within 90 degrees; with no target (−1) or
// another action the target stays at 0; 6 leaves it alone; 0 draws.
TEST(ModelBodyTest, TheHeadOfAnotherModelLooksAtItsTarget)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    auto pool = world->Actors();
    pool[5].position = {300.0f, 900.0f, 180.0f};
    ActorState actor = Monster(0x160, 2);
    actor.position = {100.0f, 200.0f, 30.0f};
    actor.attackTarget = 5;
    const auto heading = static_cast<float>(Client::Math::CreateAngle(100.0f, 200.0f, 300.0f, 900.0f));
    actor.angles.z = heading;
    CrtRandom random(Seed);
    ASSERT_TRUE(Client::Monsters::SetTheModelHeadTarget(actor, pool, random).HasValue());
    EXPECT_EQ(actor.headTurn.target.x, 0.0f);
    EXPECT_EQ(actor.headTurn.target.y, static_cast<float>((180.0 - (30.0 + 50.0)) * static_cast<double>(0.2f)));
    // Facing away: at 180 degrees, 0.
    actor.angles.z = heading >= 180.0f ? heading - 180.0f : heading + 180.0f;
    ASSERT_TRUE(Client::Monsters::SetTheModelHeadTarget(actor, pool, random).HasValue());
    EXPECT_EQ(actor.headTurn.target, (glm::vec2{0.0f, 0.0f}));
    // Below: the negative angle adds 360.
    actor.angles.z = heading;
    pool[5].position.z = 0.0f;
    ASSERT_TRUE(Client::Monsters::SetTheModelHeadTarget(actor, pool, random).HasValue());
    EXPECT_EQ(actor.headTurn.target.y, static_cast<float>((0.0 - 80.0) * static_cast<double>(0.2f)) + 360.0f);
    actor.attackTarget = 0xffff;
    ASSERT_TRUE(Client::Monsters::SetTheModelHeadTarget(actor, pool, random).HasValue());
    EXPECT_EQ(actor.headTurn.target, (glm::vec2{0.0f, 0.0f}));
    actor.attackTarget = 500;
    const auto outside = Client::Monsters::SetTheModelHeadTarget(actor, pool, random);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x4ea24c"), std::string::npos);
    actor.animation.action = 6;
    actor.headTurn.target = {7.0f, 8.0f};
    ASSERT_TRUE(Client::Monsters::SetTheModelHeadTarget(actor, pool, random).HasValue());
    EXPECT_EQ(actor.headTurn.target, (glm::vec2{7.0f, 8.0f}));
    actor.animation.action = 3;
    ASSERT_TRUE(Client::Monsters::SetTheModelHeadTarget(actor, pool, random).HasValue());
    EXPECT_EQ(actor.headTurn.target, (glm::vec2{0.0f, 0.0f}));
    EXPECT_EQ(random.State(), Seed);
    actor.animation.action = 0;
    ASSERT_TRUE(Client::Monsters::SetTheModelHeadTarget(actor, pool, random).HasValue());
    CrtRandom expected(Seed);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, expected.Next() % 32 == 0 ? 3 : 1).State());
}

// A Monster02 (model 0x153) loaded by 0x612d90 into Assets::AssetsState::fauna steps outside the pool: it advances with
// its action's speed (0.2 for action 1, 0x612e5a..0x612e9f) and, when 3 ends, goes back to 0 (0x4fcbb0), which then
// draws the head with 0x4ea190. The rand(): the two accesses of 0x508ba0, the body's % 6, the 0x505de0 cell, 0x4ea190
// (only with 0), the common head and the & 7.
class MonsterStep : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto model = Client::Tests::SampleModel();
        {
            const auto first = model.actions.front();
            model.actions.assign(8, first);
        }
        for (auto& bone : model.bones)
            if (!bone.dummy)
            {
                const auto first = bone.actions.front();
                bone.actions.assign(8, first);
            }
        model.meshes.front().textureName = "body.tga";
        for (const char* name : {"Monster/Monster01.bmd", "Monster/Monster02.bmd"})
            Client::Tests::WriteSceneFile(m_folder.Path(), name,
                                          Client::Tests::PlainBmd(Client::Tests::BmdContent(model), 0x0a));
        Client::Tests::WriteSceneFile(m_folder.Path(), "Monster/body.OZT", Client::Tests::PlainOzt(2, 2));
        m_terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        m_faunaModels.TextureTable().Bind(m_store);
        auto context = m_faunaModels.LoadContextOf(m_folder.Path(), m_log);
        ASSERT_TRUE(context.has_value());
        for (const std::int32_t number : {0, 1})
        {
            const auto loaded = m_faunaModels.LoadMonster(number, 0, *context);
            ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        }
        ASSERT_NE(m_faunaModels.SkeletonOf(Client::Models::ModelId{0x153}), nullptr);
        m_actor.alive = true;
        m_actor.type = 0x153;
        m_actor.byte88 = 2;
        m_actor.hands[0].model = 0xffff;
        m_actor.hands[1].model = 0xffff;
    }

    Client::Core::Expected<void, std::string> Step(CrtRandom& random)
    {
        return Client::Actors::StepCharacter(m_actor, {.world = *m_world,
                                                       .random = random,
                                                       .scene = Client::Kernel::SceneId::Game,
                                                       .terrain = &m_terrain,
                                                       .particles = &m_world->Particles(),
                                                       .groundMarks = &m_marks,
                                                       .dynamicLighting = &m_lighting,
                                                       .faunaModels = &m_faunaModels,
                                                       .hooks = &StepHooks,
                                                       .effectHooks = &EffectHooks});
    }

    const Skeleton& Model() const
    {
        return *m_faunaModels.SkeletonOf(Client::Models::ModelId{0x153});
    }

    Client::Tests::TemporaryFolder m_folder{"model-body"};
    Client::Tests::FakeTextureStore m_store;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    Client::Assets::FaunaModels m_faunaModels;
    std::unique_ptr<Client::Kernel::WorldState> m_world = std::make_unique<Client::Kernel::WorldState>();
    Client::Terrain::WorldTerrain m_terrain;
    Client::Terrain::GroundMarkTable m_marks{};
    Client::Terrain::TerrainLighting m_lighting;
    ActorState m_actor;
};

// What it draws after the 0x505de0 cell: the common head and the & 7.
void DrawTheHeadAndTheEffects(CrtRandom& random)
{
    Client::Kernel::CharacterHead head;
    Client::Kernel::StepCharacterHead(head, true, random);
    static_cast<void>(random.Next());
}

TEST_F(MonsterStep, AMonsterAdvancesWithTheSpeedOfItsModel)
{
    m_actor.animation.action = 1;
    m_actor.animation.frame = 0.0f;
    CrtRandom random(Seed);
    const auto stepped = Step(random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(m_actor.animation.frame, Model().actions[1].speed);
    EXPECT_EQ(Model().actions[1].speed, 0.2f);
    EXPECT_EQ(m_actor.headTurn.target, (glm::vec2{0.0f, 0.0f}));
    CrtRandom expected = Client::Tests::AfterDraws(Seed, 8 + 1 + 8);
    DrawTheHeadAndTheEffects(expected);
    EXPECT_EQ(random.State(), expected.State());
}

TEST_F(MonsterStep, AMonsterThatEndsItsAttackRests)
{
    const auto& action = Model().actions[3];
    m_actor.animation.action = 3;
    m_actor.animation.frame = static_cast<float>(action.keyCount) - 0.1f;
    CrtRandom random(Seed);
    const auto stepped = Step(random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(m_actor.animation.action, 0);
    EXPECT_EQ(m_actor.animation.frame, 0.0f);
    CrtRandom expected = Client::Tests::AfterDraws(Seed, 8 + 1 + 8);
    // 0x4ea190 with 0: rand() % 32 and, if it gives 0, two more.
    if (CrtRandom(expected).Next() % 32 == 0)
        Skip(expected, 3);
    else
        Skip(expected, 1);
    DrawTheHeadAndTheEffects(expected);
    EXPECT_EQ(random.State(), expected.State());
}

// What 0x52e810 draws for a blood mark, in order: x, y, the side, the turn and the life from 0x548fe0.
struct DrawnMark
{
    float size;
    float angle;
    std::int32_t life;
};

DrawnMark DrawBloodMark(CrtRandom& draws)
{
    Skip(draws, 2);
    constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    DrawnMark drawn{};
    drawn.size = static_cast<float>(static_cast<double>((draws.Next() & 3) + 8) * Tenth);
    drawn.angle = static_cast<float>(draws.Next() % 360);
    drawn.life = draws.Next() % 32 + 50;
    return drawn;
}

// In Lorencia, the Monster02 (0x153, bone +0x54 at 5) that ends action 6 leaves its remains once (0x4fcc3c → 0x52e810):
// +0x38a at 1 and two 0x5e3 marks with their light. Without own bones (before its first draw) they fall at the position
// of the shared model that 0x4fd69b already published from this actor; they do not keep another 0x153's origin
// (Q-C1178, revised). The rand(): those of 0x508ba0, the ten of the marks, the % 6, the 0x505de0 cell, the head and the
// & 7. The next step, with +0x38a at 1, leaves nothing.
TEST_F(MonsterStep, AMonsterThatEndsDyingBleedsOnceAtItsPublishedPosition)
{
    ActorState previous = m_actor;
    previous.position = {5000.0f, 6000.0f, 70.0f};
    previous.scale = 1.1f;
    m_world->PlaceModel(previous);
    m_actor.position = {7000.0f, 8000.0f, 90.0f};
    m_actor.scale = 1.2f;
    m_actor.light = {0.25f, 0.5f, 0.75f};
    m_actor.animation.action = 6;
    m_actor.animation.frame = static_cast<float>(Model().actions[6].keyCount) - 0.1f;
    CrtRandom random(Seed);
    const auto stepped = Step(random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(m_actor.alive);
    EXPECT_EQ(m_actor.byte38a, 1);
    const auto& marks = m_marks;
    CrtRandom draws = Client::Tests::AfterDraws(Seed, 8);
    for (std::size_t slot = 0; slot < 2; ++slot)
    {
        SCOPED_TRACE(slot);
        const DrawnMark drawn = DrawBloodMark(draws);
        ASSERT_TRUE(marks[slot].live);
        EXPECT_EQ(marks[slot].type, 0x5e3);
        EXPECT_EQ(marks[slot].position, m_actor.position);
        EXPECT_EQ(marks[slot].size, drawn.size);
        EXPECT_EQ(marks[slot].angle, drawn.angle);
        EXPECT_EQ(marks[slot].color, m_actor.light);
        EXPECT_EQ(marks[slot].life, drawn.life);
    }
    EXPECT_FALSE(marks[2].live);
    EXPECT_EQ(m_world->ModelPlacement(0x153).origin, m_actor.position);
    EXPECT_EQ(m_world->ModelPlacement(0x153).scale, 1.2f);
    CrtRandom expected = Client::Tests::AfterDraws(Seed, 8 + 10 + 1 + 8);
    DrawTheHeadAndTheEffects(expected);
    EXPECT_EQ(random.State(), expected.State());

    m_actor.animation.frame = static_cast<float>(Model().actions[6].keyCount) - 0.1f;
    CrtRandom again(Seed);
    ASSERT_TRUE(Step(again).HasValue());
    EXPECT_FALSE(marks[2].live);
    CrtRandom once = Client::Tests::AfterDraws(Seed, 8 + 1 + 8);
    DrawTheHeadAndTheEffects(once);
    EXPECT_EQ(again.State(), once.State());
}

// In Devias (map 2), the ice monster (0x161, Monster16) that finishes dying turns off its alive byte and drops ten 0xca
// shards in the map's 0x518cf0 table, five rand() each; the step goes on (0x508ea0 does not check the byte again) to
// its 0x505de0 case (0x507e63, the texture scroll, no rand()), after the & 7.
TEST_F(MonsterStep, TheIceMonsterOfDeviasBreaksIntoShards)
{
    m_world->Runtime().map = 2;
    auto model = Client::Tests::SampleModel();
    {
        const auto first = model.actions.front();
        model.actions.assign(8, first);
    }
    for (auto& bone : model.bones)
        if (!bone.dummy)
        {
            const auto first = bone.actions.front();
            bone.actions.assign(8, first);
        }
    model.meshes.front().textureName = "body.tga";
    Client::Tests::WriteSceneFile(m_folder.Path(), "Monster/Monster16.bmd",
                                  Client::Tests::PlainBmd(Client::Tests::BmdContent(model), 0x0a));
    auto context = m_faunaModels.LoadContextOf(m_folder.Path(), m_log);
    ASSERT_TRUE(context.has_value());
    const auto loaded = m_faunaModels.LoadMonster(15, 2, *context);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    const Skeleton* ice = m_faunaModels.SkeletonOf(Client::Models::ModelId{0x161});
    ASSERT_NE(ice, nullptr);
    EXPECT_EQ(ice->headBone, 0x13);
    m_actor.type = 0x161;
    m_actor.position = {22050.0f, 3050.0f, 120.0f};
    m_actor.animation.action = 6;
    m_actor.animation.frame = static_cast<float>(ice->actions[6].keyCount) - 0.1f;
    CrtRandom random(Seed);
    const auto stepped = Step(random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(m_actor.alive);
    EXPECT_EQ(m_actor.byte38a, 1);
    std::size_t shards = 0;
    for (const auto& effect : m_world->Map().effects.Effects())
        if (effect.alive && effect.type == 0xca)
        {
            ++shards;
            EXPECT_EQ(effect.transform.position.z, 170.0f);
        }
    EXPECT_EQ(shards, 10u);
    for (const auto& mark : m_marks)
        EXPECT_FALSE(mark.live);
    CrtRandom expected = Client::Tests::AfterDraws(Seed, 8 + 50 + 1 + 8);
    DrawTheHeadAndTheEffects(expected);
    EXPECT_EQ(random.State(), expected.State());
}

// An unloaded model (0x154) is the zeroed record of 0x5769ad8 (+0x26, no actions): 0x4f9580 does not advance, the
// rand() % 6 of 0x4fd7e4 is drawn anyway and 0x505de0 goes to 0x505e15, which builds the +0x13c..+0x168 box from the
// +0x124 one and the position and exits, without the cell or the & 7 (B3).
TEST_F(MonsterStep, AModelThatIsNotLoadedOnlyBoxesItself)
{
    for (const std::int32_t map : {0, 2, 7})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        m_actor.type = 0x154;
        m_actor.animation = {};
        m_actor.animation.action = 1;
        m_actor.animation.frame = 0.5f;
        m_actor.position = {1000.0f, 2000.0f, 30.0f};
        m_actor.boundsMin = {-60.0f, -60.0f, 0.0f};
        m_actor.boundsMaxX = 50.0f;
        m_actor.boundsMaxY = 70.0f;
        m_actor.boundsTop = 150.0f;
        m_actor.field14c = 9.0f;
        m_actor.field164 = 9.0f;
        CrtRandom random(Seed);
        const auto stepped = Step(random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(m_actor.animation.action, 1);
        EXPECT_EQ(m_actor.animation.frame, 0.5f);
        EXPECT_EQ(m_actor.field13c, 940.0f);
        EXPECT_EQ(m_actor.field140, 1940.0f);
        EXPECT_EQ(m_actor.field144, 30.0f);
        EXPECT_EQ(m_actor.field148, 110.0f);
        EXPECT_EQ(m_actor.field158, 130.0f);
        EXPECT_EQ(m_actor.field168, 150.0f);
        for (const float zero : {m_actor.field14c, m_actor.field150, m_actor.field154, m_actor.field15c,
                                 m_actor.field160, m_actor.field164})
            EXPECT_EQ(zero, 0.0f);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, 8 + 1).State());
    }
}

// Monster01 (0x152) has a case in 0x505de0 (0x5084b3): outside its breath's frame windows it draws only the & 7
// (Monsters::StepTheModelEffects).
TEST_F(MonsterStep, AModelWithItsOwnCaseOutsideItsWindowOnlyDrawsTheBrightness)
{
    m_actor.animation.action = 1;
    m_actor.type = 0x152;
    CrtRandom random(Seed);
    const auto stepped = Step(random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    CrtRandom expected = Client::Tests::AfterDraws(Seed, 8 + 1 + 8);
    DrawTheHeadAndTheEffects(expected);
    EXPECT_EQ(random.State(), expected.State());
}

// The map library behind AssetsState::fauna, with the 0x1f5 (an NPC) on Monster02's skeleton.
class TraderLibrary final : public Client::Models::IModelLibrary
{
public:
    explicit TraderLibrary(const Skeleton& skeleton, std::uint16_t model = 0x1f5) : m_skeleton(skeleton), m_model(model)
    {
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        return model.Value() == m_model ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }

private:
    const Skeleton& m_skeleton;
    std::uint16_t m_model;
};

// 0x4fce7a..0x4fced1: the 0x1f5 checks +0xa8 after 0x4ec200, which has just published it while advancing; on the last
// key of its action, rand() % 32 at 0 sets action 1 and otherwise 0 (0x4ea030); on another, nothing. Then it goes on to
// its model's case in 0x505de0 (0x508093): in 0, its light and its joints; in the others, nothing.
TEST_F(MonsterStep, TheTraderUsesTheKeyPublishedByTheAdvanceBeforeChoosingItsNextAction)
{
    TraderLibrary trader(Model());
    m_faunaModels.Bind(&trader);
    m_actor.type = 0x1f5;
    const std::int16_t last = static_cast<std::int16_t>(Model().actions[2].keyCount - 1);
    const auto stepFrom = [this](std::int16_t key, std::uint32_t seed, float frame)
    {
        m_actor.animation.action = 2;
        m_actor.animation.frame = frame;
        m_faunaModels.RecordPoseKey(0x1f5, key);
        CrtRandom random(seed);
        return Step(random);
    };

    const auto other = stepFrom(static_cast<std::int16_t>(last + 1), Seed, 0.0f);
    ASSERT_TRUE(other.HasValue()) << other.Error();
    EXPECT_EQ(m_actor.animation.action, 2);

    // An earlier draw in last does not trigger the gate: the current advance overwrites it with 0.
    ASSERT_TRUE(stepFrom(last, Seed, 0.0f).HasValue());
    EXPECT_EQ(m_actor.animation.action, 2);
    EXPECT_EQ(m_faunaModels.PoseKey(0x1f5), 0);
    const float beforeLast = static_cast<float>(last) - Model().actions[2].speed;
    int rests = 0;
    int others = 0;
    for (std::uint32_t seed = 1; seed < 300; ++seed)
    {
        SCOPED_TRACE(seed);
        const auto stepped = stepFrom(last, seed, beforeLast);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        if (m_actor.animation.action == 0)
            ++rests;
        else if (m_actor.animation.action == 1)
            ++others;
        else
            ADD_FAILURE() << static_cast<int>(m_actor.animation.action);
        EXPECT_EQ(m_actor.animation.frame, 0.0f);
    }
    EXPECT_GT(rests, others);
    EXPECT_GT(others, 0);
    m_faunaModels.Bind(nullptr);
}

// 0x4fd5d7: from frame 3, 0x432bd0 with the 0x1b0 in action 4 outside (5, 7) draws its rand() from 0x432bf1 and the two
// of each of its ten poses (Monsters::Drawing::StepTheSwingsOf1abTo1b4), between the body's % 6 and the 0x505de0 cell;
// then helper 0x431470 does nothing with the 0x1b0 in action 4. Monster02's skeleton with ten keys per action.
TEST_F(MonsterStep, TheSwingOfThe1b0DrawsItsRandomsBeforeItsEffects)
{
    Skeleton swinging = Model();
    for (auto& action : swinging.actions)
        action.keyCount = 10;
    for (auto& bone : swinging.bones)
        for (auto& keys : bone.actions)
            if (!keys.empty())
                keys.resize(10, keys.front());
    TraderLibrary library(swinging, 0x1b0);
    m_faunaModels.Bind(&library);
    m_actor.type = 0x1b0;
    m_actor.animation.action = 4;
    m_actor.animation.frame = 3.0f;
    CrtRandom random(Seed);
    const auto stepped = Step(random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(m_actor.animation.action, 4);
    ASSERT_GE(m_actor.animation.frame, 3.0f);
    ASSERT_LE(m_actor.animation.frame, 5.0f);
    CrtRandom expected = Client::Tests::AfterDraws(Seed, 8 + 1 + 1 + 20 + 8);
    DrawTheHeadAndTheEffects(expected);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(m_world->PoseCarry().models.count(0x1b0), 1u);
    m_faunaModels.Bind(nullptr);
}

// 0x4fd5c1: standing from frame 3, the 0x1a7 (Monster85, of types 294 and 302: the 31) leaves no trail in 0x441260
// (only in actions 3 and 4); it goes on with the rand() from 0x432bf1 and reaches 0x505de0, where no helper acts on it.
TEST_F(MonsterStep, AStanding1a7GoesPastItsTrailHelper)
{
    Skeleton standing = Model();
    for (auto& action : standing.actions)
        action.keyCount = 10;
    for (auto& bone : standing.bones)
        for (auto& keys : bone.actions)
            if (!keys.empty())
                keys.resize(10, keys.front());
    TraderLibrary library(standing, 0x1a7);
    m_faunaModels.Bind(&library);
    for (const std::int32_t map : {0, 2, 31})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        m_actor.type = 0x1a7;
        m_actor.animation.action = 0;
        m_actor.animation.frame = 3.0f;
        CrtRandom random(Seed);
        const auto stepped = Step(random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_EQ(m_actor.animation.action, 0);
        CrtRandom expected = Client::Tests::AfterDraws(Seed, 8 + 1 + 1 + 8);
        // 0x4ea190 with 0: rand() % 32 and, if it gives 0, two more.
        if (CrtRandom(expected).Next() % 32 == 0)
            Skip(expected, 3);
        else
            Skip(expected, 1);
        DrawTheHeadAndTheEffects(expected);
        EXPECT_EQ(random.State(), expected.State());
    }
    m_faunaModels.Bind(nullptr);
}
} // namespace
