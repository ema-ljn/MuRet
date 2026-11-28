#include "Network/Xor32Packet.h"
#include "Actors/ActorsState.h"
#include "Actors/CharacterStep.h"
#include "Session/CharacterStepHooks.h"

#include "Assets/PlayerSkeleton.h"
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/ModelFiles.h"
#include "Tables/PlayerBones.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"
#include "Render/ModelBatch.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Models/LogicalAccess.h"
#include "Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Kernel/ActorState.h"
#include "Kernel/CharacterHead.h"
#include "Actors/Drawing/CharacterCloth.h"
#include "Pets/DarkSpirit.h"
#include "Terrain/DynamicWorldLight.h"
#include "Kernel/DrawTailState.h"
#include "Assets/FaunaModels.h"
#include "FrontEnd/CharacterSlots.h"
#include "FrontEnd/LoginModels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Camera/SceneCameras.h"
#include "Kernel/SceneId.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Terrain/GroundMarks.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainLighting.h"
#include "Particles/Particles.h"
#include "Pathfinding/TerrainWalls.h"
#include "Terrain/WorldTerrain.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/Bytes.h"
#include "support/PlayerActors.h"
#include "support/SameBytes.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Actors::StepCharacters: 0x508f00. 06 § El paso de los personajes, § El paso de los personajes en la
// selección y el login, § El cuerpo de los personajes (0x4fd5f0), § Los efectos de los personajes (0x505de0) and
// § Caminar.
namespace
{
const Client::Session::CharacterStepHooks StepHooks{};
using Client::Actors::CharacterSeen;
using Client::Actors::CharacterStepContext;
using Client::Actors::StepCharacter;
using Client::Actors::StepCharacters;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::CharacterHead;
using Client::Kernel::SceneId;
using Client::Kernel::StepCharacterHead;
using Client::Kernel::WorldState;
using Client::Models::ActionInfo;
using Client::Models::Skeleton;
using Client::Terrain::TerrainPolygon;

constexpr std::uint32_t Seed = 0x2468ace1u;
// 0x508f00's two logical cell accesses (4 rand() per close).
constexpr int WallDraws = 8;
// 0x4fbe10 with the hero cell equal to the stored one: opens and closes X and Y, four rand() per close.
constexpr int HeroCellReads = 2 * Client::Models::SmallLogicalCloseRandoms;
// 0x4fc450 with the hero: the own record, which closes at 0 with the 3848 of 0x40b4d0.
constexpr int OwnRecordDraws = Client::Models::OwnRecordCloseRandoms;
// The rand() % 6 of 0x4fd7e4.
constexpr int BodyDraws = 1;
// 0x505de0 with the hero seen: the two cell accesses and the rand() & 7 of 0x506cac.
constexpr int SeenEffectsDraws = 8 + 1;
// The hero seen, idle, action not ended and before frame 3: without the rand() of 0x432bf1 (0x4fcee0).
constexpr int SeenHeroDraws = WallDraws + OwnRecordDraws + BodyDraws + SeenEffectsDraws;
// The rand() of 0x432bf1 from frame 3 (0x4fcee0), and the one of 0x4f2250's suffix when the action ends.
constexpr int TrailDraws = 1;
constexpr int StopDraws = 1;
// 0x508ba0's two accesses with a non-hero, X (0x412890) and Y (0x40b800).
constexpr int OtherCellDraws = 8;
// 0x505de0's two accesses with a seen non-hero (0x506362 and 0x506833).
constexpr int OtherSeenCellDraws = 8;
// The rand() & 7 of 0x506cac, after the head.
constexpr int EffectsRandomDraws = 1;
// One read of +0x38e (0x517630 or its inline copies): one rand() when it closes at 0.
constexpr int SkillReadDraws = 1;
// A skill without the joint pool: 0x2b with +0x381 at 2 and the hand's bone 0 in the model draws the point's three
// rand() and stops at the first 0x10f/2 joint (0x4f99fe), which this context does not provide. 0x2f (its 0x10f of
// 0x4f9e49) and 0x37's 0x621/1 are already created (test_attack_frame_skills).
constexpr std::uint8_t FrameSkill = 0x2b;
constexpr std::uint8_t FrameSkillStep = 2;
constexpr std::string_view FrameSkillCut = "0x4f99fe";
constexpr int FrameSkillDraws = 3;
// 0x5068fd: a non-hero's head draws when rand() % 32 is 0.
constexpr int HeadOneIn = 32;
// The selection (0x633f20, World55) and the login (0x6349a0, World56).
constexpr std::uint8_t SelectionMap = 54;
constexpr std::uint8_t LoginMap = 55;
// The login character's scepter in the right hand (LoginModels.cpp; case 0x620 of 0x509340).
constexpr std::uint16_t LoginScepter = 0x620;

// 0x634303 passes the loose object after the 400 records: it also steps with the window closed.
TEST(CharacterStepTest, TheSelectionPreviewRunsTheCommonBodyOutsideThePool)
{
    WorldState world;
    world.Runtime().map = 54;
    Client::Terrain::WorldTerrain terrain;
    terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    ActorState preview;
    preview.alive = true;
    preview.type = 0xa9;
    preview.byte88 = 0;
    preview.animation.action = 1;
    preview.animation.previousAction = 1;
    preview.hands[0].model = Client::Kernel::NoItemModel;
    preview.hands[1].model = Client::Kernel::NoItemModel;
    Skeleton face;
    face.actions.assign(2, ActionInfo{.repeats = true, .speed = 0.3f, .keyCount = 12});
    CrtRandom random(Seed);
    Client::Assets::FaunaModels faunaModels;

    const auto stepped = StepCharacter(preview, {.world = world,
                                                 .random = random,
                                                 .scene = SceneId::CharacterSelect,
                                                 .terrain = &terrain,
                                                 .newFaceSkeleton = &face,
                                                 .faunaModels = &faunaModels,
                                                 .hooks = &StepHooks});

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FLOAT_EQ(preview.animation.frame, 0.6f);
    EXPECT_EQ(preview.headTurn.target, (glm::vec2{0.0f, 0.0f}));
    // X/Y of 0x508ba0, the body's rand, Y/X of the effects, the common head, the effects' rand.
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, 19).State());
    EXPECT_EQ(terrain.attributes->cells[0] & Client::Pathfinding::CharacterWallBit, 0u);
    EXPECT_FALSE(world.Actors()[0].alive);
}

// 0x4ec200 picks the look-ahead by the global scene: being NewFace is not scene 4.
TEST(CharacterStepTest, ANewFaceInTheGameUsesTheGameAnimationEndGate)
{
    auto world = std::make_unique<WorldState>();
    Client::Terrain::WorldTerrain terrain;
    terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    ActorState preview;
    preview.alive = true;
    preview.type = 0xa9;
    preview.animation.action = preview.animation.previousAction = 1;
    preview.animation.frame = 2.0f;
    preview.firstFootstepSound = 1;
    preview.secondFootstepSound = 1;
    preview.hands[0].model = preview.hands[1].model = 0xffff;
    Skeleton face;
    face.actions.assign(2, ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 4});
    CrtRandom random(Seed);
    Client::Assets::FaunaModels faunaModels;

    const auto stepped = StepCharacter(preview, {.world = *world,
                                                 .random = random,
                                                 .scene = SceneId::Game,
                                                 .terrain = &terrain,
                                                 .newFaceSkeleton = &face,
                                                 .faunaModels = &faunaModels,
                                                 .hooks = &StepHooks});

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(preview.firstFootstepSound, 1);
    EXPECT_EQ(preview.secondFootstepSound, 1);
    EXPECT_EQ(preview.animation.frame, 2.0f);
}

TEST(CharacterStepTest, TheNewFaceHeadHasItsOwnDrawBeforeTheCommonHeadAndUsesModuloFortyEight)
{
    WorldState world;
    world.Runtime().map = 54;
    Client::Terrain::WorldTerrain terrain;
    terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    ActorState actor;
    actor.alive = true;
    actor.type = 0xa8;
    actor.animation.frame = 4.0f;
    actor.hands[0].model = Client::Kernel::NoItemModel;
    actor.hands[1].model = Client::Kernel::NoItemModel;
    Skeleton face;
    face.actions.assign(1, ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 100});
    constexpr std::uint32_t HeadSeed = 35;
    CrtRandom random(HeadSeed);
    Client::Assets::FaunaModels faunaModels;

    const auto stepped = StepCharacter(actor, {.world = world,
                                               .random = random,
                                               .scene = SceneId::CharacterSelect,
                                               .terrain = &terrain,
                                               .newFaceSkeleton = &face,
                                               .faunaModels = &faunaModels,
                                               .hooks = &StepHooks});

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    // Rand 19..22: 1536, 18843, 27303 and 16668; %48 gives Y23, while %32 would give Y351.
    EXPECT_EQ(actor.headTurn.target, (glm::vec2{323.0f, 23.0f}));
    EXPECT_FLOAT_EQ(actor.headTurn.angles.x, 352.6f);
    EXPECT_FLOAT_EQ(actor.headTurn.angles.y, 4.6f);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(HeadSeed, 23).State());
}

CrtRandom RandomAfter(int draws)
{
    CrtRandom random(Seed);
    for (int i = 0; i < draws; ++i)
    {
        static_cast<void>(random.Next());
    }
    return random;
}

void Skip(CrtRandom& random, int draws)
{
    for (int i = 0; i < draws; ++i)
    {
        static_cast<void>(random.Next());
    }
}

// What a seen, idle non-hero whose action has not ended draws after its TerrainWall: its 0x508ba0, its body (the
// rand() % 6), its 0x505de0's cell, its head (0x5068fd: rand() % 32 and, if 0, two more) and the rand() & 7. The
// head goes to `head` as StepCharacterHead moves it. Returns whether it drew.
bool DrawASeenOther(CrtRandom& random, CharacterHead& head)
{
    Skip(random, OtherCellDraws + BodyDraws + OtherSeenCellDraws);
    CrtRandom probe = random;
    const bool sorts = probe.Next() % HeadOneIn == 0;
    StepCharacterHead(head, true, random);
    Skip(random, EffectsRandomDraws);
    return sorts;
}

bool DrawASeenOther(CrtRandom& random)
{
    CharacterHead head;
    return DrawASeenOther(random, head);
}

glm::vec3 CellCentre(std::uint8_t x, std::uint8_t y)
{
    return {(static_cast<float>(x) * 100.0f) + 50.0f, (static_cast<float>(y) * 100.0f) + 50.0f, 0.0f};
}

// The game's trapezoid around `centre`, like the one 0x5fb570 leaves with the startup camera.
TerrainPolygon GamePolygon(std::uint8_t map, const glm::vec3& centre)
{
    return Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::Game, map, centre, -45.0f);
}

bool HasError(const Client::Core::Expected<void, std::string>& stepped, std::string_view address)
{
    return !stepped.HasValue() && stepped.Error().find(address) != std::string::npos;
}

std::string ErrorOf(const Client::Core::Expected<void, std::string>& stepped)
{
    return stepped.HasValue() ? std::string() : stepped.Error();
}

// Player.bmd with its 205 actions without keys, and `action` with `keys` keys at `speed` per step.
Skeleton SkeletonWith(std::uint8_t action, std::int16_t keys, float speed)
{
    Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    skeleton.actions[action] = ActionInfo{.repeats = true, .speed = speed, .keyCount = keys, .lockPositions = false};
    return skeleton;
}

// The hero alive in slot HeroSlot, standing (action 1 of 0x511290), with 0x511290's PK, and Player.bmd with its
// actions without keys: the body neither advances nor ends. The own record has the empty equipment the F3 03 reply
// leaves (0x4c47c8): 0x4fc450 does not ask for Item.bmd.
struct GameWorld
{
    explicit GameWorld(std::int32_t map = 0)
    {
        world.Runtime().map = map;
        world.Runtime().hero = HeroSlot;
        world.OwnCharacter().ClearEquipmentForEntry();
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        Hero() = Client::Tests::UnarmedPlayer(1);
        Hero().alive = true;
        Hero().byte88 = 1;
        Hero().pk = 3;
        Hero().animation.action = 1;
    }

    ActorState& Hero()
    {
        return world.Actors()[HeroSlot];
    }

    // Another player alive and standing in `slot`, with the logical cell at (x, y), +0x88 at 1 and 0x511290's PK.
    ActorState& Other(std::size_t slot, std::uint8_t x = 3, std::uint8_t y = 4)
    {
        ActorState& other = world.Actors()[slot];
        other = Client::Tests::UnarmedPlayer(2);
        other.alive = true;
        other.byte88 = 1;
        other.pk = 3;
        other.animation.action = 1;
        // Idle: 0x511290 leaves the +0x39a/b target equal to the logical cell, before the first D3.
        other.cellX = other.targetX = x;
        other.cellY = other.targetY = y;
        return other;
    }

    std::uint16_t& Cell(std::uint8_t x, std::uint8_t y)
    {
        return terrain.attributes->cells[Client::Data::Terrain::CellIndex(x, y)];
    }

    // Bone 0 of the model, with its keys: the hand of 0x2b (FrameSkill) reaches 0x4f9918's point.
    void GiveTheHandABone()
    {
        Client::Models::SkeletonBone bone;
        bone.actions.resize(skeleton.actions.size());
        for (auto& keys : bone.actions)
            keys.resize(10);
        skeleton.bones.push_back(std::move(bone));
    }

    void StandOnTheReportedCell(std::int32_t map)
    {
        world.Runtime().map = map;
        Hero().cellX = 5;
        Hero().cellY = 6;
        reportedCell.x = 5;
        reportedCell.y = 6;
        reportedCell.time = -10000.0f;
        Cell(5, 6) = 8;
    }

    // With `polygon` in the global the last 0x5fb570 leaves (Camera::CameraState::trapezoid).
    Client::Core::Expected<void, std::string> Step(const TerrainPolygon& polygon, SceneId scene = SceneId::Game)
    {
        trapezoid = polygon;
        return Step(scene);
    }

    Client::Core::Expected<void, std::string> Step(SceneId scene = SceneId::Game)
    {
        return StepCharacters(CharacterStepContext{.world = world,
                                                   .random = random,
                                                   .scene = scene,
                                                   .playerSkeleton = player,
                                                   .terrain = &terrain,
                                                   .groundMarks = &marks,
                                                   .trapezoid = &trapezoid,
                                                   .dynamicLighting = &light.Lighting(),
                                                   .faunaModels = &faunaModels,
                                                   .reportedCell = &reportedCell,
                                                   .hooks = &StepHooks});
    }

    static constexpr std::size_t HeroSlot = 42;
    // On the heap: WorldState is over 340 KB and some tests have three GameWorld alive, more than Windows' 1 MB stack.
    // Moving the GameWorld moves the pointer, and the reference stays on the same WorldState.
    std::unique_ptr<WorldState> ownedWorld = std::make_unique<WorldState>();
    WorldState& world = *ownedWorld;
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random{Seed};
    Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    const Skeleton* player = &skeleton;
    Client::Terrain::GroundMarkTable marks{};
    TerrainPolygon trapezoid{};
    Client::Terrain::DynamicWorldLight light;
    Client::Assets::FaunaModels faunaModels;
    Client::Actors::ReportedCell reportedCell{};
};

const TerrainPolygon Everything{.everything = true};

TEST(CharacterStepTest, SkillHitStunTurnsByItsOldCounterWithoutWrapping)
{
    for (const std::uint8_t counter : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{10}, std::uint8_t{255}})
    {
        SCOPED_TRACE(int{counter});
        GameWorld tested;
        tested.Hero().byte398 = counter;
        tested.Hero().angles.z = 350.5f;
        const auto stepped = tested.Step(Everything);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested.Hero().byte398, counter == 0 ? 0 : counter - 1);
        EXPECT_EQ(tested.Hero().angles.z, 350.5f + static_cast<float>(counter) * 10.0f);
        EXPECT_EQ(tested.random.State(), Client::Tests::AfterDraws(Seed, SeenHeroDraws).State());
    }
}

TEST(CharacterStepTest, FullJointsKeepTheBoneEmitterSelectorsAndDecrementItsByte)
{
    for (const std::int32_t map : {0, 30})
    {
        SCOPED_TRACE(map);
        GameWorld tested(map);
        tested.Hero().byte1a0 = 35;
        tested.skeleton.bones.resize(5);
        tested.skeleton.bones[0].parent = 2; // The parent can come later.
        tested.skeleton.bones[1].dummy = true;
        tested.skeleton.bones[1].parent = 0;
        tested.skeleton.bones[2].parent = -1;
        tested.skeleton.bones[3].parent = 5; // Outside the model.
        tested.skeleton.bones[4].parent = 0;
        tested.trapezoid = Everything;
        for (auto& joint : tested.world.DrawTail().joints)
            joint.alive = true;
        const auto stepped = StepCharacters({.world = tested.world,
                                             .random = tested.random,
                                             .playerSkeleton = &tested.skeleton,
                                             .terrain = &tested.terrain,
                                             .joints = tested.world.DrawTail().joints,
                                             .trapezoid = &tested.trapezoid,
                                             .dynamicLighting = &tested.light.Lighting(),
                                             .faunaModels = &tested.faunaModels,
                                             .hooks = &StepHooks});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested.Hero().byte1a0, map == 30 ? 35 : 34);
        EXPECT_EQ(tested.random.State(), Client::Tests::AfterDraws(Seed, SeenHeroDraws + (map == 30 ? 0 : 12)).State());
    }
}

TEST(CharacterStepTest, TheFlareSkillActionUsesTheExistingParticleAndSkipsItsFullPool)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        GameWorld tested;
        tested.Hero().animation.action = 0x4d;
        tested.Hero().animation.frame = 6.0f;
        tested.Hero().animation.previousAction = 0x4d;
        tested.trapezoid = Everything;
        if (full)
            for (auto& particle : tested.world.Particles().All())
                particle.alive = true;
        const auto stepped = StepCharacters({.world = tested.world,
                                             .random = tested.random,
                                             .playerSkeleton = &tested.skeleton,
                                             .terrain = &tested.terrain,
                                             .particles = &tested.world.Particles(),
                                             .trapezoid = &tested.trapezoid,
                                             .dynamicLighting = &tested.light.Lighting(),
                                             .faunaModels = &tested.faunaModels,
                                             .hooks = &StepHooks});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested.random.State(),
                  Client::Tests::AfterDraws(Seed, SeenHeroDraws + TrailDraws + (full ? 0 : 3)).State());
        if (!full)
        {
            const auto& particle = tested.world.Particles().All().front();
            EXPECT_TRUE(particle.alive);
            EXPECT_EQ(particle.type, 0x5ac);
            EXPECT_EQ(particle.subtype, 0);
            EXPECT_EQ(particle.light, (glm::vec3{0.3f, 0.5f, 1.0f}));
        }
    }
}

TEST(CharacterStepTest, ChargedSkillFlaresAndTheFullJointTailUseTheirNativeBudgets)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        GameWorld tested;
        tested.Hero().animation.action = 0x44;
        tested.Hero().animation.previousAction = 0x44;
        tested.Hero().byte1bc = 1;
        tested.skeleton.bones.resize(5);
        tested.skeleton.bones[2].dummy = true;
        tested.trapezoid = Everything;
        for (auto& joint : tested.world.DrawTail().joints)
            joint.alive = true;
        if (full)
            for (auto& particle : tested.world.Particles().All())
                particle.alive = true;
        const auto stepped = StepCharacters({.world = tested.world,
                                             .random = tested.random,
                                             .playerSkeleton = &tested.skeleton,
                                             .terrain = &tested.terrain,
                                             .particles = &tested.world.Particles(),
                                             .joints = tested.world.DrawTail().joints,
                                             .trapezoid = &tested.trapezoid,
                                             .dynamicLighting = &tested.light.Lighting(),
                                             .faunaModels = &tested.faunaModels,
                                             .hooks = &StepHooks});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested.random.State(), Client::Tests::AfterDraws(Seed, SeenHeroDraws + 6 + (full ? 0 : 12)).State());
        if (!full)
        {
            const auto& all = tested.world.Particles().All();
            EXPECT_EQ(std::ranges::count_if(all, [](const auto& p) { return p.alive; }), 4);
            EXPECT_EQ(all.front().type, 0x5ac);
            EXPECT_EQ(all.front().subtype, 6);
            EXPECT_EQ(all.front().light, (glm::vec3{0.3f, 0.3f, 1.0f}));
        }
    }
}

// 0x506f5a → 0x5189f0: action 0x44 or 0x45 (no bones, no 0x5ac/6 sparks) releases three 0x619/8 into the first
// three slots of the joint table, each with three rand(): the two of the angles (rand() % 90, 0, rand() % 360) and the
// creation light's (rand() & 0x7f). The first point is the position minus (0, −500, 0) rotated by the angles
// (0x5fc8f0 and 0x5fcbd0) and 120 higher; the target, the position with z + 100. The expected values are literals,
// computed outside the port with the seed's rand() after SeenHeroDraws.
TEST(CharacterStepTest, TheChargedSkillReleasesThreeEnergyJointsWithTheirRandoms)
{
    struct Expected
    {
        glm::vec3 angles;
        glm::vec3 first;
        std::uint32_t blue;
    };
    const std::array<Expected, 3> expected{
        Expected{
            {8.0f, 0.0f, 71.0f},
            {std::bit_cast<float>(0x4404f5ddu), std::bit_cast<float>(0x45071333u), std::bit_cast<float>(0x43f4cb14u)},
            0x3f0e0e0eu},
        Expected{
            {7.0f, 0.0f, 3.0f},
            {std::bit_cast<float>(0x447381bcu), std::bit_cast<float>(0x451bf97du), std::bit_cast<float>(0x43f077a3u)},
            0x3f373737u},
        Expected{
            {31.0f, 0.0f, 187.0f},
            {std::bit_cast<float>(0x44838766u), std::bit_cast<float>(0x44c4d38du), std::bit_cast<float>(0x44296138u)},
            0x3f2d2d2du}};
    for (const std::uint8_t action : {std::uint8_t{0x44}, std::uint8_t{0x45}})
    {
        SCOPED_TRACE(static_cast<int>(action));
        GameWorld tested;
        ActorState& hero = tested.Hero();
        hero.animation.action = action;
        hero.animation.previousAction = action;
        hero.position = {1000.0f, 2000.0f, 300.0f};
        tested.trapezoid = Everything;
        const auto stepped = StepCharacters({.world = tested.world,
                                             .random = tested.random,
                                             .playerSkeleton = &tested.skeleton,
                                             .terrain = &tested.terrain,
                                             .joints = tested.world.DrawTail().joints,
                                             .trapezoid = &tested.trapezoid,
                                             .dynamicLighting = &tested.light.Lighting(),
                                             .faunaModels = &tested.faunaModels,
                                             .hooks = &StepHooks});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested.random.State(), Client::Tests::AfterDraws(Seed, SeenHeroDraws + 9).State());
        const auto& joints = tested.world.DrawTail().joints;
        EXPECT_EQ(std::ranges::count_if(joints, [](const auto& joint) { return joint.alive; }), 3);
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            SCOPED_TRACE(index);
            const auto& joint = joints[index];
            EXPECT_TRUE(joint.alive);
            EXPECT_EQ(joint.type, 0x619);
            EXPECT_EQ(joint.subtype, 8);
            EXPECT_EQ(joint.ownerContext, &hero);
            EXPECT_EQ(joint.width, 10.0f);
            EXPECT_EQ(joint.key, -1);
            EXPECT_EQ(joint.life, 17);
            EXPECT_EQ(joint.maximumCount, 3);
            EXPECT_EQ(joint.angles, expected[index].angles);
            EXPECT_EQ(joint.position, expected[index].first);
            EXPECT_EQ(joint.light, glm::vec3(0.5f, 0.5f, std::bit_cast<float>(expected[index].blue)));
            EXPECT_EQ(joint.targetPosition, glm::vec3(1000.0f, 2000.0f, 400.0f));
        }
    }
}

TEST(CharacterStepTest, Action96CreatesTenFiresAndFullParticlesKeepItsBoneChoices)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        GameWorld tested;
        tested.Hero().animation.action = 0x96;
        tested.Hero().animation.previousAction = 0x96;
        tested.skeleton.bones.resize(3);
        tested.trapezoid = Everything;
        if (full)
            for (auto& particle : tested.world.Particles().All())
                particle.alive = true;
        const auto stepped = StepCharacters({.world = tested.world,
                                             .random = tested.random,
                                             .playerSkeleton = &tested.skeleton,
                                             .terrain = &tested.terrain,
                                             .particles = &tested.world.Particles(),
                                             .trapezoid = &tested.trapezoid,
                                             .dynamicLighting = &tested.light.Lighting(),
                                             .faunaModels = &tested.faunaModels,
                                             .hooks = &StepHooks});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        // 0x506f7a selector and 0x541189/0x5411ce/0x5413ef, three from the 0x5d9/0 constructor if there is room.
        EXPECT_EQ(tested.random.State(), Client::Tests::AfterDraws(Seed, SeenHeroDraws + 10 * (full ? 1 : 4)).State());
        if (!full)
        {
            const auto& all = tested.world.Particles().All();
            EXPECT_EQ(std::ranges::count_if(all, [](const auto& p) { return p.alive; }), 10);
            EXPECT_EQ(all.front().type, 0x5d9);
            EXPECT_EQ(all.front().light, (glm::vec3{1.0f, 1.0f, 1.0f}));
        }
    }
}

TEST(CharacterStepTest, TheSceneTerrainSuppliesItsWallsAndSafeZone)
{
    GameWorld tested;
    tested.Hero().alive = false;
    ActorState& actor = tested.Other(2);
    tested.trapezoid = Everything;
    tested.terrain.attributes.reset();
    Client::Terrain::WorldTerrain sceneTerrain;
    sceneTerrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    const auto cell = Client::Data::Terrain::CellIndex(3, 4);
    sceneTerrain.attributes->cells[cell] = 1;

    const auto stepped = StepCharacters({.world = tested.world,
                                         .random = tested.random,
                                         .scene = SceneId::Login,
                                         .playerSkeleton = tested.player,
                                         .terrain = &sceneTerrain,
                                         .trapezoid = &tested.trapezoid,
                                         .dynamicLighting = &tested.light.Lighting(),
                                         .faunaModels = &tested.faunaModels,
                                         .hooks = &StepHooks});

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(actor.safeZone, 1);
    EXPECT_NE(sceneTerrain.attributes->cells[cell] & Client::Pathfinding::CharacterWallBit, 0);
    EXPECT_FALSE(tested.terrain.attributes);
}

TEST(CharacterStepTest, TheRavenRunsInsideTheBodyBeforeTheCellAndHead)
{
    GameWorld tested(LoginMap);
    tested.Hero().alive = false;
    ActorState& actor = tested.Other(2, 0, 0);
    actor.position = {10603.0f, 14112.0f, 580.0f};
    actor.darkSpirit.emplace();
    actor.darkSpirit->position = {50.0f, 50.0f, 300.0f};
    tested.trapezoid = Everything;
    Skeleton raven;
    raven.actions.resize(4);
    Client::Particles::Particles particles;

    const auto stepped = StepCharacters({.world = tested.world,
                                         .random = tested.random,
                                         .scene = SceneId::Login,
                                         .playerSkeleton = tested.player,
                                         .terrain = &tested.terrain,
                                         .darkSpiritSkeleton = &raven,
                                         .particles = &particles,
                                         .trapezoid = &tested.trapezoid,
                                         .dynamicLighting = &tested.light.Lighting(),
                                         .faunaModels = &tested.faunaModels,
                                         .hooks = &StepHooks});

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    // 8 walls + 8 of the other + 1 body + 7 raven + 8 cell + 1 head + 1 suffix.
    EXPECT_EQ(tested.random.State(), RandomAfter(34).State());
    EXPECT_FLOAT_EQ(actor.darkSpirit->direction.y, -7.55f);
    EXPECT_FLOAT_EQ(actor.darkSpirit->direction.z, 2.9f);
    EXPECT_EQ(actor.headTurn.target, glm::vec2(0.0f));
}

// 0x4fd69b..0x4fd6ef publishes the placement before 0x4fb9e0 sinks the owner; 0x420dd0 consumes that snapshot.

TEST(CharacterStepTest, ThePerchedRavenUsesThePlacementPublishedByItsOwner)
{
    GameWorld tested;
    tested.Hero().alive = false;
    ActorState& first = tested.Other(2, 0, 0);
    first.position = {1000.0f, 2000.0f, 300.0f};
    first.scale = 1.5f;
    ActorState& owner = tested.Other(5, 0, 0);
    owner.position = {10603.0f, 14112.0f, 580.0f};
    owner.scale = 1.25f;
    owner.angles = {3.0f, 0.0f, 188.0f};
    owner.safeZone = 1;
    tested.Cell(0, 0) = 1;
    owner.bodyPose.assign(60, Client::Math::BoneTransform(1.0f));
    owner.bodyPose[37][3] = glm::vec3(7.0f, 8.0f, 9.0f);
    owner.darkSpirit.emplace();
    owner.darkSpirit->state = 3;
    Skeleton raven;
    raven.actions.resize(4);
    Client::Particles::Particles particles;
    tested.trapezoid = Everything;

    const auto stepped = StepCharacters({.world = tested.world,
                                         .random = tested.random,
                                         .playerSkeleton = tested.player,
                                         .terrain = &tested.terrain,
                                         .darkSpiritSkeleton = &raven,
                                         .particles = &particles,
                                         .trapezoid = &tested.trapezoid,
                                         .dynamicLighting = &tested.light.Lighting(),
                                         .faunaModels = &tested.faunaModels,
                                         .hooks = &StepHooks});

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(owner.darkSpirit->state, 3);
    // 0x4fd69b publishes the owner before 0x420dd0: 1.25 · ((7, 8, 9) + (−10, 0, 10)) + its position.
    EXPECT_EQ(owner.darkSpirit->position, glm::vec3(10599.25f, 14122.0f, 603.75f));
    EXPECT_EQ(owner.darkSpirit->angles, glm::vec3(3.0f, 0.0f, 68.0f));
    // The step's last 0x20a leaves its own for the next one.
    EXPECT_EQ(tested.world.PlayerModel().scale, 1.25f);
    EXPECT_EQ(tested.world.PlayerModel().origin, owner.position);
}

// 0x416a88..0x4176e6: in states 2, 5 and 6, with the +0x3b0 character hidden (0x4000), the raven sends the A7 that
// stops the attack (C1 07 A7 00 78 FF FF, with the XOR32) and leaves +0x3b0 at 0xffff. Without 0x4000, it sends
// nothing.

TEST(CharacterStepTest, AHiddenRavenTargetSendsTheA7ThatStopsTheAttack)
{
    for (const std::int32_t map : {0, 2, std::int32_t{LoginMap}})
    {
        for (const std::uint8_t state : {std::uint8_t{2}, std::uint8_t{5}, std::uint8_t{6}})
        {
            for (const std::uint32_t flags : {0u, 0x4000u})
            {
                SCOPED_TRACE(testing::Message() << map << " " << int{state} << " " << flags);
                GameWorld tested(map);
                tested.Hero().alive = false;
                ActorState& actor = tested.Other(2, 0, 0);
                ActorState& target = tested.Other(7, 1, 1);
                target.flags78 = flags;
                target.position = {300.0f, 400.0f, 0.0f};
                actor.darkSpirit.emplace();
                actor.darkSpirit->state = state;
                actor.darkSpirit->target3b0 = 7;
                Skeleton raven;
                raven.actions.resize(4);
                Client::Particles::Particles particles;
                tested.trapezoid = Everything;
                std::vector<Client::Protocol::Outgoing> sent;
                const auto stepped = StepCharacters(
                    {.world = tested.world,
                     .random = tested.random,
                     .scene = SceneId::Login,
                     .playerSkeleton = tested.player,
                     .terrain = &tested.terrain,
                     .darkSpiritSkeleton = &raven,
                     .particles = &particles,
                     .send = [&sent](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); },
                     .trapezoid = &tested.trapezoid,
                     .dynamicLighting = &tested.light.Lighting(),
                     .faunaModels = &tested.faunaModels,
                     .hooks = &StepHooks});
                ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
                if (flags == 0)
                {
                    EXPECT_TRUE(sent.empty());
                    EXPECT_EQ(actor.darkSpirit->target3b0, 7);
                    continue;
                }
                ASSERT_EQ(sent.size(), 1u);
                EXPECT_EQ(sent[0].delivery, Client::Network::Delivery::Clear);
                EXPECT_EQ(Client::Network::Xor32Decode(sent[0].packet).Bytes().size(), 7u);
                EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Xor32Decode(sent[0].packet).Bytes(),
                                                     Client::Tests::Bytes({0xc1, 7, 0xa7, 0, 0x78, 0xff, 0xff})));
                EXPECT_EQ(actor.darkSpirit->target3b0, 0xffff);
            }
        }
    }
}

// 0x416a8b: +0x3b0 between 0 and 399 looks at that character's flags; outside (the 400 of a missing key), only the
// owner's.

TEST(CharacterStepTest, TheRavenLooksAtTheAttackTargetOnlyInsideTheFourHundred)
{
    GameWorld tested(LoginMap);
    tested.Hero().alive = false;
    ActorState& actor = tested.Other(2, 0, 0);
    tested.world.Actors()[400].flags78 = 0x4000;
    actor.darkSpirit.emplace();
    actor.darkSpirit->state = 6;
    actor.darkSpirit->target3b0 = 400;
    Skeleton raven;
    raven.actions.resize(4);
    Client::Particles::Particles particles;
    tested.trapezoid = Everything;
    std::vector<Client::Protocol::Outgoing> sent;
    const auto stepped =
        StepCharacters({.world = tested.world,
                        .random = tested.random,
                        .scene = SceneId::Login,
                        .playerSkeleton = tested.player,
                        .terrain = &tested.terrain,
                        .darkSpiritSkeleton = &raven,
                        .particles = &particles,
                        .send = [&sent](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); },
                        .trapezoid = &tested.trapezoid,
                        .dynamicLighting = &tested.light.Lighting(),
                        .faunaModels = &tested.faunaModels,
                        .hooks = &StepHooks});
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(actor.darkSpirit->state, 6);

    actor.flags78 = 0x4000;
    const auto hidden =
        StepCharacters({.world = tested.world,
                        .random = tested.random,
                        .scene = SceneId::Login,
                        .playerSkeleton = tested.player,
                        .terrain = &tested.terrain,
                        .darkSpiritSkeleton = &raven,
                        .particles = &particles,
                        .send = [&sent](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); },
                        .trapezoid = &tested.trapezoid,
                        .dynamicLighting = &tested.light.Lighting(),
                        .faunaModels = &tested.faunaModels,
                        .hooks = &StepHooks});
    ASSERT_TRUE(hidden.HasValue()) << hidden.Error();
    EXPECT_EQ(sent.size(), 1u);
}

TEST(CharacterStepTest, AMissingRavenResourceCutsAfterTheBodyRandoms)
{
    GameWorld tested(LoginMap);
    tested.Hero().alive = false;
    ActorState& actor = tested.Other(2, 0, 0);
    actor.darkSpirit.emplace();
    const auto stepped = tested.Step(Everything, SceneId::Login);
    EXPECT_TRUE(HasError(stepped, "0x416a20"));
    EXPECT_EQ(tested.random.State(), RandomAfter(17).State());
    EXPECT_EQ(actor.headTurn.target, glm::vec2(0.0f));
}

class GoldenCharacterStep : public Client::Tests::GoldenDataTest
{
protected:
    std::optional<Client::Tables::LoadedItemTable> LoadItems() const
    {
        const auto path = DataFile("Local\\Item.bmd");
        if (!path)
        {
            return std::nullopt;
        }
        const std::optional<std::vector<char>> file = Client::Tests::ReadFileBytes(*path);
        if (!file)
        {
            return std::nullopt;
        }
        auto loaded = Client::Tables::LoadItemTable(std::as_bytes(std::span(*file)));
        if (!loaded.HasValue())
        {
            return std::nullopt;
        }
        return std::move(loaded).Value();
    }
};

// Player.bmd from the repo's Data\, with its 205 actions (P-C14 a).
std::optional<Skeleton> LoadPlayerSkeleton(const std::filesystem::path& dataRoot)
{
    for (const Client::Data::Models::ModelFile& file : Client::Tables::PlayerModelFiles())
    {
        if (file.name != Client::Data::Models::PlayerSkeletonName)
        {
            continue;
        }
        auto loaded = Client::Data::Models::LoadModelFile(dataRoot, file);
        if (!loaded.HasValue())
        {
            return std::nullopt;
        }
        auto skeleton = Client::Assets::MakePlayerSkeleton(loaded.Value());
        if (!skeleton.HasValue())
        {
            return std::nullopt;
        }
        return std::move(skeleton).Value();
    }
    return std::nullopt;
}

// Milestone 1 plan, T11, and observation 8: crossing the town border, +0x3e2 follows the logical cell (+0x41c,
// +0x420), the one a step ahead, not the drawn one. With the hero seen: the TerrainWall's 8 rand(), the own record's
// 3848, 0x4fd5f0's rand() % 6, 0x505de0's 8 for the cell and the rand() & 7.

TEST_F(GoldenCharacterStep, TheSafeZoneFollowsTheLogicalCellAhead)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t insideX;
        std::uint8_t insideY;
        std::uint8_t outsideX;
        std::uint8_t outsideY;
    };
    for (const Case& border : {Case{0, 172, 128, 173, 128}, Case{2, 220, 76, 220, 77}})
    {
        struct Walk
        {
            bool leaving;
            std::uint8_t drawnX;
            std::uint8_t drawnY;
            std::uint8_t logicalX;
            std::uint8_t logicalY;
            std::uint8_t safeZone;
        };
        for (const Walk& walk : {Walk{true, border.insideX, border.insideY, border.outsideX, border.outsideY, 0},
                                 Walk{false, border.outsideX, border.outsideY, border.insideX, border.insideY, 1}})
        {
            SCOPED_TRACE(::testing::Message()
                         << "mapa " << static_cast<int>(border.map) << (walk.leaving ? ", sale" : ", entra"));
            GameWorld tested(border.map);
            auto loaded = Client::Terrain::LoadWorldTerrain(m_dataRoot, border.map);
            ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
            tested.terrain = std::move(loaded).Value();
            ASSERT_EQ(tested.Cell(border.insideX, border.insideY) & 1, 1);
            ASSERT_EQ(tested.Cell(border.outsideX, border.outsideY) & 1, 0);
            ActorState& hero = tested.Hero();
            hero.position = CellCentre(walk.drawnX, walk.drawnY);
            hero.cellX = walk.logicalX;
            hero.cellY = walk.logicalY;
            // What 0x511290 left with the drawn cell.
            hero.safeZone = static_cast<std::uint8_t>(1 - walk.safeZone);
            const TerrainPolygon polygon = GamePolygon(border.map, hero.position);
            ASSERT_TRUE(CharacterSeen(polygon, hero.position));

            const auto stepped = tested.Step(polygon);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_TRUE(hero.seen);
            EXPECT_EQ(hero.safeZone, walk.safeZone);
            EXPECT_EQ(tested.Cell(walk.logicalX, walk.logicalY) & Client::Pathfinding::CharacterWallBit,
                      Client::Pathfinding::CharacterWallBit);
            EXPECT_EQ(tested.Cell(walk.drawnX, walk.drawnY) & Client::Pathfinding::CharacterWallBit, 0);
            EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
        }
    }
}

// What the idle hero in the safe zone draws per step with the repo's Player.bmd: action 1 (6 keys, 0.28 per step,
// repeating) of an unarmed player. Per step, the TerrainWall's 8, the own record's 3848 (0x4fc450), the rand() % 6
// (0x4fd7e4), and 0x505de0's 8 for the cell and rand() & 7: 3866. From step 11 the frame passes 3 and 0x432bf1 draws
// one more; at 22 the action ends (6.16), goes back to frame 0.16 and 0x4f2250 draws its suffix's, without 0x432bf1's.

TEST_F(GoldenCharacterStep, TheHeroAtRestDrawsTheSameBudgetOnLorenciaAndDevias)
{
    const std::optional<Skeleton> player = LoadPlayerSkeleton(m_dataRoot);
    ASSERT_TRUE(player.has_value());
    ASSERT_EQ(player->actions[1].keyCount, 6);
    ASSERT_EQ(player->actions[1].speed, 0.28f);
    struct Case
    {
        std::uint8_t map;
        std::uint8_t x;
        std::uint8_t y;
    };
    for (const Case& place : {Case{0, 172, 128}, Case{2, 220, 76}})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << static_cast<int>(place.map));
        GameWorld tested(place.map);
        tested.player = &*player;
        auto loaded = Client::Terrain::LoadWorldTerrain(m_dataRoot, place.map);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        tested.terrain = std::move(loaded).Value();
        ASSERT_EQ(tested.Cell(place.x, place.y) & 1, 1);
        ActorState& hero = tested.Hero();
        hero.position = CellCentre(place.x, place.y);
        hero.cellX = place.x;
        hero.cellY = place.y;
        hero.safeZone = 1;
        hero.run = 1;
        const TerrainPolygon polygon = GamePolygon(place.map, hero.position);
        CrtRandom expected(Seed);
        for (int step = 1; step <= 22; ++step)
        {
            SCOPED_TRACE(::testing::Message() << "paso " << step);
            const int draws = SeenHeroDraws + (step >= 11 ? TrailDraws : 0);
            for (int i = 0; i < draws; ++i)
            {
                static_cast<void>(expected.Next());
            }

            const auto stepped = tested.Step(polygon);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            ASSERT_EQ(tested.random.State(), expected.State());
            EXPECT_EQ(hero.animation.action, 1);
            EXPECT_EQ(hero.run, step < 22 ? 1 : 0) << "0x4f2250 pone Run en 0 al terminar";
        }
        EXPECT_LT(hero.animation.frame, 1.0f);
        EXPECT_EQ(hero.safeZone, 1);
    }
}

// 0x505ef2: unseen, 0x505de0 neither reads the cell nor touches +0x3e2; the TerrainWall's 8 rand() and the body's,
// which runs the same, remain.

TEST(CharacterStepTest, AnUnseenHeroKeepsItsSafeZone)
{
    GameWorld tested;
    ActorState& hero = tested.Hero();
    hero.position = CellCentre(100, 100);
    hero.cellX = 100;
    hero.cellY = 100;
    hero.safeZone = 1;
    hero.seen = true;
    tested.Cell(100, 100) = 0;
    const TerrainPolygon polygon = GamePolygon(0, CellCentre(200, 200));
    ASSERT_FALSE(CharacterSeen(polygon, hero.position));

    const auto stepped = tested.Step(polygon);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(hero.seen);
    EXPECT_EQ(hero.safeZone, 1);
    EXPECT_EQ(tested.Cell(100, 100), Client::Pathfinding::CharacterWallBit);
    EXPECT_EQ(tested.random.State(), RandomAfter(WallDraws + OwnRecordDraws + BodyDraws).State());
}

// 0x505de0 looks at bit 0x01 of the WORD's low byte: the hero's 0x02 and the high bits do not count.

TEST(CharacterStepTest, TheSafeZoneIsTheLowBitOfTheCell)
{
    struct Case
    {
        std::uint16_t cell;
        std::uint8_t safeZone;
    };
    for (const Case& probe : {Case{0x0001, 1}, Case{0x0005, 1}, Case{0x0100, 0}, Case{0x0102, 0}, Case{0x0004, 0}})
    {
        SCOPED_TRACE(probe.cell);
        GameWorld tested;
        ActorState& hero = tested.Hero();
        hero.cellX = 7;
        hero.cellY = 9;
        hero.safeZone = static_cast<std::uint8_t>(1 - probe.safeZone);
        tested.Cell(7, 9) = probe.cell;

        ASSERT_TRUE(tested.Step(Everything).HasValue());

        EXPECT_EQ(hero.safeZone, probe.safeZone);
    }
}

// 0x508f14..0x5091a0: 0x02 is cleared from the whole grid and set again on the logical cell (the low eight bits) of
// each live character, not dying (+0x389) and without the 8 of +0x88, with its two accesses; whether it is seen, for
// the first 400, alive or not; not for the 401st. Then the first live non-hero, the dying one, leaves 0x508ba0 without
// reading its cell (0x508bb6) and, after its dying step (0x4fb9e0, no rand() outside map 7), its model, 0, is
// 0x5769ad8's record without actions: it does not advance (0x4f9580), draws the rand() % 6, and 0x505de0 builds its box
// and leaves (0x505e15). The other two, without a model, read their cell in 0x508ba0 and do the same; the hero takes
// its whole step.

TEST(CharacterStepTest, TheWallsAndTheViewCoverTheFirstFourHundred)
{
    GameWorld tested;
    auto actors = tested.world.Actors();
    actors[1].alive = true;
    actors[1].dying = 1;
    actors[1].cellX = 1;
    actors[1].cellY = 1;
    actors[2].alive = true;
    actors[2].byte88 = Client::Pathfinding::NonBlockingKind;
    actors[2].cellX = 2;
    actors[2].cellY = 2;
    actors[3].alive = true;
    actors[3].cellX = 3;
    actors[3].cellY = 4;
    tested.Hero().cellX = 30;
    tested.Hero().cellY = 40;
    actors[400].alive = true;
    actors[400].cellX = 50;
    actors[400].cellY = 50;
    tested.Cell(200, 201) = 0x0106;

    const auto stepped = tested.Step(Everything);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FLOAT_EQ(actors[1].dyingTime, 0.02f);
    EXPECT_EQ(tested.Cell(200, 201), 0x0104);
    EXPECT_EQ(tested.Cell(1, 1), 0);
    EXPECT_EQ(tested.Cell(2, 2), 0);
    EXPECT_EQ(tested.Cell(3, 4), Client::Pathfinding::CharacterWallBit);
    EXPECT_EQ(tested.Cell(30, 40), Client::Pathfinding::CharacterWallBit);
    EXPECT_EQ(tested.Cell(50, 50), 0);
    EXPECT_TRUE(actors[0].seen);
    EXPECT_TRUE(actors[399].seen);
    EXPECT_FALSE(actors[400].seen);
    EXPECT_EQ(tested.random.State(),
              RandomAfter(2 * WallDraws + BodyDraws + 2 * (OtherCellDraws + BodyDraws) + SeenHeroDraws).State());
}

// 0x4fbb08: on reaching 1, 0x4fb1a0 releases the cloths in the same frame, also for the hero. 0x4fb1a0 is idempotent:
// the next frames call it again while the death time stays above 1.

TEST(CharacterStepTest, AnOpenAccessSpendsNothingOnClosing)
{
    GameWorld tested;
    tested.Hero().cellXAccesses = 1;
    tested.Hero().cellYAccesses = 1;

    ASSERT_TRUE(tested.Step(Everything).HasValue());

    EXPECT_EQ(tested.Hero().cellXAccesses, 1);
    EXPECT_EQ(tested.Hero().cellYAccesses, 1);
    EXPECT_EQ(tested.random.State(), RandomAfter(OwnRecordDraws + BodyDraws + 1).State());
}

// Without live characters, the step clears the TerrainWall, writes +0x16c and spends no rand().

TEST(CharacterStepTest, WithoutLiveCharactersOnlyTheWallsAndTheViewChange)
{
    GameWorld tested;
    tested.Hero().alive = false;
    tested.Cell(5, 5) = Client::Pathfinding::CharacterWallBit;

    ASSERT_TRUE(tested.Step(Everything).HasValue());

    EXPECT_EQ(tested.Cell(5, 5), 0);
    EXPECT_TRUE(tested.Hero().seen);
    EXPECT_EQ(tested.random.State(), RandomAfter(0).State());
}

// Q-C217: before any 0x5fb570 the global has the .bss zeros, an area-less trapezoid at (0, 0): the four sides give 0,
// which passes the −20 margin, so everyone is seen, anywhere, also the dead one in slot 0. With a new WorldState (what
// WinMain leaves) the step runs whole.

TEST(CharacterStepTest, WithTheTrapezoidOfTheBssEveryoneIsSeen)
{
    GameWorld tested;
    ASSERT_EQ(tested.trapezoid.x, (std::array<float, 4>{}));
    ASSERT_EQ(tested.trapezoid.y, (std::array<float, 4>{}));
    ASSERT_FALSE(tested.trapezoid.everything);
    tested.Hero().position = CellCentre(250, 3);
    tested.world.Actors()[0].position = CellCentre(7, 200);

    const auto stepped = tested.Step();

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(tested.Hero().seen);
    EXPECT_TRUE(tested.world.Actors()[0].seen);
    EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
}

// 0x412140 on map 30, before the loop: with the startup guild status (0x6c649a at 0xff) it builds the class 0 siege
// panel and the step goes on as in Lorencia, with the same rand() and walls; with another status it picks class 1 or 2
// from the guild globals. Outside 30, 0x412560 releases the panel. Without the map loaded there is no TerrainWall: it
// stops before touching anything.

TEST(CharacterStepTest, TheCastleSiegePanelAndAnUnloadedMapComeBeforeTheWalls)
{
    GameWorld castle(30);
    GameWorld lorencia(0);
    for (GameWorld* tested : {&castle, &lorencia})
        tested->Cell(5, 5) = Client::Pathfinding::CharacterWallBit;
    const auto inCastle = castle.Step(Everything);
    ASSERT_TRUE(inCastle.HasValue()) << ErrorOf(inCastle);
    ASSERT_TRUE(lorencia.Step(Everything).HasValue());
    EXPECT_EQ(castle.world.Runtime().castleSiege.siegePanelKind, std::optional<std::uint8_t>{std::uint8_t{0}});
    EXPECT_FALSE(lorencia.world.Runtime().castleSiege.siegePanelKind.has_value());
    EXPECT_EQ(castle.Cell(5, 5), lorencia.Cell(5, 5));
    EXPECT_EQ(castle.random.State(), lorencia.random.State());

    castle.world.Runtime().map = 0;
    ASSERT_TRUE(castle.Step(Everything).HasValue());
    EXPECT_FALSE(castle.world.Runtime().castleSiege.siegePanelKind.has_value());

    GameWorld guild(30);
    guild.world.Runtime().guild.heroGuildStatus = 0x80;
    guild.world.Runtime().guild.heroGuildIndex = 0;
    guild.Cell(5, 5) = Client::Pathfinding::CharacterWallBit;
    const auto inGuild = guild.Step(Everything);
    ASSERT_TRUE(inGuild.HasValue()) << ErrorOf(inGuild);
    EXPECT_EQ(guild.world.Runtime().castleSiege.siegePanelKind, std::optional<std::uint8_t>{std::uint8_t{2}});
    EXPECT_EQ(guild.Cell(5, 5), castle.Cell(5, 5));
    EXPECT_EQ(guild.random.State(), lorencia.random.State());

    GameWorld unloaded;
    unloaded.terrain.attributes.reset();
    const auto withoutMap = unloaded.Step(Everything);
    EXPECT_TRUE(HasError(withoutMap, "0x508f14")) << ErrorOf(withoutMap);
    EXPECT_EQ(unloaded.random.State(), RandomAfter(0).State());
}

// 0x4fc450, only with the hero published: with the own record open, each WORD of +0x3e, +0x40 and +0x42 decreases if
// positive and, at 0, clears its bit of +0x3c and calls its helpers; with +0x40 at 0, the 16 marks of the [0x6eed3c]
// list go back to 0. On close, the 3848 rand() if the count stays at 0; with the record already open, none.

TEST(CharacterStepTest, TheOwnRecordTimersRunWithTheHero)
{
    for (const std::uint8_t depth : {std::uint8_t{0}, std::uint8_t{1}})
    {
        SCOPED_TRACE(::testing::Message() << "con la cuenta en " << static_cast<int>(depth));
        GameWorld tested;
        Client::Kernel::OwnCharacterRecord& own = tested.world.OwnCharacter();
        own.word3e = 2;
        own.word40 = 1;
        own.word42 = 0;
        own.speedAdjustBits = 0x0b;
        tested.world.ItemSets().list.used[3] = 1;
        tested.world.Runtime().ownRecordDepth = depth;

        ASSERT_TRUE(tested.Step(Everything).HasValue());

        EXPECT_EQ(own.word3e, 1);
        EXPECT_EQ(own.word40, 0);
        EXPECT_EQ(own.word42, 0);
        EXPECT_EQ(own.speedAdjustBits, 0x01);
        EXPECT_EQ(tested.world.ItemSets().list.used, (std::array<std::uint8_t, 16>{}));
        EXPECT_EQ(tested.world.Runtime().ownRecordDepth, depth);
        EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws - (depth == 0 ? 0 : OwnRecordDraws)).State());
    }
}

// With a non-hero, 0x4fc450 leaves at 0x4fc464: the own record stays as it was and spends nothing.

TEST(CharacterStepTest, TheOwnRecordTimersSkipTheOthers)
{
    GameWorld tested;
    tested.Hero().alive = false;
    ActorState& other = tested.world.Actors()[3];
    other = Client::Tests::UnarmedPlayer(1);
    other.alive = true;
    other.byte88 = 1;
    other.position = CellCentre(10, 10);
    tested.world.OwnCharacter().word3e = 2;
    const TerrainPolygon polygon = GamePolygon(0, CellCentre(200, 200));
    ASSERT_FALSE(CharacterSeen(polygon, other.position));

    const auto stepped = tested.Step(polygon);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.world.OwnCharacter().word3e, 2);
    EXPECT_EQ(tested.random.State(), RandomAfter(WallDraws + OtherCellDraws + BodyDraws).State());
}

// 0x4e9980 from 0x4fd72e: with +0x16d at 1 (0x511290) the alpha rises by 0.05f, in double, and reaches 1 at step 20,
// the entrance's (the F3 03 reply leaves it at 0); the blendMesh light (+0x68) goes down with it.

TEST(CharacterStepTest, TheHeroAppearsInTwentySteps)
{
    GameWorld tested;
    ActorState& hero = tested.Hero();
    hero.alphaMode = 1;
    hero.alphaTarget = 1.0f;
    hero.alpha = 0.0f;
    hero.blendMeshLight = 1.0f;

    ASSERT_TRUE(tested.Step(Everything).HasValue());
    EXPECT_EQ(hero.alpha, static_cast<float>(static_cast<double>(0.05f)));
    EXPECT_EQ(hero.blendMeshLight, hero.alpha);
    for (int step = 2; step <= 19; ++step)
    {
        ASSERT_TRUE(tested.Step(Everything).HasValue());
    }
    EXPECT_LT(hero.alpha, 1.0f);
    ASSERT_TRUE(tested.Step(Everything).HasValue());
    EXPECT_EQ(hero.alpha, 1.0f);
    EXPECT_LT(hero.blendMeshLight, 0.1f) << "la luz del blendMesh no sube (Q-C112)";
}

// 0x4ec31f..0x4ec33a publishes +0xa4/+0xa8 before the suffix, even without a draw between steps.
TEST(CharacterStepTest, AdvancingTheBodyPublishesTheSharedFrameAndUnclampedKey)
{
    GameWorld tested;
    tested.skeleton = SkeletonWith(1, 20, 0.4f);
    tested.Hero().animation.frame = 1.2f;
    tested.faunaModels.RecordPoseKey(tested.Hero().type, 9);

    const auto stepped = tested.Step(Everything);

    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
    EXPECT_FLOAT_EQ(tested.faunaModels.PoseFrame(tested.Hero().type), 1.6f);
    EXPECT_EQ(tested.faunaModels.PoseKey(tested.Hero().type), 1);
}

// 0x4ec21b/0x4ec229 and 0x4f973a: without progress, +0xa4/+0xa8 are not published and the actor's frame is not
// inherited.
TEST(CharacterStepTest, AOneKeyOrFrozenBodyPreservesTheSharedAnimationMetadata)
{
    for (const bool frozen : {false, true})
    {
        SCOPED_TRACE(frozen);
        GameWorld tested;
        tested.skeleton = SkeletonWith(1, frozen ? 20 : 1, 0.4f);
        tested.Hero().animation.frame = 1.2f;
        tested.Hero().flags78 = frozen ? 0x1000 : 0;
        tested.faunaModels.RecordAnimation(tested.Hero().type, 6.25f, 9);

        const auto stepped = tested.Step(Everything);

        ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
        EXPECT_EQ(tested.faunaModels.PoseFrame(tested.Hero().type), 6.25f);
        EXPECT_EQ(tested.faunaModels.PoseKey(tested.Hero().type), 9);
    }
}

// 0x4f9580: the action's speed (0.4 per step here) times 1.5 in 3 and 4 with +0x3e3, times 0.5 in 0x3f past frame 6
// and in 0x44, times 0.1f in 0x4d, 0x4f and 0x5a past 5.5, and again times 0.5 in 0x44. In class 4, 0x94, 0x51 and
// 0x5b between frames 1 and 3, times 0.5 outside the party (0x59e680).

TEST(CharacterStepTest, TheSpeedOfTheActionGoesThroughItsFactors)
{
    constexpr float Speed = 0.4f;
    struct Case
    {
        std::uint8_t action;
        std::uint8_t byte3e3;
        std::uint8_t characterClass;
        float frame;
        float after;
        std::string_view cut;
    };
    for (const Case& probe :
         {Case{3, 0, 1, 0.0f, Speed, ""}, Case{3, 1, 1, 0.0f, Speed * 1.5f, ""}, Case{4, 1, 1, 0.0f, Speed * 1.5f, ""},
          Case{5, 1, 1, 0.0f, Speed, ""}, Case{0x3f, 0, 1, 6.0f, 6.0f + Speed, ""},
          Case{0x3f, 0, 1, 6.5f, 6.5f + (Speed * 0.5f), ""}, Case{0x4d, 0, 1, 5.0f, 5.0f + Speed, ""},
          Case{0x5a, 0, 1, 5.6f, 5.6f + (Speed * 0.1f), "0x4fdb92"}, Case{0x44, 0, 1, 0.0f, Speed * 0.5f, "0x518aa8"},
          Case{0x51, 0, 4, 2.0f, 2.0f + (Speed * 0.5f), ""}, Case{0x51, 0, 4, 3.5f, 3.5f + Speed, ""},
          Case{0x51, 0, 0x14, 0.5f, 0.5f + Speed, ""}})
    {
        SCOPED_TRACE(::testing::Message() << "acción " << static_cast<int>(probe.action) << ", frame " << probe.frame
                                          << ", clase " << static_cast<int>(probe.characterClass));
        GameWorld tested;
        tested.skeleton = SkeletonWith(probe.action, 20, Speed);
        ActorState& hero = tested.Hero();
        hero.animation.action = probe.action;
        hero.animation.frame = probe.frame;
        hero.byte3e3 = probe.byte3e3;
        hero.characterClass = probe.characterClass;

        const auto stepped = tested.Step(Everything);

        if (probe.cut.empty())
        {
            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        else
        {
            EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        }
        EXPECT_EQ(hero.animation.frame, probe.after);
    }
}

// 0x4f969d..0x4f96ba: with the character in the party (a 0x77f3cd0 slot that starts with its name), class 4's 0x94,
// 0x51 and 0x5b between frames 1 and 3 advance by 0.125 (0x6ba170).
TEST(CharacterStepTest, TheLordInThePartySlowsItsActionsToAnEighth)
{
    constexpr float Speed = 0.4f;
    for (const std::uint8_t action : {std::uint8_t{0x94}, std::uint8_t{0x51}, std::uint8_t{0x5b}})
    {
        SCOPED_TRACE(static_cast<int>(action));
        GameWorld tested;
        tested.skeleton = SkeletonWith(action, 20, Speed);
        ActorState& hero = tested.Hero();
        hero.animation.action = action;
        hero.animation.frame = 1.5f;
        hero.characterClass = 4;
        const std::string_view name = "Lord";
        for (std::size_t at = 0; at < name.size(); ++at)
            hero.name[at] = static_cast<std::byte>(name[at]);
        Client::Social::PartyState& party = tested.world.Runtime().social.party;
        const std::string_view member = "Lordes";
        for (std::size_t at = 0; at < member.size(); ++at)
            party.slots[1].name[at] = static_cast<std::byte>(member[at]);
        tested.world.Runtime().hud.partyCount = 2;

        const auto stepped = tested.Step(Everything);

        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(hero.animation.frame, 1.5f + (Speed * 0.125f));
    }
}

// 0x4fcd00 with the ended action (+0x78 & 0x1000: 0x4f9580 returns 0 without advancing, each step) goes to 0x4fcb10:
// those before 0x0d, 0xbd to 0xbf, 0x54 and 0x55 and, from 0x22 to 0xc2, those not in the list go back to standing
// (0x4f2250: action 1 of the unarmed player, with its suffix's rand()); the others stay; 0x44 goes back to frame 0;
// 0xc3 and 0xc4 stay and set +0x38a to 1 (0x52e810, which with the test skeleton, without bone +0x54, leaves nothing).

TEST(CharacterStepTest, AnEndedActionGoesBackToStanding)
{
    struct Case
    {
        std::uint8_t action;
        std::uint8_t after;
        std::string_view cut;
        int draws;
    };
    constexpr int Stopped = SeenHeroDraws + StopDraws;
    for (const Case& probe :
         {Case{1, 1, "", Stopped}, Case{0x0c, 1, "", Stopped}, Case{0xbd, 1, "", Stopped}, Case{0xbf, 1, "", Stopped},
          Case{0x54, 1, "", Stopped}, Case{0x55, 1, "", Stopped}, Case{0x22, 1, "", Stopped},
          Case{0xc2, 1, "", Stopped}, Case{0x0d, 0x0d, "", SeenHeroDraws}, Case{0x8c, 0x8c, "", SeenHeroDraws},
          Case{0x46, 0x46, "", SeenHeroDraws}, Case{0x6a, 0x6a, "", SeenHeroDraws}, Case{0x75, 0x75, "", SeenHeroDraws},
          Case{0x76, 1, "", Stopped}, Case{0xc5, 0xc5, "", SeenHeroDraws}, Case{0xc3, 0xc3, "", SeenHeroDraws},
          Case{0xc4, 0xc4, "", SeenHeroDraws}})
    {
        SCOPED_TRACE(::testing::Message() << "acción " << static_cast<int>(probe.action));
        GameWorld tested;
        ActorState& hero = tested.Hero();
        hero.flags78 = 0x1000;
        hero.animation.action = probe.action;
        hero.animation.frame = 2.5f;
        hero.run = 1;
        tested.Cell(0, 0) = 1;

        const auto stepped = tested.Step(Everything);

        if (probe.cut.empty())
        {
            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        else
        {
            EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        }
        EXPECT_EQ(hero.animation.action, probe.after);
        EXPECT_EQ(hero.run, probe.draws == Stopped ? 0 : 1);
        EXPECT_EQ(hero.animation.frame, probe.after != probe.action ? 0.0f : 2.5f);
        EXPECT_EQ(hero.byte38a, probe.action == 0xc3 || probe.action == 0xc4 ? 1 : 0);
        EXPECT_EQ(tested.random.State(), RandomAfter(probe.draws).State());
    }
}

// 0x4fcb87..0x4fcba6: the player that ends 0xc3 or 0xc4 with +0x38a at 0 sets it to 1 and leaves its remains
// (0x52e810): with Player.bmd's bone 20 posed on its own bones, two 0x5e3 marks around it, by the scale and position
// published on entering the body, and their ten rand().

TEST(CharacterStepTest, AnEndedActionFortyFourRestartsItsFrame)
{
    GameWorld tested;
    ActorState& hero = tested.Hero();
    hero.flags78 = 0x1000;
    hero.animation.action = 0x44;
    hero.animation.frame = 2.5f;

    const auto stepped = tested.Step(Everything);

    EXPECT_TRUE(HasError(stepped, "0x518aa8")) << ErrorOf(stepped);
    EXPECT_EQ(hero.animation.action, 0x44);
    EXPECT_EQ(hero.animation.frame, 0.0f);
    EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws + 2).State());
}

// Regression of the attack observed live: the visible hand must make a ribbon, not close Game.
// 0x4fd236..0x4fd5bb: ten samples, type 0, level 7 color, and it goes on with 0x432bf1's rand.
TEST(CharacterStepTest, AnEquippedSwordAttackCreatesItsTrailAndContinuesTheBody)
{
    GameWorld tested;
    ActorState& hero = tested.Hero();
    hero.animation.action = 0x23;
    hero.animation.frame = 3.0f;
    hero.position = {100.0f, 200.0f, 300.0f};
    hero.scale = 1.0f;
    hero.hands[0].model = 0x222;
    hero.hands[0].level = 7;
    hero.hands[0].bone = 0;
    tested.skeleton.actions[0x23] = ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 10};
    Client::Models::SkeletonBone bone;
    bone.actions.resize(tested.skeleton.actions.size());
    for (auto& keys : bone.actions)
        keys.resize(10);
    tested.skeleton.bones.push_back(std::move(bone));

    const auto stepped = tested.Step(Everything);

    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
    const auto& ribbon = tested.world.DrawTail().ribbons[0];
    ASSERT_TRUE(ribbon.alive);
    EXPECT_EQ(ribbon.ownerContext, &hero);
    EXPECT_EQ(ribbon.ownerEquipment, &hero.level);
    EXPECT_EQ(ribbon.type, 0);
    // 0x4fd5a0's ten creations come before the single ribbon step at the end of the pool (0x5091d0).
    EXPECT_EQ(ribbon.count, 9);
    EXPECT_EQ(ribbon.life, 14);
    EXPECT_EQ(ribbon.light, (glm::vec3{1.0f, 0.6f, 0.2f}));
    EXPECT_EQ(ribbon.first[0], (glm::vec3{100.0f, 180.0f, 300.0f}));
    EXPECT_EQ(ribbon.second[0], (glm::vec3{100.0f, 80.0f, 300.0f}));
    EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws + TrailDraws).State());
}

// 0x4fcee0: before frame 3, except in 0x8d, copies the position to +0x17c and leaves; otherwise, with the 0x20a, the
// trail by action and hand weapon (the left one with 0x25 and 0x26) stops at 0x52e470, and without a trail 0x432bd0
// draws its rand() unconditionally (0x432bf1).

TEST(CharacterStepTest, TheWeaponTrailDrawsFromFrameThree)
{
    constexpr std::uint16_t NoItem = Client::Kernel::NoItemModel;
    struct Case
    {
        std::uint8_t action;
        float frame;
        std::uint16_t right;
        std::uint16_t left;
        bool trail;
        int draws;
    };
    constexpr int Trailed = WallDraws + OwnRecordDraws + BodyDraws;
    constexpr int Drawn = SeenHeroDraws + TrailDraws;
    for (const Case& probe :
         {Case{1, 2.99f, NoItem, NoItem, false, SeenHeroDraws}, Case{1, 3.0f, NoItem, NoItem, false, Drawn},
          Case{0x8d, 0.0f, NoItem, NoItem, false, Drawn}, Case{0x8d, 0.0f, 0x212, NoItem, true, Trailed},
          Case{0x84, 3.0f, NoItem, NoItem, true, Trailed}, Case{0x4c, 3.0f, NoItem, NoItem, true, Trailed},
          Case{0x39, 3.0f, NoItem, NoItem, true, Trailed}, Case{0x3c, 3.0f, NoItem, NoItem, true, Trailed},
          Case{0x23, 3.0f, 0x212, NoItem, true, Trailed}, Case{0x29, 3.0f, 0x411, NoItem, true, Trailed},
          Case{0x2a, 3.0f, 0x411, NoItem, false, Drawn}, Case{0x25, 3.0f, 0x212, NoItem, false, Drawn},
          Case{0x25, 3.0f, NoItem, 0x212, true, Trailed}, Case{0x38, 3.0f, 0x415, NoItem, true, Trailed},
          Case{0x38, 3.0f, 0x416, NoItem, false, Drawn}, Case{0x38, 3.0f, 0x811, NoItem, true, Trailed},
          Case{0x2a, 3.0f, 0x812, NoItem, true, Trailed}, Case{0x2d, 3.0f, 0xa11, NoItem, true, Trailed},
          Case{0x2a, 3.0f, 0xa12, NoItem, false, Drawn}})
    {
        SCOPED_TRACE(::testing::Message() << "acción " << static_cast<int>(probe.action) << ", frame " << probe.frame
                                          << ", manos " << probe.right << " y " << probe.left);
        GameWorld tested;
        ActorState& hero = tested.Hero();
        hero.animation.action = probe.action;
        hero.animation.frame = probe.frame;
        hero.hands[0].model = probe.right;
        hero.hands[1].model = probe.left;
        const glm::vec3 retained{1.0f, 2.0f, 3.0f};
        hero.trailOrigin = retained;

        const auto stepped = tested.Step(Everything);

        const bool copies = !(probe.frame >= 3.0f) && probe.action != 0x8d;
        EXPECT_EQ(hero.trailOrigin, copies ? hero.position : retained);

        if (probe.trail)
        {
            EXPECT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
            EXPECT_TRUE(tested.world.DrawTail().ribbons[0].alive);
        }
        else
        {
            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        EXPECT_EQ(tested.random.State(), RandomAfter(probe.trail ? Drawn : probe.draws).State());
    }
}

// Where the body (0x4fd5f0) needs what MuRet lacks it stops, in its order and after the earlier rand(): maps 18 to 23
// with the hero 10 s on a cell with bit 8 without the C1 06 AF 02 session (0x4fbee8), 0xc5 without Text.bmd
// (0x4fd63b), 0xf1 without the terrain heights (0x4fb942), 0x6c88b0's name without timeGetTime() (0x4fcd82), another
// model, Player.bmd not loaded or an action outside it (0x4f9580, 0x4f95c0), the particle of 0x4d, 0x4f and 0x5a
// (0x4fdb70) and, after hitting, a +0x38e skill in 0x4f97b0 (0x4f9899, after reading it; also while dying: the dying
// step, 0x4fb9e0, and 0x4fd787's already ran).
TEST(CharacterStepTest, TheBodyStopsWhereMuRetHasNothing)
{
    struct Case
    {
        std::string_view name;
        void (*prepare)(GameWorld&);
        std::string_view cut;
        int draws;
    };
    constexpr int AfterTheTimers = WallDraws + OwnRecordDraws;
    const std::array cases{
        Case{"mapa 18", [](GameWorld& tested) { tested.StandOnTheReportedCell(18); }, "0x4fbee8",
             WallDraws + HeroCellReads},
        Case{"mapa 23", [](GameWorld& tested) { tested.StandOnTheReportedCell(23); }, "0x4fbee8",
             WallDraws + HeroCellReads},
        Case{"el 0xc5", [](GameWorld& tested) { tested.Hero().type = 0xc5; }, "0x4fd63b", WallDraws},
        Case{"el 0xf1", [](GameWorld& tested) { tested.Hero().type = 0xf1; }, "0x4fb942", AfterTheTimers},
        Case{"se muere y pegó con una habilidad",
             [](GameWorld& tested)
             {
                 tested.GiveTheHandABone();
                 tested.Hero().dying = 1;
                 tested.Hero().animation.action = 0x3d;
                 tested.Hero().byte381 = FrameSkillStep;
                 tested.Hero().byte38e = FrameSkill;
             },
             FrameSkillCut, OwnRecordDraws + BodyDraws + SkillReadDraws + FrameSkillDraws},
        Case{"pegó con una habilidad",
             [](GameWorld& tested)
             {
                 tested.GiveTheHandABone();
                 tested.Hero().animation.action = 0x3d;
                 tested.Hero().byte381 = FrameSkillStep;
                 tested.Hero().byte38e = FrameSkill;
             },
             FrameSkillCut, AfterTheTimers + BodyDraws + SkillReadDraws + FrameSkillDraws},
        Case{"el nombre",
             [](GameWorld& tested)
             {
                 constexpr std::array<std::uint8_t, 11> Name{0xc8, 0xb2, 0xb1, 0xdd, 0xc0, 0xc7,
                                                             0x20, 0xb1, 0xc3, 0xbc, 0xf6};
                 for (std::size_t i = 0; i < tested.Hero().name.size(); ++i)
                 {
                     tested.Hero().name[i] = std::byte{Name[i]};
                 }
                 tested.Hero().byte1e3 = Name[10];
             },
             "0x4fcd82", AfterTheTimers},
        Case{"sin Player.bmd", [](GameWorld& tested) { tested.player = nullptr; }, "0x4f9580", AfterTheTimers},
        Case{"una acción fuera",
             [](GameWorld& tested)
             {
                 tested.skeleton.actions.resize(2);
                 tested.Hero().animation.action = 5;
             },
             "0x4f95c0", AfterTheTimers},
        Case{"la partícula",
             [](GameWorld& tested)
             {
                 tested.Hero().animation.action = 0x4f;
                 tested.Hero().animation.frame = 5.6f;
             },
             "0x4fdb92", AfterTheTimers + BodyDraws},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        GameWorld tested;
        probe.prepare(tested);

        const auto stepped = tested.Step(Everything);

        EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        EXPECT_EQ(tested.random.State(), RandomAfter(probe.draws).State());
    }
    // Another unloaded model is the record at 0 (+0x26 without actions): 0x4f9580 does not advance, the rand() % 6 is
    // drawn and 0x505de0 builds 0x505e15's box and leaves, without the cell or the & 7.
    GameWorld unloaded;
    unloaded.Hero().type = 0x100;
    const auto boxed = unloaded.Step(Everything);
    EXPECT_TRUE(boxed.HasValue()) << ErrorOf(boxed);
    EXPECT_EQ(unloaded.random.State(), RandomAfter(AfterTheTimers + BodyDraws).State());
}

// The edges of those cuts: maps 17 and 24, the name with +0x1e3 at 0 (what the F3 03 reply leaves) and the particle
// at exactly frame 5.5 take the whole path.

TEST(CharacterStepTest, TheBordersOfTheBodyCutsGoOn)
{
    for (const std::int32_t map : {17, 24})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << map);
        GameWorld tested(map);
        const auto stepped = tested.Step(Everything);
        EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
    }
    GameWorld named;
    constexpr std::array<std::uint8_t, 10> Name{0xc8, 0xb2, 0xb1, 0xdd, 0xc0, 0xc7, 0x20, 0xb1, 0xc3, 0xbc};
    for (std::size_t i = 0; i < Name.size(); ++i)
    {
        named.Hero().name[i] = std::byte{Name[i]};
    }
    const auto withName = named.Step(Everything);
    EXPECT_TRUE(withName.HasValue()) << withName.Error();
    GameWorld particle;
    particle.Hero().animation.action = 0x4f;
    particle.Hero().animation.frame = 5.5f;
    const auto atTheBorder = particle.Step(Everything);
    EXPECT_TRUE(atTheBorder.HasValue()) << atTheBorder.Error();
    EXPECT_EQ(particle.random.State(), RandomAfter(SeenHeroDraws + TrailDraws).State());
}

// 0x4fcd00 with +0x88 at 4 and the 0x20a of subtype 0xd2 (type 236) in Lorencia: turns to 90 and 0x4fc790 draws its
// rand() after the clocks; without +0x376 at 0x101 it says nothing. The rest is the idle hero's path, with one more.

TEST(CharacterStepTest, ChaosCastleKeepsTheHeroCellAndItsClock)
{
    GameWorld plain(17);
    plain.Hero().cellX = 5;
    plain.Hero().cellY = 6;
    ASSERT_TRUE(plain.Step(Everything).HasValue());
    const auto after = [&](int extra)
    {
        CrtRandom expected = plain.random;
        for (int draw = 0; draw < extra; ++draw)
            static_cast<void>(expected.Next());
        return expected.State();
    };
    const auto step = [](GameWorld& tested, float clock)
    {
        return StepCharacters(CharacterStepContext{.world = tested.world,
                                                   .random = tested.random,
                                                   .scene = SceneId::Game,
                                                   .playerSkeleton = tested.player,
                                                   .terrain = &tested.terrain,
                                                   .worldClock = clock,
                                                   .trapezoid = &tested.trapezoid,
                                                   .dynamicLighting = &tested.light.Lighting(),
                                                   .faunaModels = &tested.faunaModels,
                                                   .reportedCell = &tested.reportedCell,
                                                   .hooks = &StepHooks});
    };
    {
        SCOPED_TRACE("otra celda");
        GameWorld moved(18);
        moved.Hero().cellX = 5;
        moved.Hero().cellY = 6;
        moved.trapezoid = Everything;
        const auto stepped = step(moved, 1234.0f);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(moved.reportedCell.x, 5);
        EXPECT_EQ(moved.reportedCell.y, 6);
        EXPECT_EQ(moved.reportedCell.time, 1234.0f);
        EXPECT_EQ(moved.random.State(), after(3 * Client::Models::SmallLogicalCloseRandoms));
    }
    {
        SCOPED_TRACE("la misma, 9,999 s después");
        GameWorld waiting(18);
        waiting.StandOnTheReportedCell(18);
        waiting.reportedCell.time = 0.0f;
        waiting.trapezoid = Everything;
        const auto stepped = step(waiting, 9999.0f);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(waiting.reportedCell.time, 0.0f);
        EXPECT_EQ(waiting.random.State(), after(HeroCellReads));
    }
    {
        SCOPED_TRACE("la misma, 10 s después y sin el bit 8");
        GameWorld still(18);
        still.StandOnTheReportedCell(18);
        still.Cell(5, 6) = 0;
        still.reportedCell.time = 0.0f;
        still.trapezoid = Everything;
        const auto stepped = step(still, 10000.0f);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(still.reportedCell.time, 10000.0f);
        EXPECT_EQ(still.random.State(), after(HeroCellReads));
    }
    for (const std::int32_t map : {18, 21, 23})
    {
        SCOPED_TRACE(testing::Message() << "la misma, 10 s después y con el bit 8, en el mapa " << map);
        // CL9 milestone 11, 0x4fbeed..0x4fc39d: the C1 06 AF 02 with the low byte of 0x743a180 and 0x743a184, and the
        // clock.
        GameWorld reported(map);
        reported.StandOnTheReportedCell(map);
        reported.reportedCell.time = 0.0f;
        reported.trapezoid = Everything;
        std::vector<std::pair<std::uint8_t, std::uint8_t>> sent;
        const auto stepped = StepCharacters(
            CharacterStepContext{.world = reported.world,
                                 .random = reported.random,
                                 .scene = SceneId::Game,
                                 .playerSkeleton = reported.player,
                                 .terrain = &reported.terrain,
                                 .worldClock = 10000.0f,
                                 .sendChaosCastleReposition =
                                     [&](std::uint8_t x, std::uint8_t y) -> Client::Core::Expected<void, std::string>
                                 {
                                     sent.emplace_back(x, y);
                                     return {};
                                 },
                                 .trapezoid = &reported.trapezoid,
                                 .dynamicLighting = &reported.light.Lighting(),
                                 .faunaModels = &reported.faunaModels,
                                 .reportedCell = &reported.reportedCell,
                                 .hooks = &StepHooks});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_EQ(sent[0], (std::pair<std::uint8_t, std::uint8_t>{std::uint8_t{5}, std::uint8_t{6}}));
        EXPECT_EQ(reported.reportedCell.time, 10000.0f);
        EXPECT_EQ(reported.random.State(), after(HeroCellReads));
    }
    {
        SCOPED_TRACE("el envío que falla corta con el reloj guardado (0x4fc313)");
        GameWorld failing(18);
        failing.StandOnTheReportedCell(18);
        failing.reportedCell.time = 0.0f;
        failing.trapezoid = Everything;
        const auto stepped = StepCharacters(CharacterStepContext{
            .world = failing.world,
            .random = failing.random,
            .scene = SceneId::Game,
            .playerSkeleton = failing.player,
            .terrain = &failing.terrain,
            .worldClock = 10000.0f,
            .sendChaosCastleReposition = [](std::uint8_t, std::uint8_t) -> Client::Core::Expected<void, std::string>
            { return Client::Core::Unexpected(std::string("el GameServer no está abierto")); },
            .trapezoid = &failing.trapezoid,
            .dynamicLighting = &failing.light.Lighting(),
            .faunaModels = &failing.faunaModels,
            .reportedCell = &failing.reportedCell,
            .hooks = &StepHooks});
        ASSERT_FALSE(stepped.HasValue());
        EXPECT_NE(stepped.Error().find("GameServer"), std::string::npos) << stepped.Error();
        EXPECT_EQ(failing.reportedCell.time, 10000.0f);
    }
}

TEST(CharacterStepTest, ChaosCastleStopsWithoutTheReportedCell)
{
    GameWorld tested(18);
    tested.Hero().cellX = 5;
    tested.Hero().cellY = 6;
    tested.trapezoid = Everything;
    const auto stepped = StepCharacters(CharacterStepContext{.world = tested.world,
                                                             .random = tested.random,
                                                             .scene = SceneId::Game,
                                                             .playerSkeleton = tested.player,
                                                             .terrain = &tested.terrain,
                                                             .trapezoid = &tested.trapezoid,
                                                             .dynamicLighting = &tested.light.Lighting(),
                                                             .faunaModels = &tested.faunaModels,
                                                             .hooks = &StepHooks});
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x4fbe10"), std::string::npos) << stepped.Error();
}

// The (x, y) cell of the dynamic buffer 0x7b4ccc4.
glm::vec3 DynamicLightAt(const Client::Terrain::DynamicWorldLight& light, std::uint8_t x, std::uint8_t y)
{
    const auto& cell = light.Lighting().cells[Client::Data::Terrain::CellIndex(x, y)];
    return {cell[0], cell[1], cell[2]};
}

// 0x506acf..0x506b62: with +0x375 below 6, a weapon with light in either hand adds to the dynamic buffer (0x5f9780) at
// the object's x and y, without rand(); the step runs to the end. 0x21e gives (0.8, 0.64, 0.4) with radius 3 and the
// other four (0.64, 0.4, 0.24) with radius 2, with the DWORD bits of 0x506af7..0x506b05 and 0x506b2d..0x506b3b. With 6
// or more, no lights.

TEST(CharacterStepTest, TheHandLightsAddToTheDynamicBufferBelowPkSix)
{
    struct Case
    {
        std::uint16_t model;
        std::array<std::uint32_t, 3> color;
        bool radiusThree;
    };
    constexpr std::array<std::uint32_t, 3> Bright{0x3f4ccccdu, 0x3f23d70bu, 0x3ecccccdu};
    constexpr std::array<std::uint32_t, 3> Dim{0x3f23d70bu, 0x3ecccccdu, 0x3e75c290u};
    for (const Case& probe : {Case{0x21e, Bright, true}, Case{0x225, Dim, false}, Case{0xa24, Dim, false},
                              Case{0xc1c, Dim, false}, Case{0x61f, Dim, false}})
    {
        for (const std::size_t hand : {std::size_t{0}, std::size_t{1}})
        {
            SCOPED_TRACE(::testing::Message() << probe.model << " en la mano " << hand);
            GameWorld tested;
            tested.Hero().hands[hand].model = probe.model;
            // At (0, 0): the centre cell gets the whole color, with weight 1.
            tested.Cell(0, 0) = 1;

            const auto stepped = tested.Step(Everything);

            ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
            EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
            EXPECT_EQ(tested.Hero().safeZone, 1);
            const glm::vec3 center = DynamicLightAt(tested.light, 0, 0);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(center.r), probe.color[0]);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(center.g), probe.color[1]);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(center.b), probe.color[2]);
            // Two cells away, the radius 2 weight is 0 and does not touch the cell; radius 3 leaves a third.
            const glm::vec3 two = DynamicLightAt(tested.light, 2, 0);
            if (probe.radiusThree)
            {
                EXPECT_GT(two.r, 0.0f);
            }
            else
            {
                EXPECT_EQ(two, glm::vec3(0.0f));
            }
            EXPECT_EQ(DynamicLightAt(tested.light, 3, 0), glm::vec3(0.0f));

            GameWorld outlaw;
            outlaw.Hero().hands[hand].model = probe.model;
            outlaw.Hero().pk = 6;
            EXPECT_TRUE(outlaw.Step(Everything).HasValue());
            EXPECT_EQ(outlaw.random.State(), RandomAfter(SeenHeroDraws).State());
            EXPECT_EQ(DynamicLightAt(outlaw.light, 0, 0), glm::vec3(0.0f));
        }
    }
}

// Each channel adds in double and stores in float, on top of what the other hand left.

TEST(CharacterStepTest, BothHandsAddTheirLightsRightFirst)
{
    GameWorld tested;
    tested.Hero().hands[0].model = 0x21e;
    tested.Hero().hands[1].model = 0x61f;

    ASSERT_TRUE(tested.Step(Everything).HasValue());

    const glm::vec3 center = DynamicLightAt(tested.light, 0, 0);
    const auto sum = [](std::uint32_t first, std::uint32_t second)
    {
        return static_cast<float>(static_cast<double>(std::bit_cast<float>(second)) +
                                  static_cast<double>(std::bit_cast<float>(first)));
    };
    EXPECT_EQ(center.r, sum(0x3f4ccccdu, 0x3f23d70bu));
    EXPECT_EQ(center.g, sum(0x3f23d70bu, 0x3ecccccdu));
    EXPECT_EQ(center.b, sum(0x3ecccccdu, 0x3e75c290u));
}

// The 0x20a case in 0x506cf8 and the hero's steps (0x50862f), after the rand() & 7: map 7 and actions 0x44, 0x45 and
// 0x96 stop (7 without the bubble pool; 0x44 and 0x45 without the joint table, after the two angle rand() of the first
// 0x619/8); 9 has its own test. The steps already make CL10 requests without stopping or adding RNG.

TEST(CharacterStepTest, ThePlayerEffectsAndStepsStopAfterTheRandom)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t action;
        std::string_view cut;
    };
    for (const Case& probe : {Case{7, 1, "0x506d77"}, Case{0, 0x44, "0x518aa8"}, Case{0, 0x45, "0x518aa8"},
                              Case{0, 0x96, "0x506f84"}, Case{0, 0x0d, ""}, Case{0, 0x21, ""}, Case{2, 0x8b, ""},
                              Case{2, 0x8c, ""}, Case{2, 0x4b, ""}, Case{2, 0x0c, ""}, Case{0, 0x22, ""}})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << probe.map << ", acción " << static_cast<int>(probe.action));
        GameWorld tested(probe.map);
        tested.Hero().animation.action = probe.action;

        const auto stepped = tested.Step(Everything);

        if (probe.cut.empty())
        {
            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        else
        {
            EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        }
        EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws + (probe.action == 0x44 || probe.action == 0x45 ? 2
                                                                      : probe.action == 0x96                       ? 1
                                                                                                                   : 0))
                                             .State());
    }
}

// 0x508ba0 with an idle non-hero: the logical X and Y in two closing accesses (8 rand()), equal to the clear copy
// (0x511290 writes both), so it does not look for a path (Q-C236). Its body (0x4fd5f0) draws the rand() % 6, without
// the own record, and its 0x505de0, seen, the cell (8 more), the head draw (0x5068fd) and the rand() & 7. Then the
// hero in slot 42 takes its whole step.

TEST(CharacterStepTest, AnotherCharacterRunsItsWholeStepBeforeTheHero)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << map);
        GameWorld tested(map);
        ActorState& other = tested.Other(3);
        CrtRandom expected(Seed);
        Skip(expected, 2 * WallDraws);
        CharacterHead head;
        static_cast<void>(DrawASeenOther(expected, head));
        Skip(expected, OwnRecordDraws + BodyDraws + SeenEffectsDraws);

        const auto stepped = tested.Step(Everything);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_TRUE(other.seen);
        EXPECT_EQ(other.cellXAccesses, 0);
        EXPECT_EQ(other.cellYAccesses, 0);
        EXPECT_EQ(tested.Cell(3, 4), Client::Pathfinding::CharacterWallBit);
        EXPECT_EQ(other.headTurn.angles, head.angles);
        EXPECT_EQ(other.headTurn.target, head.target);
        EXPECT_EQ(tested.random.State(), expected.State());
    }
}

// 0x508e0e → 0x4eac00 (Q-C1351): an idle other with the target far from its cell retries the path (0x508c84), and
// 0x4eac0a..0x4eac41 sets the flag if 0x6c8e54 has an altar (record 9, dead) with stage 4 on map 34: its wall target
// stays at 0 and the path arrives. In Lorencia and Devias the wall stays and the path stops beside it.
TEST(CharacterStepTest, AnotherCharacterRetriesItsPathWithTheCrywolfAltarFlag)
{
    for (const std::int32_t map : {0, 2, 34})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << map);
        GameWorld tested(map);
        tested.world.Runtime().crywolf.stage = 4;
        tested.world.Runtime().chosenNpc.index = 9;
        tested.world.Actors()[9].type = 0x205;
        ActorState& other = tested.Other(3);
        other.targetX = 7;
        tested.Cell(7, 4) = Client::Pathfinding::BlockedWallBit;

        const auto stepped = tested.Step(Everything);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(other.movement, 1);
        ASSERT_GT(other.path.count, 1);
        const auto& last = other.path.cells[other.path.count - 1U];
        EXPECT_EQ(tested.Cell(7, 4), map == 34 ? 0 : Client::Pathfinding::BlockedWallBit);
        EXPECT_EQ(last.x, map == 34 ? 7 : 6);
        EXPECT_EQ(last.y, map == 34 ? 4 : 3);
    }
}

// 0x5068eb..0x5069ab with a non-hero, step by step: rand() % 32 at 0 draws the target with two more rand() (+0x34 and
// +0x38), and both angles turn toward it (+0x28 and +0x2c), as in the login. With the hero dead, each step draws the
// other's TerrainWall and its own, with 1 or 3 for the head.

TEST(CharacterStepTest, TheHeadOfAnotherCharacterSortsOneStepInThirtyTwo)
{
    constexpr int Steps = 200;
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << map);
        GameWorld tested(map);
        tested.Hero().alive = false;
        ActorState& other = tested.Other(3);
        CrtRandom expected(Seed);
        CharacterHead head;
        int sorted = 0;
        for (int step = 1; step <= Steps; ++step)
        {
            SCOPED_TRACE(::testing::Message() << "paso " << step);
            Skip(expected, WallDraws);
            sorted += DrawASeenOther(expected, head) ? 1 : 0;

            const auto stepped = tested.Step(Everything);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            ASSERT_EQ(tested.random.State(), expected.State());
            ASSERT_EQ(other.headTurn.angles, head.angles);
            ASSERT_EQ(other.headTurn.target, head.target);
        }
        EXPECT_GT(sorted, 0) << "la cabeza tiene que sortear al menos una vez";
        EXPECT_NE(other.headTurn.angles, glm::vec2(0.0f));
    }
}

// With the hero the head neither draws nor spends: it turns toward the target (+0x34 and +0x38), which MoveHero writes
// from the mouse (CL7.4), at most 20 % of what is left per step.

TEST(CharacterStepTest, TheHeroTurnsItsHeadWithoutRandoms)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << map);
        GameWorld tested(map);
        ActorState& hero = tested.Hero();
        hero.headTurn.target = glm::vec2(40.0f, 350.0f);
        CharacterHead head = hero.headTurn;
        CrtRandom untouched(Seed);
        StepCharacterHead(head, false, untouched);
        ASSERT_EQ(untouched.State(), RandomAfter(0).State());
        ASSERT_NE(head.angles, glm::vec2(0.0f));

        const auto stepped = tested.Step(Everything);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(hero.headTurn.angles, head.angles);
        EXPECT_EQ(hero.headTurn.target, glm::vec2(40.0f, 350.0f));
        EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
    }
}

// 0x5068be..0x5068d4: with +0x78 & 0x20 or & 0x1000 the head neither moves nor draws, and the step goes on with the
// rand() & 7. With 0x1000 the action also ends without advancing and goes back to standing (0x4f2250, with its
// suffix's rand()).

TEST(CharacterStepTest, TheFlagsOf78KeepTheHeadStill)
{
    struct Case
    {
        std::int32_t map;
        std::uint32_t flags;
        int stopDraws;
    };
    for (const Case& probe :
         {Case{0, 0x20, 0}, Case{2, 0x1000, StopDraws}, Case{0, 0x1020, StopDraws}, Case{2, 0x20, 0}})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << probe.map << ", +0x78 = " << probe.flags);
        GameWorld tested(probe.map);
        tested.Hero().alive = false;
        ActorState& other = tested.Other(3);
        other.flags78 = probe.flags;
        other.headTurn.target = glm::vec2(30.0f, 10.0f);

        const auto stepped = tested.Step(Everything);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(other.headTurn.angles, glm::vec2(0.0f));
        EXPECT_EQ(other.headTurn.target, glm::vec2(30.0f, 10.0f));
        EXPECT_EQ(tested.random.State(), RandomAfter(WallDraws + OtherCellDraws + BodyDraws + probe.stopDraws +
                                                     OtherSeenCellDraws + EffectsRandomDraws)
                                             .State());
    }
}

// 0x50862f..0x508641: the steps (0x505770) are only the published hero's. Another one in the step actions does not
// stop and runs to the end.

TEST(CharacterStepTest, TheStepSoundsAreOnlyTheHeros)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t action : std::array<std::uint8_t, 6>{0x0d, 0x15, 0x21, 0x8b, 0x8c, 0x4b})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", acción " << static_cast<int>(action));
            GameWorld tested(map);
            tested.Hero().alive = false;
            tested.Other(3).animation.action = action;
            CrtRandom expected(Seed);
            Skip(expected, WallDraws);
            static_cast<void>(DrawASeenOther(expected));

            const auto stepped = tested.Step(Everything);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(tested.random.State(), expected.State());
        }
    }
}

// 0x5086cf..0x5087fc: in Kalima (maps 24 to 29 and 36), the 0x20a in actions 0x15, 0x1d to 0x21 and 0x4b draws two
// rand() % 64 and 0x6f4, which stops. On the neighboring maps, or with another action, it goes on. The hero already
// goes through the 0x508668 sound request and reaches the same particle, with its two extra rand.

TEST(CharacterStepTest, KalimaParticleCutsAfterItsTwoRandoms)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t action;
        bool cuts;
    };
    for (const Case& probe :
         {Case{24, 0x15, true}, Case{25, 0x1d, true}, Case{26, 0x1e, true}, Case{27, 0x1f, true}, Case{28, 0x20, true},
          Case{29, 0x21, true}, Case{36, 0x4b, true}, Case{35, 0x15, false}, Case{37, 0x4b, false},
          Case{24, 0x16, false}, Case{29, 0x1c, false}, Case{36, 0x0d, false}})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << probe.map << ", acción " << static_cast<int>(probe.action));
        GameWorld tested(probe.map);
        tested.Hero().alive = false;
        tested.Other(3).animation.action = probe.action;
        CrtRandom expected(Seed);
        Skip(expected, WallDraws);
        static_cast<void>(DrawASeenOther(expected));
        Skip(expected, probe.cuts ? 2 : 0);

        const auto stepped = tested.Step(Everything);

        if (probe.cuts)
        {
            EXPECT_TRUE(HasError(stepped, "0x5087fc")) << ErrorOf(stepped);
            EXPECT_NE(stepped.Error().find("lugar 3 "), std::string::npos) << stepped.Error();
        }
        else
        {
            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        EXPECT_EQ(tested.random.State(), expected.State());
    }
    GameWorld hero(24);
    hero.Hero().animation.action = 0x15;
    EXPECT_TRUE(HasError(hero.Step(Everything), "0x5087fc"));
    EXPECT_EQ(hero.random.State(), RandomAfter(SeenHeroDraws + 2).State());
}

// After the head, a non-hero takes the hero's 0x20a path: one hand's light adds to the dynamic buffer and goes on; map
// 7 (only in the game) and actions 0x44, 0x45 and 0x96 stop after the rand() & 7 (0x44 and 0x45, without the joint
// table, at the first 0x619/8).

TEST(CharacterStepTest, AfterItsHeadAnotherCharacterTakesThePathOfTheHero)
{
    struct Case
    {
        std::string_view name;
        std::int32_t map;
        std::uint8_t action;
        std::uint16_t rightHand;
        std::string_view cut;
        bool drawsTheRandom;
    };
    constexpr std::uint16_t NoItem = Client::Kernel::NoItemModel;
    for (const Case& probe :
         {Case{"el mapa 7", 7, 1, NoItem, "0x506d77", true}, Case{"la 0x44", 2, 0x44, NoItem, "0x518aa8", true},
          Case{"la 0x45", 0, 0x45, NoItem, "0x518aa8", true}, Case{"la 0x96", 2, 0x96, NoItem, "0x506f84", true}})
    {
        SCOPED_TRACE(probe.name);
        GameWorld tested(probe.map);
        tested.Hero().alive = false;
        ActorState& other = tested.Other(3);
        other.animation.action = probe.action;
        other.hands[0].model = probe.rightHand;
        CrtRandom expected(Seed);
        Skip(expected, WallDraws);
        static_cast<void>(DrawASeenOther(expected));
        CrtRandom withoutTheRandom(Seed);
        Skip(withoutTheRandom, WallDraws + OtherCellDraws + BodyDraws + OtherSeenCellDraws);
        CharacterHead head;
        StepCharacterHead(head, true, withoutTheRandom);

        const int extra = probe.action == 0x44 || probe.action == 0x45 ? 2 : probe.action == 0x96 ? 1 : 0;
        Skip(expected, extra);
        Skip(withoutTheRandom, extra);
        const auto stepped = tested.Step(Everything);

        EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        EXPECT_EQ(tested.random.State(), (probe.drawsTheRandom ? expected : withoutTheRandom).State());
    }
    GameWorld selection(7);
    selection.Hero().alive = false;
    static_cast<void>(selection.Other(3));
    CrtRandom expected(Seed);
    Skip(expected, WallDraws);
    static_cast<void>(DrawASeenOther(expected));
    const auto stepped = selection.Step(Everything, SceneId::CharacterSelect);
    EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(selection.random.State(), expected.State());

    GameWorld lit;
    lit.Hero().alive = false;
    lit.Other(3).hands[0].model = 0x21e;
    CrtRandom afterTheLight(Seed);
    Skip(afterTheLight, WallDraws);
    static_cast<void>(DrawASeenOther(afterTheLight));
    const auto litStep = lit.Step(Everything);
    EXPECT_TRUE(litStep.HasValue()) << ErrorOf(litStep);
    EXPECT_EQ(lit.random.State(), afterTheLight.State());
    EXPECT_EQ(DynamicLightAt(lit.light, 0, 0).r, std::bit_cast<float>(0x3f4ccccdu));
}

// 0x508ba0's exits before the cell: the dying one leaves (0x508bb6) and runs its body, with the dying step (0x4fb9e0)
// and the rand() % 6, up to the +0x38e skill after hitting (0x4f9899), except 0x154, which takes the terrain height
// (0x508e89); the walking one follows its path (0x508e27). Each with the hero alive in slot 42, after the TerrainWall.
// 0x159 leaves without reading the cell; without its model (the record at 0), it does not advance, draws the rand() %
// 6 and 0x505de0 builds its box (0x505e15), and then the hero takes its whole step.

TEST(CharacterStepTest, TheExitsOf508ba0BeforeTheCell)
{
    struct Case
    {
        std::string_view name;
        std::uint16_t type;
        std::uint8_t dying;
        std::uint8_t movement;
        std::string_view cut;
        int draws;
    };
    constexpr std::uint16_t Player = Client::Tables::PlayerSkeletonModel;
    for (const Case& probe :
         {Case{"se muere", Player, 1, 0, FrameSkillCut, WallDraws + BodyDraws + SkillReadDraws + FrameSkillDraws},
          Case{"se muere el 0x154", 0x154, 1, 0, "0x508e89", WallDraws},
          // Walk exists: it consumes its rand before Position finds the missing height.
          Case{"camina", Player, 0, 1, "0x508a80", 2 * WallDraws + 1},
          Case{"el 0x159", 0x159, 0, 0, "", 2 * WallDraws + BodyDraws + OwnRecordDraws + BodyDraws + SeenEffectsDraws}})
    {
        SCOPED_TRACE(probe.name);
        GameWorld tested;
        if (probe.type == Player && probe.dying != 0)
            tested.GiveTheHandABone();
        ActorState& other = tested.world.Actors()[3];
        other.alive = true;
        other.byte88 = 1;
        other.type = probe.type;
        other.dying = probe.dying;
        other.movement = probe.movement;
        if (probe.dying != 0)
            other.animation.action = 0x3d;
        other.byte381 = probe.dying != 0 ? FrameSkillStep : 0;
        other.byte38e = FrameSkill;

        const auto stepped = tested.Step(Everything);

        if (probe.cut.empty())
            EXPECT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
        else
            EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        EXPECT_EQ(tested.random.State(), RandomAfter(probe.draws).State());
    }
}

// An open count is not encrypted again on close: neither in the TerrainWall, nor in 0x508ba0, nor in 0x505de0's cell.
// The other keeps the rand() % 6, the head and the rand() & 7, and then the hero takes its step.

TEST(CharacterStepTest, AnotherCharacterWithOpenCellAccessesSpendsNothingOnThem)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << map);
        GameWorld tested(map);
        ActorState& other = tested.Other(3);
        other.cellXAccesses = 1;
        other.cellYAccesses = 1;
        CrtRandom expected(Seed);
        Skip(expected, WallDraws + BodyDraws);
        CharacterHead head;
        StepCharacterHead(head, true, expected);
        Skip(expected, EffectsRandomDraws + OwnRecordDraws + BodyDraws + SeenEffectsDraws);

        const auto stepped = tested.Step(Everything);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(other.cellXAccesses, 1);
        EXPECT_EQ(other.cellYAccesses, 1);
        EXPECT_EQ(tested.random.State(), expected.State());
    }
}

// The selection (0x6342f9) and the login (0x634d13) on their repo map, with the map's .att in the TerrainWall global
// (0x7d0d348, which 0x61a560 overwrites), the current world and the hero published in `hero`. The live ones are
// standing players in their slots, with both cell copies at (0, 0), as 0x511290 leaves them from 0x517270 or 0x6349a0,
// and +0x88 at 1. The own record stays as the login leaves it, zeroed (0x4baf79..0x4bafc2): its equipment is type 0,
// and 0x54efe0 reads its wings' +0x2d in Item.bmd (`items`).
struct SceneCharacters
{
    SceneCharacters(const std::filesystem::path& dataRoot, std::uint8_t map, std::size_t hero)
    {
        auto loaded = Client::Terrain::LoadWorldTerrain(dataRoot, map);
        if (!loaded.HasValue())
        {
            ADD_FAILURE() << loaded.Error();
            return;
        }
        terrain = std::move(loaded).Value();
        world.Runtime().map = map;
        world.Runtime().hero = hero;
    }

    ActorState& Place(std::size_t slot, const glm::vec3& position)
    {
        ActorState& actor = world.Actors()[slot];
        actor = Client::Tests::UnarmedPlayer(1);
        actor.alive = true;
        actor.byte88 = 1;
        actor.pk = 3;
        actor.position = position;
        return actor;
    }

    // 0x4c20a0's five slots, with 0x634680's z.
    void PlaceTheFiveSlots()
    {
        for (std::size_t slot = 0; slot < Client::FrontEnd::CharacterSlots.size(); ++slot)
        {
            const glm::vec2 xy = Client::FrontEnd::CharacterSlots[slot].position;
            Place(slot, glm::vec3(xy.x, xy.y, Client::FrontEnd::CharacterSlotHeight));
        }
    }

    Client::Core::Expected<void, std::string> Step(SceneId scene)
    {
        return StepCharacters(CharacterStepContext{.world = world,
                                                   .random = random,
                                                   .scene = scene,
                                                   .playerSkeleton = &skeleton,
                                                   .items = items,
                                                   .terrain = &terrain,
                                                   .trapezoid = &trapezoid,
                                                   .hooks = &StepHooks});
    }

    WorldState world;
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random{Seed};
    Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    const Client::Tables::ItemTable* items = nullptr;
    TerrainPolygon trapezoid{};
};

// The trapezoid the selection draw leaves (0x63475c), the one of its fixed point.
TerrainPolygon SelectionPolygon()
{
    return Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::CharacterSelect, SelectionMap,
                                             Client::Camera::SelectionCullingPoint,
                                             Client::Camera::SelectionCamera().angles.z);
}

// The selection with the five slots and the hero in 0, with its draw's trapezoid, step by step: all five are seen
// (P-C46 b, with MuRet's count) and mark their logical cell, (0, 0), with 8 rand() each. The hero leaves 0x508ba0
// without spending (0x508bad) and takes the game path: its body with the own record and its whole 0x505de0. Slots 1
// to 4 spend their 0x508ba0, body, cell, head (1 or 3) and the rand() & 7. Per step, 40 + 3858 + 4 · 19 = 3974, plus
// 2 per head that draws (*simulated*). The test skeleton has no speed: the animation does not advance, without
// 0x432bf1's rand() from frame 3 or the suffix's at the action's end.

TEST_F(GoldenCharacterStep, TheSelectionStepsItsFiveSlots)
{
    constexpr int Steps = 64;
    const auto items = LoadItems();
    ASSERT_TRUE(items.has_value());
    SceneCharacters tested(m_dataRoot, SelectionMap, 0);
    tested.items = &items->table;
    tested.PlaceTheFiveSlots();
    tested.trapezoid = SelectionPolygon();
    CrtRandom expected(Seed);
    std::array<CharacterHead, Client::FrontEnd::CharacterSlots.size()> heads{};
    int sorted = 0;
    for (int step = 1; step <= Steps; ++step)
    {
        SCOPED_TRACE(::testing::Message() << "paso " << step);
        Skip(expected, (5 * WallDraws) + OwnRecordDraws + BodyDraws + SeenEffectsDraws);
        for (std::size_t slot = 1; slot < heads.size(); ++slot)
        {
            sorted += DrawASeenOther(expected, heads[slot]) ? 1 : 0;
        }

        const auto stepped = tested.Step(SceneId::CharacterSelect);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_EQ(tested.random.State(), expected.State());
        for (std::size_t slot = 0; slot < heads.size(); ++slot)
        {
            ASSERT_TRUE(tested.world.Actors()[slot].seen) << "lugar " << slot;
            ASSERT_EQ(tested.world.Actors()[slot].headTurn.target, heads[slot].target) << "lugar " << slot;
        }
    }
    EXPECT_GT(sorted, 0);
    const std::uint16_t origin = tested.terrain.attributes->cells[0];
    EXPECT_EQ(origin & Client::Pathfinding::CharacterWallBit, Client::Pathfinding::CharacterWallBit);
}

// Without Item.bmd, the selection hero stops at the type 0 wings of its zeroed record (0x54efe0, from 0x4fc450), after
// the TerrainWalls and closing the own record.

TEST_F(GoldenCharacterStep, WithoutItemBmdTheSelectionHeroStopsAtItsWings)
{
    SceneCharacters tested(m_dataRoot, SelectionMap, 0);
    tested.PlaceTheFiveSlots();
    tested.trapezoid = SelectionPolygon();

    const auto stepped = tested.Step(SceneId::CharacterSelect);

    ASSERT_TRUE(HasError(stepped, "0x54efe0 (0x54f30e)")) << ErrorOf(stepped);
    EXPECT_EQ(tested.random.State(), RandomAfter((5 * WallDraws) + OwnRecordDraws).State());
}

// After the return, the published hero stays in the game slot (Q-C213), which 0x510f70(−1) killed: the five are
// others, without the own record, and each draws its TerrainWall, its 0x508ba0, its body, its cell, its head and the
// rand() & 7: 40 + 5 · 19 = 135 per step, plus 2 per head that draws (*simulated*), with the animation still.

TEST_F(GoldenCharacterStep, AfterTheReturnTheFiveSlotsAreOthers)
{
    constexpr int Steps = 64;
    SceneCharacters tested(m_dataRoot, SelectionMap, 42);
    tested.PlaceTheFiveSlots();
    tested.trapezoid = SelectionPolygon();
    CrtRandom expected(Seed);
    std::array<CharacterHead, Client::FrontEnd::CharacterSlots.size()> heads{};
    int sorted = 0;
    for (int step = 1; step <= Steps; ++step)
    {
        SCOPED_TRACE(::testing::Message() << "paso " << step);
        Skip(expected, 5 * WallDraws);
        for (CharacterHead& head : heads)
        {
            sorted += DrawASeenOther(expected, head) ? 1 : 0;
        }

        const auto stepped = tested.Step(SceneId::CharacterSelect);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_EQ(tested.random.State(), expected.State());
        for (std::size_t slot = 0; slot < heads.size(); ++slot)
        {
            ASSERT_EQ(tested.world.Actors()[slot].headTurn.angles, heads[slot].angles) << "lugar " << slot;
        }
    }
    EXPECT_GT(sorted, 0);
}

// The login with the slot 2 character in its place (0x6349a0, LoginCharacterTransform), with the scepter and the raven
// in its hands (no light) and in its standing action, and the slot 0 hero not built, from the first step's .bss
// trapezoid (Q-C217): per step, the TerrainWall's 8 rand(), 8 of its 0x508ba0, its body's, 8 of its cell, its head and
// the rand() & 7: 27, and 29 when the head draws (*simulated*), with the animation still (no 0x432bf1 rand() from
// frame 3). The head is the one the login runs on its own (LoginScene, 06 § Lo demás del login).

TEST_F(GoldenCharacterStep, TheLoginStepsItsCharacter)
{
    constexpr int Steps = 200;
    SceneCharacters tested(m_dataRoot, LoginMap, 0);
    ActorState& login = tested.Place(2, Client::FrontEnd::LoginCharacterTransform().position);
    login.hands[0].model = LoginScepter;
    login.hands[1].model = Client::FrontEnd::LoginRavenItem;
    login.animation.action = Client::FrontEnd::LoginCharacterStandingAction;
    CrtRandom expected(Seed);
    CharacterHead head;
    int sorted = 0;
    for (int step = 1; step <= Steps; ++step)
    {
        SCOPED_TRACE(::testing::Message() << "paso " << step);
        Skip(expected, WallDraws);
        sorted += DrawASeenOther(expected, head) ? 1 : 0;

        const auto stepped = tested.Step(SceneId::Login);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_TRUE(login.seen);
        ASSERT_EQ(tested.random.State(), expected.State());
        ASSERT_EQ(login.headTurn.angles, head.angles);
        ASSERT_EQ(login.headTurn.target, head.target);
    }
    EXPECT_GT(sorted, 0);
    EXPECT_FALSE(tested.world.Actors()[0].alive);
}

// Hero alone on a game map (0 and 2) in the selection or login scene: the whole body and 0x505de0, with the same
// rand().

TEST(CharacterStepTest, OutsideTheGameTheHeroTakesTheSamePath)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const SceneId scene : {SceneId::CharacterSelect, SceneId::Login})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", " << Client::Kernel::SceneName(scene));
            GameWorld tested(map);
            tested.Hero().safeZone = 1;

            const auto stepped = tested.Step(Everything, scene);

            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(tested.Hero().safeZone, 0);
            EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
        }
    }
}

// 0x506cf8: map 7's 0x607 is only in scene 5; in the others map 7 goes straight on. Without the particle pool, the
// bubble stops.

TEST(CharacterStepTest, TheClockEffectOfMapSevenIsOnlyInTheGame)
{
    GameWorld game(7);
    EXPECT_TRUE(HasError(game.Step(Everything), "0x506d77"));
    GameWorld selection(7);
    const auto stepped = selection.Step(Everything, SceneId::CharacterSelect);
    EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(selection.random.State(), RandomAfter(SeenHeroDraws).State());
}

// 0x506d0a..0x506d7c: in the game, on map 7, the second of every ten of the world clock (ftol % 10000 below 1000)
// blows the 0x607/0 bubble (two rand()) from the head bone (20) of the last draw's bones: (0, 20, −10) times the
// scale, plus the position, with the character's angles and light at 1. Without a draw yet, from the position (Q-C318).

TEST(CharacterStepTest, AtlansBlowsABubbleFromTheHeadOneSecondInTen)
{
    struct Case
    {
        float clock;
        bool drawn;
        bool blows;
    };
    for (const Case& probe : {Case{0.0f, true, true}, Case{999.9f, true, true}, Case{1000.0f, true, false},
                              Case{10500.0f, true, true}, Case{19999.0f, true, false}, Case{500.0f, false, true}})
    {
        SCOPED_TRACE(::testing::Message() << "reloj " << probe.clock << (probe.drawn ? ", dibujado" : ", sin dibujo"));
        GameWorld tested(7);
        ActorState& hero = tested.Hero();
        hero.scale = 2.0f;
        hero.angles = {0.0f, 0.0f, 45.0f};
        if (probe.drawn)
        {
            hero.bodyPose.assign(Client::Tables::PlayerHeadBone + 1, Client::Math::BoneTransform(0.0f));
            Client::Math::BoneTransform& head = hero.bodyPose[Client::Tables::PlayerHeadBone];
            head = Client::Math::BoneTransform(1.0f);
            head[3] = glm::vec3(5.0f, 6.0f, 7.0f);
        }
        tested.trapezoid = Everything;
        Client::Particles::Particles particles;

        const auto stepped = StepCharacters(CharacterStepContext{.world = tested.world,
                                                                 .random = tested.random,
                                                                 .scene = SceneId::Game,
                                                                 .playerSkeleton = tested.player,
                                                                 .terrain = &tested.terrain,
                                                                 .particles = &particles,
                                                                 .worldClock = probe.clock,
                                                                 .trapezoid = &tested.trapezoid,
                                                                 .dynamicLighting = &tested.light.Lighting(),
                                                                 .faunaModels = &tested.faunaModels,
                                                                 .hooks = &StepHooks});

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const Client::Particles::Particle& bubble = particles.All()[0];
        EXPECT_EQ(bubble.alive, probe.blows);
        EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws + (probe.blows ? 2 : 0)).State());
        if (!probe.blows)
            continue;
        EXPECT_EQ(bubble.type, Client::Particles::DropParticle);
        EXPECT_EQ(bubble.subtype, 0);
        const glm::vec3 local = probe.drawn ? glm::vec3(10.0f, 52.0f, -6.0f) : glm::vec3(0.0f);
        EXPECT_EQ(bubble.position, hero.position + local);
        EXPECT_EQ(bubble.angles, hero.angles);
        EXPECT_EQ(bubble.light, glm::vec3(1.0f));
    }
}

// 0x506d7f..0x506e54: on map 9, in any scene, two rand() & 3; at 0, a 0x68/0 drop from bone 26 (the first) and 35
// (the second) of the last draw: (−15, 0, 0) plus the bone's translation, times the scale, plus the position, with
// the character's angles and light at 1. Without the pool it only stops if it had to create one.

TEST(CharacterStepTest, DevilSquareRainsFromTwoBonesOfEachCharacter)
{
    struct Drop
    {
        std::size_t bone;
        glm::vec3 local;
        std::string_view address;
    };
    constexpr std::array<Drop, 2> Drops{Drop{26, {-20.0f, 12.0f, 14.0f}, "0x506dff"},
                                        Drop{35, {-28.0f, 4.0f, 6.0f}, "0x506e4f"}};
    std::array<int, 3> rained{};
    for (std::uint32_t seed = 1; seed <= 24; ++seed)
    {
        for (const bool pooled : {true, false})
        {
            SCOPED_TRACE(::testing::Message() << "semilla " << seed << (pooled ? ", con pool" : ", sin pool"));
            GameWorld tested(9);
            tested.random = CrtRandom(seed);
            ActorState& hero = tested.Hero();
            hero.scale = 2.0f;
            hero.angles = {0.0f, 0.0f, 45.0f};
            hero.bodyPose.assign(36, Client::Math::BoneTransform(0.0f));
            hero.bodyPose[26] = Client::Math::BoneTransform(1.0f);
            hero.bodyPose[26][3] = glm::vec3(5.0f, 6.0f, 7.0f);
            hero.bodyPose[35] = Client::Math::BoneTransform(1.0f);
            hero.bodyPose[35][3] = glm::vec3(1.0f, 2.0f, 3.0f);
            tested.trapezoid = Everything;
            Client::Particles::Particles particles;

            const auto stepped = StepCharacters(CharacterStepContext{.world = tested.world,
                                                                     .random = tested.random,
                                                                     .scene = SceneId::Game,
                                                                     .playerSkeleton = tested.player,
                                                                     .terrain = &tested.terrain,
                                                                     .particles = pooled ? &particles : nullptr,
                                                                     .trapezoid = &tested.trapezoid,
                                                                     .dynamicLighting = &tested.light.Lighting(),
                                                                     .faunaModels = &tested.faunaModels,
                                                                     .hooks = &StepHooks});

            CrtRandom oracle(seed);
            Skip(oracle, SeenHeroDraws);
            Client::Particles::Particles expected;
            std::string_view cut;
            int drops = 0;
            for (const Drop& drop : Drops)
            {
                if (oracle.Next() % 4 != 0)
                    continue;
                ++drops;
                if (!pooled)
                {
                    cut = drop.address;
                    break;
                }
                ASSERT_TRUE(expected
                                .CreateChecked(0x68, hero.position + drop.local, hero.angles, glm::vec3(1.0f), 0, 1.0f,
                                               0.0f, oracle, 0)
                                .HasValue());
            }
            EXPECT_EQ(tested.random.State(), oracle.State());
            if (!cut.empty())
            {
                EXPECT_TRUE(HasError(stepped, cut)) << ErrorOf(stepped);
                continue;
            }
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            if (pooled)
                ++rained[static_cast<std::size_t>(drops)];
            for (std::size_t index = 0; index < 2; ++index)
            {
                const Client::Particles::Particle& made = particles.All()[index];
                const Client::Particles::Particle& wanted = expected.All()[index];
                EXPECT_EQ(made.alive, wanted.alive);
                if (!wanted.alive)
                    continue;
                EXPECT_EQ(made.type, 0x68);
                EXPECT_EQ(made.subtype, 0);
                EXPECT_EQ(made.position, wanted.position);
                EXPECT_EQ(made.angles, hero.angles);
                EXPECT_EQ(made.light, glm::vec3(1.0f));
                EXPECT_EQ(made.life, wanted.life);
                EXPECT_EQ(made.velocity, wanted.velocity);
            }
        }
    }
    EXPECT_GT(rained[0], 0);
    EXPECT_GT(rained[1], 0);
}

// In the selection (scene 4) 0x4ec200 decides the end with frame + 2 (Q-C21): the 6-key action at 1 per step ends at
// frame 4 and goes back to standing, with 0x4f2250's suffix rand(); in the game, it does not.

TEST(CharacterStepTest, TheSelectionEndsTheActionTwoFramesEarlier)
{
    for (const SceneId scene : {SceneId::Game, SceneId::CharacterSelect})
    {
        SCOPED_TRACE(Client::Kernel::SceneName(scene));
        GameWorld tested;
        tested.skeleton = SkeletonWith(3, 6, 1.0f);
        ActorState& hero = tested.Hero();
        hero.animation.action = 3;
        hero.animation.frame = 3.0f;
        tested.Cell(0, 0) = 1;

        const auto stepped = tested.Step(Everything, scene);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const bool ended = scene == SceneId::CharacterSelect;
        EXPECT_EQ(hero.animation.action, ended ? 1 : 3);
        EXPECT_EQ(hero.animation.frame, ended ? 0.0f : 4.0f);
        const int draws = SeenHeroDraws + (ended ? StopDraws : TrailDraws);
        EXPECT_EQ(tested.random.State(), RandomAfter(draws).State());
    }
}
} // namespace

TEST(CharacterStepTest, KalimaFootstepCallerCreatesSubtypeOneInTheSharedPool)
{
    GameWorld tested(24);
    tested.Hero().animation.action = 0x15;
    tested.trapezoid = Everything;
    Client::Particles::Particles particles;
    const auto stepped = StepCharacters({.world = tested.world,
                                         .random = tested.random,
                                         .playerSkeleton = tested.player,
                                         .terrain = &tested.terrain,
                                         .particles = &particles,
                                         .trapezoid = &tested.trapezoid,
                                         .dynamicLighting = &tested.light.Lighting(),
                                         .faunaModels = &tested.faunaModels,
                                         .hooks = &StepHooks});
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const auto& p = particles.All()[0];
    ASSERT_TRUE(p.alive);
    EXPECT_EQ(p.type, 0x6f4);
    EXPECT_EQ(p.subtype, 1);
    EXPECT_EQ(p.life, 20);
    EXPECT_EQ(p.light, glm::vec3(0.3f));
    EXPECT_EQ(p.position.z, tested.Hero().position.z + 100);
    EXPECT_EQ(p.start.z, tested.Hero().position.z + 50);
    EXPECT_EQ(p.scale, 1);
    EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws + 3).State());
}

TEST(CharacterStepTest, ThePerchedRavenUsesItsOwnersPlacementBeforeDeathMovesTheActor)
{
    GameWorld tested;
    tested.Hero().alive = false;
    ActorState& first = tested.Other(2, 0, 0);
    first.position = {1000.0f, 2000.0f, 300.0f};
    first.scale = 1.5f;
    ActorState& owner = tested.Other(5, 0, 0);
    owner.position = {10603.0f, 14112.0f, 580.0f};
    owner.scale = 1.25f;
    owner.dying = 1;
    owner.dyingTime = 0.98f;
    owner.angles = {3.0f, 0.0f, 188.0f};
    owner.safeZone = 1;
    tested.Cell(0, 0) = 1;
    owner.bodyPose.assign(60, Client::Math::BoneTransform(1.0f));
    owner.bodyPose[37][3] = glm::vec3(7.0f, 8.0f, 9.0f);
    owner.darkSpirit.emplace();
    owner.darkSpirit->state = 3;
    Skeleton raven;
    raven.actions.resize(4);
    Client::Particles::Particles particles;
    tested.trapezoid = Everything;

    const auto stepped = StepCharacters({.world = tested.world,
                                         .random = tested.random,
                                         .playerSkeleton = tested.player,
                                         .terrain = &tested.terrain,
                                         .darkSpiritSkeleton = &raven,
                                         .particles = &particles,
                                         .trapezoid = &tested.trapezoid,
                                         .dynamicLighting = &tested.light.Lighting(),
                                         .faunaModels = &tested.faunaModels,
                                         .hooks = &StepHooks});

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(owner.darkSpirit->state, 3);
    EXPECT_FLOAT_EQ(owner.position.z, 579.6f);
    // 1.25 · ((7, 8, 9) + (−10, 0, 10)) + (10603, 14112, 580).
    EXPECT_EQ(owner.darkSpirit->position, glm::vec3(10599.25f, 14122.0f, 603.75f));
    EXPECT_EQ(owner.darkSpirit->angles, glm::vec3(3.0f, 0.0f, 68.0f));
    // The later player publishes its model after the owner.
    EXPECT_EQ(tested.world.PlayerModel().scale, 1.25f);
    EXPECT_EQ(tested.world.PlayerModel().origin, owner.position);
}

TEST(CharacterStepTest, TheBodyCountsDownTheRemovalWordAndKeepsItsMinus128Sentinel)
{
    struct Case
    {
        std::int32_t before;
        std::int32_t after;
        bool removed;
    };
    constexpr std::array Cases{Case{150, 149, false},  Case{2, 1, false},
                               Case{1, -128, true},    Case{0, -128, true},
                               Case{-1, -128, true},   Case{-127, -128, true},
                               Case{-129, -128, true}, Case{std::numeric_limits<std::int32_t>::min(), -128, true},
                               Case{-128, -128, false}};
    for (const Case& probe : Cases)
    {
        SCOPED_TRACE(probe.before);
        GameWorld tested;
        ActorState& actor = tested.Other(1);
        actor.dword428 = probe.before;

        const auto stepped = tested.Step(Everything);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(actor.dword428, probe.after);
        EXPECT_EQ(actor.alive, !probe.removed);
    }
}

// The counter drops per slot, not per key: other duplicates of the key and their effects stay alive.

TEST(CharacterStepTest, ExpiringOneActorCleansOnlyItsOwnEffectsAndLetsTheBodyFinish)
{
    GameWorld tested;
    ActorState& expired = tested.Other(1);
    expired.key = 77;
    expired.dword428 = 1;
    expired.dying = 1;
    expired.flags78 = 0x18000;
    expired.darkSpirit.emplace();
    expired.darkSpirit->state = 2;
    expired.byte360 = 5;
    expired.layer.emplace();
    ActorState& duplicate = tested.Other(2);
    duplicate.key = 77;
    tested.world.Helpers()[0] = {.alive = true, .type = 0xbf, .rider = 1};
    tested.world.Helpers()[1] = {.alive = true, .type = 0xbf, .rider = 2};
    tested.world.Runtime().namedBones = {{1, "RightHand", 3}, {2, "LeftHand", 4}};

    const auto stepped = tested.Step(Everything);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(expired.alive);
    EXPECT_EQ(expired.dword428, -128);
    EXPECT_FALSE(expired.darkSpirit.has_value());
    EXPECT_FALSE(expired.layer.has_value());
    EXPECT_EQ(expired.byte360, 0);
    EXPECT_EQ(expired.flags78 & 0x18000u, 0u);
    EXPECT_TRUE(duplicate.alive);
    EXPECT_FALSE(tested.world.Helpers()[0].alive);
    EXPECT_TRUE(tested.world.Helpers()[1].alive);
    ASSERT_EQ(tested.world.Runtime().namedBones.size(), 1u);
    EXPECT_EQ(tested.world.Runtime().namedBones[0].actor, 2u);
    EXPECT_EQ(tested.world.Runtime().namedBones[0].name, "LeftHand");
}

TEST(CharacterStepTest, AnExpiringActorOutsideThePoolStillRunsItsDirectCleanup)
{
    GameWorld tested;
    ActorState actor = Client::Tests::UnarmedPlayer(1);
    actor.alive = true;
    actor.dword428 = 1;
    actor.darkSpirit.emplace();
    actor.darkSpirit->state = 2;

    const auto stepped = StepCharacter(actor, {.world = tested.world,
                                               .random = tested.random,
                                               .scene = SceneId::Login,
                                               .playerSkeleton = tested.player,
                                               .terrain = &tested.terrain,
                                               .hooks = &StepHooks});

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(actor.alive);
    EXPECT_EQ(actor.dword428, Client::Kernel::ActorRemovalCountdownSentinel);
    EXPECT_FALSE(actor.darkSpirit.has_value());
}

// 0x416add: states 2, 5 and 6 belong to the A7 orders (CL9) and stop before touching the raven.

TEST(CharacterStepTest, DyingActorsReleaseTheirClothsAtTheFadeStart)
{
    GameWorld tested;
    std::array<Client::Math::BoneTransform, 21> bones{};
    const Client::Models::ClothBody body{.bones = bones};
    Client::Models::ClothWorld clothWorld{.random = CrtRandom(Seed)};
    for (const std::size_t slot : {GameWorld::HeroSlot, std::size_t{1}})
    {
        ActorState& actor = slot == GameWorld::HeroSlot ? tested.Hero() : tested.Other(slot);
        actor.dying = 1;
        actor.dyingTime = 0.98f;
        auto& cloth = tested.world.ActorCloths()[slot];
        Client::Actors::Drawing::ClothWearer wearer;
        wearer.characterClass = 4;
        Client::Render::ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        cloth.Frame(batch, wearer, body, clothWorld, 0, 0.0f, true);
        ASSERT_FALSE(cloth.Cloths().empty()) << "slot " << slot;
    }

    ASSERT_TRUE(tested.Step(Everything).HasValue());

    for (const std::size_t slot : {GameWorld::HeroSlot, std::size_t{1}})
    {
        EXPECT_FLOAT_EQ(tested.world.Actors()[slot].dyingTime, 1.0f);
        EXPECT_TRUE(tested.world.Actors()[slot].alive);
        EXPECT_TRUE(tested.world.ActorCloths()[slot].Cloths().empty()) << "slot " << slot;
    }

    ASSERT_TRUE(tested.Step(Everything).HasValue());
    for (const std::size_t slot : {GameWorld::HeroSlot, std::size_t{1}})
    {
        EXPECT_TRUE(tested.world.ActorCloths()[slot].Cloths().empty()) << "slot " << slot;
    }
}

// 0x4fbad6..0x4fbb08 and 0x401040: dropping below 0.01 erases the non-hero record and its named bones, without
// touching the names of the pool's other slots.

TEST(CharacterStepTest, RemovingADyingActorForgetsOnlyItsNamedBones)
{
    GameWorld tested;
    ActorState& removed = tested.Other(1);
    removed.dying = 1;
    removed.dyingTime = 1.98f;
    tested.world.Runtime().namedBones = {{1, "Left_Hand", 3}, {1, "Right_Hand", 4}, {2, "Left_Hand", 3}};

    ASSERT_TRUE(tested.Step(Everything).HasValue());

    EXPECT_FALSE(removed.alive);
    ASSERT_EQ(tested.world.Runtime().namedBones.size(), 1u);
    EXPECT_EQ(tested.world.Runtime().namedBones[0].actor, 2u);
    EXPECT_EQ(tested.world.Runtime().namedBones[0].name, "Left_Hand");
}

// An open count in the protection table is not encrypted again on close: the cell spends nothing, and the own record,
// the body's rand() % 6 and the rand() & 7 remain.

TEST(CharacterStepTest, TheEndOfTheMarkActionsLeavesTheBloodOnce)
{
    constexpr int RemainsDraws = 2 * 5;
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t action : {std::uint8_t{0xc3}, std::uint8_t{0xc4}})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", acción " << static_cast<int>(action));
            GameWorld tested(map);
            tested.skeleton.headBone = 20;
            ActorState& hero = tested.Hero();
            hero.position = {3000.0f, 4000.0f, 10.0f};
            hero.scale = 1.5f;
            hero.flags78 = 0x1000;
            hero.animation.action = action;
            hero.animation.frame = 2.5f;
            hero.light = {0.5f, 0.25f, 0.125f};
            hero.bodyPose.assign(21, Client::Math::BoneTransform(1.0f));
            hero.bodyPose[20][3] = {0.0f, 0.0f, 40.0f};
            tested.Cell(0, 0) = 1;
            tested.world.PlayerModel() = {.scale = 0.75f, .origin = {-3000.0f, -4000.0f, -10.0f}};

            const auto stepped = tested.Step(Everything);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(hero.animation.action, action);
            EXPECT_EQ(hero.byte38a, 1);
            const auto& marks = tested.marks;
            CrtRandom draws = RandomAfter(WallDraws + OwnRecordDraws);
            for (std::size_t slot = 0; slot < 2; ++slot)
            {
                SCOPED_TRACE(slot);
                const int x = draws.Next() % 100 - 50;
                const int y = draws.Next() % 100 - 50;
                const auto size =
                    static_cast<float>(static_cast<double>((draws.Next() & 3) + 8) * static_cast<double>(0.1f));
                const auto angle = static_cast<float>(draws.Next() % 360);
                const std::int32_t life = draws.Next() % 32 + 50;
                ASSERT_TRUE(marks[slot].live);
                EXPECT_EQ(marks[slot].type, 0x5e3);
                EXPECT_EQ(marks[slot].position, (glm::vec3{static_cast<float>(x) * 1.5f + 3000.0f,
                                                           static_cast<float>(y) * 1.5f + 4000.0f, 70.0f}));
                EXPECT_EQ(marks[slot].size, size);
                EXPECT_EQ(marks[slot].angle, angle);
                EXPECT_EQ(marks[slot].color, hero.light);
                EXPECT_EQ(marks[slot].life, life);
            }
            EXPECT_FALSE(marks[2].live);
            EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws + RemainsDraws).State());

            const CrtRandom before = tested.random;
            ASSERT_TRUE(tested.Step(Everything).HasValue());
            EXPECT_FALSE(marks[2].live);
            CrtRandom again = before;
            for (int draw = 0; draw < SeenHeroDraws; ++draw)
                static_cast<void>(again.Next());
            EXPECT_EQ(tested.random.State(), again.State());
        }
    }
}

// 0x4fcb71: the ended 0x44 stays and goes back to frame 0; then 0x505de0 stops in its effects.

TEST(CharacterStepTest, TheTurnedNpcTurnsAndDrawsItsLine)
{
    GameWorld tested;
    tested.Hero().byte88 = 4;
    tested.Hero().subtype = 0xd2;
    tested.Hero().angles = {1.0f, 2.0f, 3.0f};

    const auto stepped = tested.Step(Everything);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.Hero().angles, glm::vec3(0.0f, 0.0f, 90.0f));
    EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws + 1).State());
}

// 0x4fbe10 on maps 18 to 23, sending nothing: with another cell it stores the hero's and the world clock, and opens
// and closes X twice and Y once (12 rand()); with the same one and under 10 s, it touches nothing and reads both (8);
// with 10 s on a cell without bit 8, it stores the clock. The rest of the step is map 17's.
