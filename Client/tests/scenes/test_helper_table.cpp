#include "Models/IModelLibrary.h"
#include "Kernel/HelperTable.h"
#include "Pets/HelperTable.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Kernel/JointRequest.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Math/HalfLifeMath.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Math/RoundedMath.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Pets/DarkHorse.h"
#include "Actors/Drawing/PosePublishing.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Pets/PetHelperSteps.h"
#include "Kernel/SceneId.h"
#include "Kernel/GameContext.h"
#include "Kernel/WorldState.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"

#include "support/GoldenDir.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Pets/HelperTable.h: the game's helper table (0x7d335d0) with the dark horse (0xbf) and the pets: 0x443fa0 →
// 0x443d70, 0x443d40, the step of scenes 4 and 5 (0x445230 → 0x444010) and its draw (0x4453b0 → 0x445260). 06 § El
// caballo oscuro and § Las mascotas.
namespace
{
using Client::Crt::CrtRandom;
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Kernel::ActorPoolSize;
using Client::Kernel::ActorState;
using Client::Kernel::HelperCount;
using Client::Kernel::HelperState;
using Client::Kernel::JointState;
using Client::Kernel::SceneId;
using Client::Kernel::TurnOffHelpersOf;
using Client::Kernel::WorldState;
using Client::Math::RoundedCos;
using Client::Math::RoundedSin;
using Client::Models::ActionInfo;
using Client::Models::BoneKey;
using Client::Models::IModelLibrary;
using Client::Models::Skeleton;
using Client::Models::SkeletonBone;
using Client::Pets::ConstructHelper;
using Client::Pets::CreateHelper;
using Client::Pets::DarkHorseModel;
using Client::Pets::DinorantModel;
using Client::Pets::DrawHelpers;
using Client::Pets::DrawTheHelperOutsideTheTable;
using Client::Pets::GuardianAngelModel;
using Client::Pets::HelperDrawContext;
using Client::Pets::HelperStepContext;
using Client::Pets::StepHelpers;
using Client::Pets::StepTheHelperOutsideTheTable;
using Client::Pets::UniriaModel;
using Client::Render::ModelBatch;
using Client::Render::TextureSlot;
using Client::Terrain::TerrainPolygon;

// The CRT rand() seeds, by their first rand(): with QuietSeed, % 3 gives 2 and % 2 gives 1 (no breath, no dust); with
// BreathSeed, % 3 gives 0; with DustSeed, % 2 gives 0.
constexpr std::uint32_t QuietSeed = 1;
constexpr std::uint32_t BreathSeed = 2;
constexpr std::uint32_t DustSeed = 3;
constexpr std::size_t HeroSlot = 42;
constexpr std::size_t OtherSlot = 7;
// The rider actions (+0x111) that 0x444010 looks at.
constexpr std::uint8_t StandingRider = 0x4a;
constexpr std::uint8_t GallopingRider = 0x4b;
constexpr std::uint8_t RearingRider = 0x53;
constexpr std::uint8_t GestureRider = 0x54;

int FirstDraw(std::uint32_t seed)
{
    CrtRandom random(seed);
    return random.Next();
}

CrtRandom RandomAfter(std::uint32_t seed, int draws)
{
    CrtRandom random(seed);
    for (int i = 0; i < draws; ++i)
    {
        static_cast<void>(random.Next());
    }
    return random;
}

glm::vec3 CellCentre(std::uint8_t x, std::uint8_t y)
{
    return {(static_cast<float>(x) * 100.0f) + 50.0f, (static_cast<float>(y) * 100.0f) + 50.0f, 170.0f};
}

bool HasError(const Client::Core::Expected<void, std::string>& result, std::string_view address)
{
    return !result.HasValue() && result.Error().find(address) != std::string::npos;
}

// DarkHorse.bmd's seven actions (or as many), 10 keys each, on `boneCount` bones: 0 at the origin and the others
// hanging from it, each at its number of units in z.
Skeleton HorseSkeleton(std::size_t boneCount = 1, std::size_t actionCount = 7)
{
    Skeleton skeleton;
    for (std::size_t index = 0; index < boneCount; ++index)
    {
        SkeletonBone bone;
        bone.parent = index == 0 ? std::int16_t{-1} : std::int16_t{0};
        bone.actions.assign(actionCount, std::vector<BoneKey>(10, BoneKey{}));
        for (std::vector<BoneKey>& keys : bone.actions)
        {
            for (BoneKey& key : keys)
            {
                key.position = {0.0f, 0.0f, static_cast<float>(index)};
                key.rotation = Client::Math::AngleQuaternion({0.0f, 0.0f, 0.0f});
            }
        }
        skeleton.bones.push_back(bone);
    }
    skeleton.actions.assign(actionCount,
                            ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 10, .lockPositions = false});
    return skeleton;
}

// The pets 0x444010 moves besides the horse.
constexpr std::array<std::uint16_t, 6> PetModels{UniriaModel, DinorantModel, 0x14a, 0x14b, 0x14c, GuardianAngelModel};
// The Uniria and the Dinorant reach action 7 (0x44498b).
constexpr std::size_t PetActions = 8;

bool IsPet(std::uint16_t model)
{
    return std::find(PetModels.begin(), PetModels.end(), model) != PetModels.end();
}

// The horse as a model of one mesh with one triangle, hanging from bone 0.
class HorseModels final : public IModelLibrary
{
public:
    explicit HorseModels(std::size_t boneCount = 1)
        : m_skeleton(HorseSkeleton(boneCount)), m_petSkeleton(HorseSkeleton(1, PetActions))
    {
        Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "dkhorse.jpg";
        m_model.meshes = {mesh};
    }

    [[nodiscard]] const Model* Find(Client::Models::ModelId model) const override
    {
        return model.Value() == DarkHorseModel || IsPet(model.Value()) ? &m_model : nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        if (model.Value() == DarkHorseModel)
        {
            return &m_skeleton;
        }
        return IsPet(model.Value()) ? &m_petSkeleton : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(Client::Models::ModelId model) const override
    {
        return model.Value() == DarkHorseModel || IsPet(model.Value()) ? std::span<const TextureSlot>(m_slots)
                                                                       : std::span<const TextureSlot>{};
    }

private:
    Skeleton m_skeleton;
    Skeleton m_petSkeleton;
    Model m_model;
    std::array<TextureSlot, 1> m_slots{TextureSlot{0x3bc}};
};

// The hero alive in HeroSlot, as 0x511290 leaves it (+0x88 at 1), on cell (100, 100) without attributes, and the
// helper table empty.
class HelperWorld
{
private:
    // Several scenarios live in one test: the durable pool must not exhaust the Windows stack.
    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();

public:
    explicit HelperWorld(std::int32_t map = 0, std::uint32_t seed = QuietSeed) : random(seed)
    {
        world.Runtime().map = map;
        world.Runtime().hero = HeroSlot;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        Rider() = Client::Tests::UnarmedPlayer(1);
        Rider().alive = true;
        Rider().byte88 = 1;
        Rider().position = CellCentre(100, 100);
        Rider().angles = {0.0f, 0.0f, 45.0f};
        Rider().animation.action = StandingRider;
    }

    HelperWorld(const HelperWorld&) = delete;
    HelperWorld& operator=(const HelperWorld&) = delete;
    HelperWorld(HelperWorld&&) = delete;
    HelperWorld& operator=(HelperWorld&&) = delete;

    ActorState& Rider()
    {
        return world.Actors()[HeroSlot];
    }

    HelperState& Horse()
    {
        return world.Helpers()[0];
    }

    std::uint16_t& Cell(std::uint8_t x, std::uint8_t y)
    {
        return terrain.attributes->cells[Client::Data::Terrain::CellIndex(x, y)];
    }

    // 0x443fa0(0xbf, hero + 0x10, hero, 0, 0), as 0x5bc997 calls it.
    void Mount()
    {
        const auto created = CreateHelper(world, DarkHorseModel, Rider().position, HeroSlot);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        ASSERT_EQ(created.Value(), std::optional<std::size_t>{0});
    }

    // 0x443fa0(type, hero + 0x10, hero, 0, 0) with the shared rand().
    void Ride(std::uint16_t type)
    {
        const auto created = CreateHelper(world, type, Rider().position, HeroSlot, &random);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        ASSERT_EQ(created.Value(), std::optional<std::size_t>{0});
    }

    Client::Core::Expected<void, std::string> Step(SceneId scene = SceneId::Game)
    {
        return StepHelpers(HelperStepContext{.world = world,
                                             .models = &models,
                                             .random = random,
                                             .worldClock = clock,
                                             .scene = scene,
                                             .terrain = &terrain});
    }

    // The game step with the logic step count (0x7d3a0f0) and the camera shake (0x7d325c8).
    Client::Core::Expected<void, std::string> StepCounting(std::int32_t logicSteps, float* shake)
    {
        return StepHelpers(HelperStepContext{.world = world,
                                             .models = &models,
                                             .random = random,
                                             .worldClock = clock,
                                             .scene = SceneId::Game,
                                             .terrain = &terrain,
                                             .logicSteps = logicSteps,
                                             .cameraShake = shake});
    }

    // The rider in 0x53 with the horse already in its action 3, at `frame` and with the +0x8e count at `count`.
    void Rear(float frame, std::uint8_t count = 0)
    {
        Rider().animation.action = RearingRider;
        Horse().horse.animation.action = EarthquakeAction;
        Horse().horse.animation.frame = frame;
        Horse().horse.effectCount = count;
    }

    static constexpr std::uint8_t EarthquakeAction = 3;

    // The global pose buffer (0x68b181c) as a draw of 39 bones (or `count`) would leave it: each without rotation, at
    // PosedBone(bone).
    void PoseTheGlobalBones(std::size_t count = PosedBones)
    {
        auto& bones = world.PoseCarry().bones;
        bones.assign(count, Client::Math::BoneTransform(1.0f));
        for (std::size_t bone = 0; bone < count; ++bone)
        {
            bones[bone][3] = PosedBone(bone);
        }
    }

    // The gallop (0x4b) with the horse already in its action 1 and at `frame`.
    void Gallop(float frame = 0.0f)
    {
        Rider().animation.action = GallopingRider;
        Horse().horse.animation.action = 1;
        Horse().horse.animation.frame = frame;
    }

    static constexpr std::size_t PosedBones = 39;

    static glm::vec3 PosedBone(std::size_t bone)
    {
        const auto at = static_cast<float>(bone);
        return {10000.0f + at, 10100.0f + at, 170.0f + at};
    }

    // What the `scene` draw reads: this world's terrain light and the sprite table.
    HelperDrawContext DrawContext(ModelBatch& batch, bool originalFrame = true, SceneId scene = SceneId::Game)
    {
        return HelperDrawContext{.batch = batch,
                                 .models = models,
                                 .world = world,
                                 .random = random,
                                 .frameClock = std::chrono::milliseconds{0},
                                 .stepAlpha = 0.0f,
                                 .originalFrame = originalFrame,
                                 .scene = scene,
                                 .lighting = lighting.get(),
                                 .sprites = &sprites,
                                 .worldClock = clock,
                                 .effects = &world.Map().effects,
                                 .particles = &world.Particles(),
                                 .systemTime = []() { return FenrirHour; },
                                 .globalPose = [this]()
                                 { return std::span<const Client::Math::BoneTransform>(world.PoseCarry().bones); },
                                 .trapezoid = &trapezoid};
    }

    // The timeGetTime() the Fenrir draw sees.
    static constexpr std::uint32_t FenrirHour = 4321;

    Client::Core::Expected<void, std::string> Draw(ModelBatch& batch, const TerrainPolygon& polygon,
                                                   bool originalFrame = true, SceneId scene = SceneId::Game)
    {
        trapezoid = polygon;
        batch.Begin(Client::Render::Pass3D{});
        return DrawHelpers(DrawContext(batch, originalFrame, scene));
    }

    std::vector<Client::Particles::Sprite> LiveSprites()
    {
        std::vector<Client::Particles::Sprite> live;
        for (const Client::Particles::Sprite& sprite : sprites.Sprites())
        {
            if (sprite.alive)
            {
                live.push_back(sprite);
            }
        }
        return live;
    }

    WorldState& world = *m_world;
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random;
    // The world clock (0x5767b88) with which 0x53fe70 turns the 0x5f2: 12345 ms, 105 degrees.
    float clock = 12345.0f;
    HorseModels models;
    // The terrain light (0x5ffce0), (0.25, 0.5, 0.75) on each cell, and the frame sprites (0x5491e0).
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = UniformLighting();
    Client::Particles::FrameSprites sprites;
    // The global trapezoid that 0x5fba90 reads (Camera::CameraState::trapezoid).
    TerrainPolygon trapezoid{};

private:
    static std::unique_ptr<Client::Terrain::TerrainLighting> UniformLighting()
    {
        auto uniform = std::make_unique<Client::Terrain::TerrainLighting>();
        uniform->cells.fill({0.25f, 0.5f, 0.75f});
        return uniform;
    }
};

const TerrainPolygon Everything{.everything = true};

TEST(HelperTableTest, TheSeedsDrawWhatTheTestsNeed)
{
    EXPECT_EQ(FirstDraw(QuietSeed) % 3, 2);
    EXPECT_EQ(FirstDraw(QuietSeed) % 2, 1);
    EXPECT_EQ(FirstDraw(BreathSeed) % 3, 0);
    EXPECT_EQ(FirstDraw(DustSeed) % 2, 0);
}

// 0x443d70 in the first free slot: alive, 0xbf, the rider, the given position, the rider's angles, scale at 1, speed
// at 0.5, alpha at 0, unseen, and the frame and the previous key at 0. No rand(). The next one goes to slot 1.
TEST(HelperTableTest, CreatingFillsTheFirstFreeSlot)
{
    HelperWorld tested;
    const glm::vec3 place{1234.0f, 5678.0f, 90.0f};

    const auto first = CreateHelper(tested.world, DarkHorseModel, place, HeroSlot);
    const auto second = CreateHelper(tested.world, DarkHorseModel, place, HeroSlot);

    ASSERT_TRUE(first.HasValue()) << first.Error();
    ASSERT_TRUE(second.HasValue()) << second.Error();
    EXPECT_EQ(first.Value(), std::optional<std::size_t>{0});
    EXPECT_EQ(second.Value(), std::optional<std::size_t>{1});
    const HelperState& horse = tested.Horse();
    EXPECT_TRUE(horse.alive);
    EXPECT_EQ(horse.type, DarkHorseModel);
    EXPECT_EQ(horse.rider, HeroSlot);
    EXPECT_EQ(horse.position, place);
    EXPECT_EQ(horse.angles, tested.Rider().angles);
    EXPECT_FLOAT_EQ(horse.scale, 1.0f);
    EXPECT_FALSE(horse.seen);
    EXPECT_FLOAT_EQ(horse.horse.speed, 0.5f);
    EXPECT_FLOAT_EQ(horse.horse.alpha, 0.0f);
    EXPECT_FLOAT_EQ(horse.horse.animation.frame, 0.0f);
    EXPECT_EQ(horse.horse.animation.previousKey, 0);
    EXPECT_EQ(tested.random.State(), CrtRandom(QuietSeed).State());
}

// Q-C221: 0x443d70 writes neither the action (+0x111) nor the previous one (+0x112): a reused slot keeps the previous
// horse's. On the rider's safe cell, the first step advances that action.
TEST(HelperTableTest, AReusedSlotKeepsTheActionOfThePreviousHorse)
{
    HelperWorld tested;
    HelperState& old = tested.Horse();
    old.horse.animation.action = 5;
    old.horse.animation.previousAction = 2;
    old.horse.animation.frame = 3.5f;
    old.horse.animation.previousKey = 3;
    old.horse.speed = 1.0f;
    old.horse.alpha = 1.0f;

    ASSERT_NO_FATAL_FAILURE(tested.Mount());

    const HelperState& horse = tested.Horse();
    EXPECT_EQ(horse.horse.animation.action, 5u);
    EXPECT_EQ(horse.horse.animation.previousAction, 2u);
    EXPECT_FLOAT_EQ(horse.horse.animation.frame, 0.0f);
    EXPECT_EQ(horse.horse.animation.previousKey, 0);
    EXPECT_FLOAT_EQ(horse.horse.speed, 0.5f);
    EXPECT_FLOAT_EQ(horse.horse.alpha, 0.0f);

    tested.Cell(100, 100) = 0x0001;
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(tested.Horse().horse.animation.action, 5u);
    EXPECT_FLOAT_EQ(tested.Horse().horse.animation.frame, 0.5f);
}

// 0x443faa..0x443fc7: on maps 18 to 23, or with a rider that is not a player, nothing; with all 10 alive, neither. On
// 17 and 24, yes.
TEST(HelperTableTest, NothingIsCreatedInChaosCastleWithoutAPlayerOrWithAFullTable)
{
    for (const std::int32_t map : {18, 20, 23})
    {
        SCOPED_TRACE(map);
        HelperWorld tested(map);
        const auto created = CreateHelper(tested.world, DarkHorseModel, tested.Rider().position, HeroSlot);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_EQ(created.Value(), std::nullopt);
        EXPECT_FALSE(tested.Horse().alive);
    }
    for (const std::int32_t map : {17, 24})
    {
        SCOPED_TRACE(map);
        HelperWorld tested(map);
        ASSERT_NO_FATAL_FAILURE(tested.Mount());
    }

    HelperWorld monster;
    monster.Rider().type = 0x100;
    const auto withoutPlayer = CreateHelper(monster.world, DarkHorseModel, monster.Rider().position, HeroSlot);
    ASSERT_TRUE(withoutPlayer.HasValue()) << withoutPlayer.Error();
    EXPECT_EQ(withoutPlayer.Value(), std::nullopt);

    HelperWorld full;
    for (HelperState& helper : full.world.Helpers())
    {
        helper.alive = true;
        helper.rider = OtherSlot;
    }
    const auto overflow = CreateHelper(full.world, DarkHorseModel, full.Rider().position, HeroSlot);
    ASSERT_TRUE(overflow.HasValue()) << overflow.Error();
    EXPECT_EQ(overflow.Value(), std::nullopt);
    for (const HelperState& helper : full.world.Helpers())
    {
        EXPECT_EQ(helper.rider, OtherSlot);
    }
}

// A type without a ported constructor stops where the original would create it; in Chaos Castle, nothing.
TEST(HelperTableTest, UnknownHelpersFailBeforeConstruction)
{
    for (const std::uint16_t type : std::array<std::uint16_t, 2>{0xbe, 0x14d})
    {
        SCOPED_TRACE(type);
        HelperWorld tested;
        const auto created = CreateHelper(tested.world, type, tested.Rider().position, HeroSlot);
        ASSERT_FALSE(created.HasValue());
        EXPECT_NE(created.Error().find("0x443d70"), std::string::npos) << created.Error();
        EXPECT_FALSE(tested.Horse().alive);

        HelperWorld chaos(18);
        const auto inChaos = CreateHelper(chaos.world, type, chaos.Rider().position, HeroSlot);
        ASSERT_TRUE(inChaos.HasValue()) << inChaos.Error();
        EXPECT_EQ(inChaos.Value(), std::nullopt);
    }
}

TEST(HelperTableTest, TheSelectionRidingHelpersUseTheSharedFactoryAndKeepTheirActions)
{
    for (const std::uint16_t type : std::array<std::uint16_t, 5>{0xc6, 0x110, 0x14a, 0x14b, 0x14c})
    {
        WorldState world;
        auto& rider = world.Actors()[HeroSlot];
        rider.type = 0x20a;
        rider.angles = {0.0f, 0.0f, 75.0f};
        auto& helper = world.Helpers()[0];
        helper.horse.animation.action = 3;
        helper.horse.animation.previousAction = 2;

        const auto built = CreateHelper(world, type, {3.0f, 4.0f, 5.0f}, HeroSlot);

        ASSERT_TRUE(built.HasValue()) << built.Error();
        ASSERT_TRUE(built.Value().has_value());
        EXPECT_EQ(*built.Value(), 0u);
        EXPECT_TRUE(helper.alive);
        EXPECT_EQ(helper.type, type);
        EXPECT_FLOAT_EQ(helper.scale, 0.9f);
        EXPECT_EQ(helper.position, (glm::vec3{3.0f, 4.0f, 5.0f}));
        EXPECT_EQ(helper.angles, rider.angles);
        EXPECT_EQ(helper.horse.animation.action, 3u);
        EXPECT_EQ(helper.horse.animation.previousAction, 2u);
    }
}

// 0x443d40: turns off that rider's live ones; another rider's and the rest of the slot stay.
TEST(HelperTableTest, TurningOffTheRidersHorsesLeavesTheOthers)
{
    HelperWorld tested;
    auto& helpers = tested.world.Helpers();
    helpers[1] = HelperState{.alive = true, .type = DarkHorseModel, .rider = HeroSlot};
    helpers[2] = HelperState{.alive = true, .type = DarkHorseModel, .rider = OtherSlot};
    helpers[3] = HelperState{.alive = false, .type = DarkHorseModel, .rider = HeroSlot};
    helpers[4] = HelperState{.alive = true, .type = DarkHorseModel, .position = {1.0f, 2.0f, 3.0f}, .rider = HeroSlot};

    TurnOffHelpersOf(helpers, HeroSlot);

    EXPECT_FALSE(helpers[1].alive);
    EXPECT_TRUE(helpers[2].alive);
    EXPECT_FALSE(helpers[3].alive);
    EXPECT_FALSE(helpers[4].alive);
    EXPECT_EQ(helpers[4].position, (glm::vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(helpers.size(), HelperCount);
}

// Q-C225: in scene 5, with the rider dead or its +0x88 other than 1, 0x444010 turns the horse off and does nothing
// else: no alpha, no rand().
TEST(HelperTableTest, TheGameStepTurnsOffAHorseWithoutItsRider)
{
    struct Case
    {
        bool alive;
        std::uint8_t byte88;
    };
    for (const Case& probe : {Case{false, 1}, Case{true, 0}, Case{true, 8}})
    {
        SCOPED_TRACE(::testing::Message() << probe.alive << " con " << static_cast<int>(probe.byte88));
        HelperWorld tested(0, BreathSeed);
        ASSERT_NO_FATAL_FAILURE(tested.Mount());
        tested.Rider().alive = probe.alive;
        tested.Rider().byte88 = probe.byte88;

        ASSERT_TRUE(tested.Step().HasValue());

        EXPECT_FALSE(tested.Horse().alive);
        EXPECT_FLOAT_EQ(tested.Horse().horse.alpha, 0.0f);
        EXPECT_EQ(tested.random.State(), CrtRandom(BreathSeed).State());
    }
}

// 0x4440fd..0x445039 outside the safe cell: copies the rider's position and angles, takes the action and the speed from
// the rider's and advances; the breath draws its rand() % 3 (with QuietSeed, 2: no breath). With the rider's +8 at a
// value other than 1, sets it back to 0.
TEST(HelperTableTest, OutsideTheSafeCellTheHorseFollowsItsRider)
{
    HelperWorld tested;
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.Rider().position = CellCentre(101, 102);
    tested.Rider().angles = {0.0f, 0.0f, 90.0f};
    tested.Rider().animation.action = GestureRider;
    tested.Rider().byte8 = 2;

    ASSERT_TRUE(tested.Step().HasValue());

    const HelperState& horse = tested.Horse();
    EXPECT_TRUE(horse.alive);
    EXPECT_EQ(horse.position, CellCentre(101, 102));
    EXPECT_EQ(horse.angles, (glm::vec3{0.0f, 0.0f, 90.0f}));
    EXPECT_EQ(horse.horse.animation.action, 5u);
    EXPECT_FLOAT_EQ(horse.horse.speed, 1.0f);
    EXPECT_FLOAT_EQ(horse.horse.animation.frame, 1.0f);
    EXPECT_FLOAT_EQ(horse.horse.alpha, 0.1f);
    EXPECT_EQ(tested.Rider().byte8, 0);
    EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 1).State());
}

// 0x4440b8..0x4440f8: on a cell with bit 0 of the low byte, the one at the rider's position times 0.01, the alpha goes
// to 0 after rising and the horse advances with the action and speed it had (0x443d70's 0.5), without copying anything
// from the rider or drawing rand(). The hero's 0x02 and the high bits do not count.
TEST(HelperTableTest, InTheRidersSafeCellTheHorseHides)
{
    struct Case
    {
        std::uint16_t cell;
        bool hides;
    };
    for (const Case& probe : {Case{0x0001, true}, Case{0x0005, true}, Case{0x0002, false}, Case{0x0100, false}})
    {
        SCOPED_TRACE(probe.cell);
        HelperWorld tested;
        ASSERT_NO_FATAL_FAILURE(tested.Mount());
        const glm::vec3 created = tested.Horse().position;
        tested.Rider().position = CellCentre(7, 9);
        tested.Rider().animation.action = GestureRider;
        tested.Cell(7, 9) = probe.cell;

        ASSERT_TRUE(tested.Step().HasValue());

        const HelperState& horse = tested.Horse();
        if (probe.hides)
        {
            EXPECT_FLOAT_EQ(horse.horse.alpha, 0.0f);
            EXPECT_EQ(horse.position, created);
            EXPECT_EQ(horse.horse.animation.action, 0u);
            EXPECT_FLOAT_EQ(horse.horse.speed, 0.5f);
            EXPECT_FLOAT_EQ(horse.horse.animation.frame, 0.5f);
            EXPECT_EQ(tested.random.State(), CrtRandom(QuietSeed).State());
        }
        else
        {
            EXPECT_FLOAT_EQ(horse.horse.alpha, 0.1f);
            EXPECT_EQ(horse.position, CellCentre(7, 9));
            EXPECT_EQ(horse.horse.animation.action, 5u);
        }
    }
}

// 0x4440bb..0x4440d6: the cell is the truncated position's, with the low byte of each coordinate (0x5f82a0): 17299.99
// times 0.01 falls on 172, and 25650 (256.5) on 0.
TEST(HelperTableTest, TheSafeCellTruncatesAndKeepsTheLowByte)
{
    HelperWorld edge;
    ASSERT_NO_FATAL_FAILURE(edge.Mount());
    edge.Rider().position = {17299.99f, 12850.0f, 0.0f};
    edge.Cell(172, 128) = 0x0001;
    ASSERT_TRUE(edge.Step().HasValue());
    EXPECT_FLOAT_EQ(edge.Horse().horse.alpha, 0.0f);

    HelperWorld wrapped;
    ASSERT_NO_FATAL_FAILURE(wrapped.Mount());
    wrapped.Rider().position = {25650.0f, 12850.0f, 0.0f};
    wrapped.Cell(0, 128) = 0x0001;
    ASSERT_TRUE(wrapped.Step().HasValue());
    EXPECT_FLOAT_EQ(wrapped.Horse().horse.alpha, 0.0f);
}

using GoldenHelperTable = Client::Tests::GoldenDataTest;

// Q-C222, with the .att of Lorencia (0) and Devias (2): the horse looks at the cell of the rider's position, the drawn
// one, not the logical one at +0x41c (that of +0x3e2, 0x505de0, one ahead). On the town border, with the rider drawn
// inside, the logical cell outside and +0x3e2 at 0, the horse hides; drawn outside, with the logical one inside and
// +0x3e2 at 1, it is seen and follows.
TEST_F(GoldenHelperTable, TheHorseLooksAtTheDrawnCellAndNotAtTheLogicalOne)
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
        for (const bool drawnInside : {true, false})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << static_cast<int>(border.map)
                                              << (drawnInside ? ", dibujado adentro" : ", dibujado afuera"));
            HelperWorld tested(border.map);
            auto loaded = Client::Terrain::LoadWorldTerrain(m_dataRoot, border.map);
            ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
            tested.terrain = std::move(loaded).Value();
            ASSERT_EQ(tested.Cell(border.insideX, border.insideY) & 1, 1);
            ASSERT_EQ(tested.Cell(border.outsideX, border.outsideY) & 1, 0);
            ASSERT_NO_FATAL_FAILURE(tested.Mount());
            const glm::vec3 created = tested.Horse().position;
            ActorState& rider = tested.Rider();
            rider.animation.action = GestureRider;
            rider.position =
                drawnInside ? CellCentre(border.insideX, border.insideY) : CellCentre(border.outsideX, border.outsideY);
            rider.cellX = drawnInside ? border.outsideX : border.insideX;
            rider.cellY = drawnInside ? border.outsideY : border.insideY;
            rider.safeZone = drawnInside ? 0 : 1;

            ASSERT_TRUE(tested.Step().HasValue());

            const HelperState& horse = tested.Horse();
            if (drawnInside)
            {
                EXPECT_FLOAT_EQ(horse.horse.alpha, 0.0f);
                EXPECT_FLOAT_EQ(horse.horse.speed, 0.5f);
                EXPECT_EQ(horse.position, created);
                EXPECT_EQ(tested.random.State(), CrtRandom(QuietSeed).State());
            }
            else
            {
                EXPECT_FLOAT_EQ(horse.horse.alpha, 0.1f);
                EXPECT_FLOAT_EQ(horse.horse.speed, 1.0f);
                EXPECT_EQ(horse.position, rider.position);
                EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 1).State());
            }
        }
    }
}

// 0x44456b..0x4445d9: with rand() % 3 at 0, the breath leaves from (50, −4, 0) on bone 27 of the global pose buffer
// (0x4ec110 without the origin) into the game particles: 0x5f2/0 with the horse's light 3, its two rand() and the world
// clock, or in Atlans (7) the 0x607/0 drop, born with light at 1. Three rand() in total.
TEST(HelperTableTest, TheBreathLeavesFromBone27OfTheGlobalPose)
{
    HelperWorld tested(0, BreathSeed);
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.PoseTheGlobalBones();

    const auto stepped = tested.Step();

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const Client::Particles::Particle& breath = tested.world.Particles().All()[0];
    EXPECT_TRUE(breath.alive);
    EXPECT_EQ(breath.type, Client::Particles::BreathParticle);
    EXPECT_EQ(breath.subtype, 0);
    EXPECT_EQ(breath.position, (glm::vec3{10077.0f, 10123.0f, 197.0f}));
    EXPECT_EQ(breath.light, glm::vec3(3.0f));
    EXPECT_EQ(breath.angles, (glm::vec3{78.0f, 0.0f, 45.0f}));
    EXPECT_EQ(breath.scale, std::bit_cast<float>(0x3ef5c28fu));
    EXPECT_EQ(breath.rotation, 105.0f);
    EXPECT_EQ(breath.life, 16);
    EXPECT_FALSE(tested.world.Particles().All()[1].alive);
    EXPECT_EQ(tested.random.State(), RandomAfter(BreathSeed, 3).State());

    HelperWorld atlans(7, BreathSeed);
    ASSERT_NO_FATAL_FAILURE(atlans.Mount());
    atlans.PoseTheGlobalBones();
    ASSERT_TRUE(atlans.Step().HasValue());
    const Client::Particles::Particle& drop = atlans.world.Particles().All()[0];
    EXPECT_EQ(drop.type, Client::Particles::DropParticle);
    EXPECT_EQ(drop.position, (glm::vec3{10077.0f, 10123.0f, 197.0f}));
    EXPECT_EQ(drop.light, glm::vec3(1.0f));
    EXPECT_EQ(drop.life, 36);
    EXPECT_EQ(drop.scale, std::bit_cast<float>(0x3df5c28fu));
    EXPECT_EQ(atlans.random.State(), RandomAfter(BreathSeed, 3).State());
}

// Without bone 27 in the global buffer (no draw wrote it yet) the bone is the .bss zero matrix (200 bones): the breath
// leaves from the origin, with the same three rand(). With the horse's action 1, or the rider's +0x78 & 0x4000, there
// is no breath.
TEST(HelperTableTest, TheBreathWithoutItsBoneLeavesFromTheOrigin)
{
    HelperWorld tested(0, BreathSeed);
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.world.PoseCarry().bones.resize(27);

    const auto stepped = tested.Step();

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const Client::Particles::Particle& breath = tested.world.Particles().All()[0];
    EXPECT_TRUE(breath.alive);
    EXPECT_EQ(breath.type, Client::Particles::BreathParticle);
    EXPECT_EQ(breath.position, glm::vec3(0.0f));
    EXPECT_EQ(tested.random.State(), RandomAfter(BreathSeed, 3).State());

    HelperWorld calm(0, BreathSeed);
    ASSERT_NO_FATAL_FAILURE(calm.Mount());
    calm.Rider().flags78 = 0x4000;
    ASSERT_TRUE(calm.Step().HasValue());
    EXPECT_EQ(calm.random.State(), CrtRandom(BreathSeed).State());
}

// 0x445260 → 0x602e90 leaves the horse's pose in the global buffer (WorldState::PoseCarry) and the next step takes the
// breath from bone 27 of that pose.
TEST(HelperTableTest, TheBreathReadsThePoseTheHorseDrawLeft)
{
    HelperWorld tested(0, BreathSeed);
    tested.models = HorseModels(28);
    const HorseModels& models = tested.models;
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.Horse().horse.alpha = 1.0f;
    tested.trapezoid = Everything;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    HelperDrawContext context{.batch = batch,
                              .models = models,
                              .world = tested.world,
                              .random = tested.random,
                              .trapezoid = &tested.trapezoid};
    context.onPose = [&](const Client::Actors::Drawing::DrawnObject& object)
    { return Client::Actors::Drawing::PublishPose(tested.world.PoseCarry(), models, object, true); };
    ASSERT_TRUE(DrawHelpers(context).HasValue());
    ASSERT_EQ(tested.world.PoseCarry().bones.size(), 28u);
    const glm::vec3 expected = Client::Math::OriginalVectorTransform(
        {50.0f, -4.0f, 0.0f}, Client::Math::OriginalMatrixOf(tested.world.PoseCarry().bones[27]));
    tested.random = CrtRandom(BreathSeed);

    ASSERT_TRUE(tested.Step().HasValue());

    const Client::Particles::Particle& breath = tested.world.Particles().All()[0];
    EXPECT_TRUE(breath.alive);
    EXPECT_EQ(breath.position, expected);
    EXPECT_NE(breath.position, glm::vec3(0.0f));
}

// 0x44416b..0x4444ef, the rider's gallop (0x4b) outside 10: action 1, no breath. rand() % 2: with 1, nothing; with 0,
// in Kalima (24 to 29 and 36) or with the rider's +0x78 & 0x4000, nothing; otherwise, the place's three rand() around
// the horse, (rand() & 63) − 32 in x and y and (rand() & 31) − 16 in z, and in Devias (2) 0x5f2/0 with its two rand(),
// and elsewhere 0x5f3/0 with its four, both with light 1.
TEST(HelperTableTest, TheGallopRaisesTheDustOfItsMap)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        HelperWorld quiet(map, QuietSeed);
        ASSERT_NO_FATAL_FAILURE(quiet.Mount());
        quiet.Rider().animation.action = GallopingRider;
        ASSERT_TRUE(quiet.Step().HasValue());
        EXPECT_EQ(quiet.Horse().horse.animation.action, 1u);
        EXPECT_FLOAT_EQ(quiet.Horse().horse.speed, 0.34f);
        EXPECT_FALSE(quiet.world.Particles().All()[0].alive);
        EXPECT_EQ(quiet.random.State(), RandomAfter(QuietSeed, 1).State());
    }

    HelperWorld lorencia(0, DustSeed);
    ASSERT_NO_FATAL_FAILURE(lorencia.Mount());
    lorencia.Rider().animation.action = GallopingRider;
    const auto inLorencia = lorencia.Step();
    ASSERT_TRUE(inLorencia.HasValue()) << inLorencia.Error();
    const Client::Particles::Particle& dust = lorencia.world.Particles().All()[0];
    EXPECT_TRUE(dust.alive);
    EXPECT_EQ(dust.type, Client::Particles::DustParticle);
    EXPECT_EQ(dust.subtype, 0);
    EXPECT_EQ(dust.position, (glm::vec3{10045.0f, 10033.0f, 157.0f}));
    EXPECT_EQ(dust.light, glm::vec3(1.0f));
    EXPECT_EQ(dust.angles, (glm::vec3{103.0f, 0.0f, 45.0f}));
    EXPECT_EQ(dust.scale, std::bit_cast<float>(0x3ec28f5cu));
    EXPECT_EQ(dust.velocity, Client::Math::OriginalVectorRotate(
                                 {0.0f, 3.0f, 0.0f}, Client::Math::OriginalAngleMatrix({0.0f, 0.0f, 45.0f})));
    EXPECT_EQ(dust.life, 32);
    EXPECT_FALSE(lorencia.world.Particles().All()[1].alive);
    EXPECT_EQ(lorencia.random.State(), RandomAfter(DustSeed, 8).State());

    HelperWorld devias(2, DustSeed);
    ASSERT_NO_FATAL_FAILURE(devias.Mount());
    devias.Rider().animation.action = GallopingRider;
    const auto inDevias = devias.Step();
    ASSERT_TRUE(inDevias.HasValue()) << inDevias.Error();
    const Client::Particles::Particle& smoke = devias.world.Particles().All()[0];
    EXPECT_EQ(smoke.type, Client::Particles::BreathParticle);
    EXPECT_EQ(smoke.subtype, 0);
    EXPECT_EQ(smoke.position, (glm::vec3{10046.0f, 10032.0f, 157.0f}));
    EXPECT_EQ(smoke.light, glm::vec3(1.0f));
    EXPECT_EQ(smoke.angles, (glm::vec3{177.0f, 0.0f, 45.0f}));
    EXPECT_EQ(smoke.scale, std::bit_cast<float>(0x3f35c28fu));
    EXPECT_EQ(smoke.rotation, 105.0f);
    EXPECT_EQ(smoke.life, 16);
    EXPECT_EQ(devias.random.State(), RandomAfter(DustSeed, 6).State());

    for (const std::int32_t map : {24, 29, 36})
    {
        SCOPED_TRACE(map);
        HelperWorld kalima(map, DustSeed);
        ASSERT_NO_FATAL_FAILURE(kalima.Mount());
        kalima.Rider().animation.action = GallopingRider;
        ASSERT_TRUE(kalima.Step().HasValue());
        EXPECT_FALSE(kalima.world.Particles().All()[0].alive);
        EXPECT_EQ(kalima.random.State(), RandomAfter(DustSeed, 1).State());
    }

    HelperWorld flagged(0, DustSeed);
    ASSERT_NO_FATAL_FAILURE(flagged.Mount());
    flagged.Rider().animation.action = GallopingRider;
    flagged.Rider().flags78 = 0x4000;
    ASSERT_TRUE(flagged.Step().HasValue());
    EXPECT_FALSE(flagged.world.Particles().All()[0].alive);
    EXPECT_EQ(flagged.random.State(), RandomAfter(DustSeed, 1).State());
}

// 0x44419f..0x4443d1, the gallop in Icarus (10), ignoring Kalima and the rider's +0x78: (rand() & 63) − 32 in the z of
// (120, 0, z), the frame bone's point in the global buffer, the 0x6eb/1 wave shifted (rand() & 15) − 8 in x and y with
// its scale rand(), the guard's rand() and the 0x5f2/0 one at a time, shifted rand() % 50 − 25 in x and y and
// (rand() & 15) − 18 in z, while their count does not reach a new rand() % 5 + 5 on each turn (Q-C400). With seed 1
// and frame 1.25 (bone 25 at (10025, 10125, 195)): the place (10145, 10125, 204), seven clouds and 47 rand().
TEST(HelperTableTest, InIcarusTheGallopRaisesAWaveAndPuffsFromTheGlobalPose)
{
    struct Puff
    {
        glm::vec3 position;
        std::uint32_t scale;
        float turn;
    };
    constexpr std::array<Puff, 7> Puffs{{
        {{10144.0f, 10128.0f, 200.0f}, 0x3f28f5c2u, 344.0f},
        {{10165.0f, 10131.0f, 197.0f}, 0x3f11eb85u, 131.0f},
        {{10162.0f, 10127.0f, 198.0f}, 0x3f0ccccdu, 204.0f},
        {{10123.0f, 10142.0f, 200.0f}, 0x3f1c28f6u, 356.0f},
        {{10165.0f, 10147.0f, 200.0f}, 0x3f2b851eu, 18.0f},
        {{10132.0f, 10117.0f, 197.0f}, 0x3f170a3du, 174.0f},
        {{10131.0f, 10122.0f, 199.0f}, 0x3f11eb85u, 344.0f},
    }};
    for (const std::uint32_t flags : {0u, 0x4000u})
    {
        SCOPED_TRACE(flags);
        HelperWorld tested(10, QuietSeed);
        ASSERT_NO_FATAL_FAILURE(tested.Mount());
        tested.PoseTheGlobalBones();
        tested.Gallop(1.25f);
        tested.Rider().flags78 = flags;

        const auto stepped = tested.Step();

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const Client::Effects::ModelEffect& wave = tested.world.Map().effects.Effects()[0];
        EXPECT_TRUE(wave.alive);
        EXPECT_EQ(wave.type, Client::Effects::ShockWaveEffect);
        EXPECT_EQ(wave.subtype, 1);
        EXPECT_EQ(wave.life, 20);
        EXPECT_EQ(wave.transform.position, (glm::vec3{10140.0f, 10131.0f, 204.0f}));
        EXPECT_EQ(wave.transform.angles, (glm::vec3{0.0f, 0.0f, 45.0f}));
        EXPECT_EQ(wave.transform.scale, 1.0f);
        EXPECT_EQ(wave.light, glm::vec3(1.0f));
        EXPECT_FALSE(tested.world.Map().effects.Effects()[1].alive);
        for (std::size_t index = 0; index < Puffs.size(); ++index)
        {
            SCOPED_TRACE(index);
            const Client::Particles::Particle& puff = tested.world.Particles().All()[index];
            EXPECT_TRUE(puff.alive);
            EXPECT_EQ(puff.type, Client::Particles::BreathParticle);
            EXPECT_EQ(puff.subtype, 0);
            EXPECT_EQ(puff.position, Puffs[index].position);
            EXPECT_EQ(puff.light, glm::vec3(1.0f));
            EXPECT_EQ(puff.scale, std::bit_cast<float>(Puffs[index].scale));
            EXPECT_EQ(puff.angles, (glm::vec3{Puffs[index].turn, 0.0f, 45.0f}));
            EXPECT_EQ(puff.rotation, 105.0f);
        }
        EXPECT_FALSE(tested.world.Particles().All()[Puffs.size()].alive);
        EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 47).State());
    }
}

// 0x4441cd..0x4442ad: the bone by the horse's frame, in overlapping windows where the first wins: (1, 1.2) 19,
// (1.1, 1.4) 25, (1.3, 1.6) 32 and (1.5, 1.8] 38, closed above. Outside all, only the z rand() (Q-C404), no wave or
// clouds. The wave stays shifted −5 in x from the bone plus 120.
TEST(HelperTableTest, TheIcarusDustPicksItsBoneByTheFrame)
{
    struct Case
    {
        float frame;
        std::optional<std::size_t> bone;
    };
    for (const Case& probe : {Case{1.0f, std::nullopt}, Case{1.15f, std::size_t{19}}, Case{1.35f, std::size_t{25}},
                              Case{1.55f, std::size_t{32}}, Case{1.65f, std::size_t{38}}, Case{1.8f, std::size_t{38}},
                              Case{1.85f, std::nullopt}, Case{0.5f, std::nullopt}})
    {
        SCOPED_TRACE(probe.frame);
        HelperWorld tested(10, QuietSeed);
        ASSERT_NO_FATAL_FAILURE(tested.Mount());
        tested.PoseTheGlobalBones();
        tested.Gallop(probe.frame);

        ASSERT_TRUE(tested.Step().HasValue());

        const Client::Effects::ModelEffect& wave = tested.world.Map().effects.Effects()[0];
        if (probe.bone)
        {
            ASSERT_TRUE(wave.alive);
            EXPECT_EQ(wave.transform.position.x, HelperWorld::PosedBone(*probe.bone).x + 115.0f);
            EXPECT_TRUE(tested.world.Particles().All()[0].alive);
        }
        else
        {
            EXPECT_FALSE(wave.alive);
            EXPECT_FALSE(tested.world.Particles().All()[0].alive);
            EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 1).State());
        }
    }
}

// Without the frame bone in the global buffer, the .bss zero matrix applies: the wave is born at the rand() % 16 − 8
// shift around the origin (the seed's −5, the posed bones' 115 − 120), the same as with the bones written as zero.
TEST(HelperTableTest, TheIcarusDustWithoutItsBoneReadsAZeroBone)
{
    HelperWorld tested(10, QuietSeed);
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.Gallop(1.25f);
    HelperWorld zeroed(10, QuietSeed);
    ASSERT_NO_FATAL_FAILURE(zeroed.Mount());
    zeroed.Gallop(1.25f);
    zeroed.world.PoseCarry().bones.assign(HelperWorld::PosedBones, Client::Math::BoneTransform(0.0f));

    const auto stepped = tested.Step();
    const auto zeroedStep = zeroed.Step();

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_TRUE(zeroedStep.HasValue()) << zeroedStep.Error();
    const Client::Effects::ModelEffect& wave = tested.world.Map().effects.Effects()[0];
    ASSERT_TRUE(wave.alive);
    EXPECT_EQ(wave.transform.position.x, -5.0f);
    EXPECT_EQ(wave.transform.position, zeroed.world.Map().effects.Effects()[0].transform.position);
    EXPECT_TRUE(tested.world.Particles().All()[0].alive);
    EXPECT_EQ(tested.random.State(), zeroed.random.State());
}

float QuakeF32(double value)
{
    return static_cast<float>(value);
}

// 0x445571: ((rand() % 3) − 3) · 0.7f (0x6b8a5c), stored as a float in 0x7d325c8.
float QuakeShake(CrtRandom& random)
{
    return QuakeF32(static_cast<double>((random.Next() % 3) - 3) * static_cast<double>(0.7f));
}

std::size_t LiveEffects(HelperWorld& tested)
{
    const auto effects = tested.world.Map().effects.Effects();
    return static_cast<std::size_t>(
        std::count_if(effects.begin(), effects.end(), [](const auto& effect) { return effect.alive; }));
}

// 0x4445e1..0x44468a: with the horse's action 3 (the rider's 0x53), after the breath, 0x52e470 with the hero
// (0x743a170) as owner, from the point (60, 0, 0) of bone 9 of the global buffer (0x68b19cc) to the same bone's, in
// white and with type, length and key at 0; then 0x4453e0 raises the +0x8e count. With 1 logic step there is no wave
// and with frame 0 it is outside the window: no stones, no shake. Neither draws rand().
TEST(HelperTableTest, TheRearingLeavesARibbonFromBone9AndCountsTheQuake)
{
    HelperWorld rearing;
    ASSERT_NO_FATAL_FAILURE(rearing.Mount());
    rearing.PoseTheGlobalBones();
    rearing.Rider().animation.action = RearingRider;
    float shake = 5.0f;
    const auto reared = rearing.StepCounting(1, &shake);
    ASSERT_TRUE(reared.HasValue()) << reared.Error();
    EXPECT_EQ(rearing.Horse().horse.animation.action, HelperWorld::EarthquakeAction);
    EXPECT_EQ(rearing.Horse().horse.effectCount, 1u);
    const auto& ribbons = rearing.world.DrawTail().ribbons;
    EXPECT_EQ(std::count_if(ribbons.begin(), ribbons.end(), [](const auto& ribbon) { return ribbon.alive; }), 1);
    const auto& ribbon = ribbons[0];
    EXPECT_TRUE(ribbon.alive);
    EXPECT_EQ(ribbon.ownerContext, &rearing.Rider());
    EXPECT_EQ(ribbon.ownerEquipment, &rearing.Rider().level);
    EXPECT_EQ(ribbon.type, 0);
    EXPECT_EQ(ribbon.key, 0);
    EXPECT_EQ(ribbon.life, 30);
    EXPECT_EQ(ribbon.count, 1);
    EXPECT_EQ(ribbon.light, glm::vec3(1.0f, 1.0f, 1.0f));
    EXPECT_EQ(ribbon.first[0], HelperWorld::PosedBone(9) + glm::vec3(60.0f, 0.0f, 0.0f));
    EXPECT_EQ(ribbon.second[0], HelperWorld::PosedBone(9));
    EXPECT_EQ(LiveEffects(rearing), 0u);
    EXPECT_EQ(shake, 5.0f);
    EXPECT_EQ(rearing.random.State(), RandomAfter(QuietSeed, 1).State());

    // The next step lengthens the same ribbon (the same owner) and the count goes on.
    ASSERT_TRUE(rearing.StepCounting(2, &shake).HasValue());
    EXPECT_EQ(ribbon.count, 2);
    EXPECT_EQ(rearing.Horse().horse.effectCount, 2u);
}

// 0x4453f8..0x445425: with the logic step count a multiple of 10 (idiv's signed remainder, also for negatives), the
// 0x6eb/0 wave at the horse's position and angles (the rider's) with its light (+0xf4, 0x443d70's 3): life 30 and
// scale 20, no rand(). With another count, none.
TEST(HelperTableTest, EveryTenLogicStepsTheRearingStompsAShockWave)
{
    for (const std::int32_t steps : {0, 20, -10})
    {
        HelperWorld stomp;
        ASSERT_NO_FATAL_FAILURE(stomp.Mount());
        stomp.Rider().animation.action = RearingRider;
        float shake = 0.0f;
        ASSERT_TRUE(stomp.StepCounting(steps, &shake).HasValue()) << steps;
        ASSERT_EQ(LiveEffects(stomp), 1u) << steps;
        const auto& wave = stomp.world.Map().effects.Effects()[0];
        EXPECT_EQ(wave.type, Client::Effects::ShockWaveEffect);
        EXPECT_EQ(wave.subtype, Client::Effects::StompShockWaveSubtype);
        EXPECT_EQ(wave.transform.position, CellCentre(100, 100));
        EXPECT_EQ(wave.transform.angles, glm::vec3(0.0f, 0.0f, 45.0f));
        EXPECT_EQ(wave.light, glm::vec3(3.0f, 3.0f, 3.0f));
        EXPECT_EQ(wave.life, 30);
        EXPECT_EQ(wave.transform.scale, 20.0f);
        EXPECT_EQ(stomp.random.State(), RandomAfter(QuietSeed, 1).State()) << steps;
    }
    HelperWorld quiet;
    ASSERT_NO_FATAL_FAILURE(quiet.Mount());
    quiet.Rider().animation.action = RearingRider;
    float shake = 0.0f;
    ASSERT_TRUE(quiet.StepCounting(7, &shake).HasValue());
    EXPECT_EQ(LiveEffects(quiet), 0u);
}

// 0x44542d..0x445553: with the frame in [8, 9.5] and an odd count (4 + 1 = 5), six stones on a ring of radius
// (5 / 2) · 150 = 300 around the horse: the turn starts at rand() % 360 and adds 60 before each, (0, 300, 0) rotated by
// that z plus the horse's position, each axis in float; type 0x118 + (rand() & 1) is drawn after the turn and before
// the creation, which draws its two rand() (scale and yaw) with the horse's light and life 40. Then, the camera
// shake. With 1 logic step, no wave.
TEST(HelperTableTest, InItsWindowTheQuakeRingsSixStonesOnOddCountsAndShakesTheCamera)
{
    HelperWorld quake;
    ASSERT_NO_FATAL_FAILURE(quake.Mount());
    quake.Rear(8.0f, 4);
    float shake = 0.0f;
    const auto quaked = quake.StepCounting(1, &shake);
    ASSERT_TRUE(quaked.HasValue()) << quaked.Error();
    EXPECT_EQ(quake.Horse().horse.effectCount, 5u);

    CrtRandom expected = RandomAfter(QuietSeed, 1);
    const glm::vec3 horse = CellCentre(100, 100);
    const glm::vec3 reach(0.0f, 300.0f, 0.0f);
    glm::vec3 turn(0.0f, 0.0f, static_cast<float>(expected.Next() % 360));
    const auto effects = quake.world.Map().effects.Effects();
    for (std::size_t stone = 0; stone < 6; ++stone)
    {
        turn.z = QuakeF32(static_cast<double>(turn.z) + 60.0);
        const glm::vec3 offset = Client::Math::OriginalVectorRotate(reach, Client::Math::OriginalAngleMatrix(turn));
        const glm::vec3 place(QuakeF32(static_cast<double>(offset.x) + static_cast<double>(horse.x)),
                              QuakeF32(static_cast<double>(offset.y) + static_cast<double>(horse.y)),
                              QuakeF32(static_cast<double>(offset.z) + static_cast<double>(horse.z)));
        const int type = 0x118 + (expected.Next() & 1);
        static_cast<void>(expected.Next());
        static_cast<void>(expected.Next());
        const auto& effect = effects[stone];
        EXPECT_TRUE(effect.alive) << stone;
        EXPECT_EQ(effect.type, type) << stone;
        EXPECT_EQ(effect.transform.position, place) << stone;
        EXPECT_EQ(effect.light, glm::vec3(3.0f, 3.0f, 3.0f)) << stone;
        EXPECT_EQ(effect.life, 40) << stone;
        // The ring, without the matrix: (−300 · sin z, 300 · cos z) around the horse.
        const double radians = static_cast<double>(turn.z) * 3.14159265358979323846 / 180.0;
        EXPECT_NEAR(effect.transform.position.x - horse.x, -300.0 * RoundedSin(radians), 0.01) << stone;
        EXPECT_NEAR(effect.transform.position.y - horse.y, 300.0 * RoundedCos(radians), 0.01) << stone;
    }
    EXPECT_FALSE(effects[6].alive);
    EXPECT_EQ(shake, QuakeShake(expected));
    EXPECT_EQ(quake.random.State(), expected.State());
}

// 0x445476: with an even count (5 + 1 = 6) there are no stones, but inside the window (9.5 included) there is the
// shake.
TEST(HelperTableTest, OnEvenCountsTheQuakeOnlyShakesTheCamera)
{
    HelperWorld quake;
    ASSERT_NO_FATAL_FAILURE(quake.Mount());
    quake.Rear(9.5f, 5);
    float shake = 0.0f;
    ASSERT_TRUE(quake.StepCounting(1, &shake).HasValue());
    EXPECT_EQ(quake.Horse().horse.effectCount, 6u);
    EXPECT_EQ(LiveEffects(quake), 0u);
    CrtRandom expected = RandomAfter(QuietSeed, 1);
    EXPECT_EQ(shake, QuakeShake(expected));
    EXPECT_EQ(quake.random.State(), expected.State());
}

// 0x44542d..0x445455: the frame below 8, above 9.5 or NaN (fcomp + test ah,1 sends it out) only raises the count: no
// stones, shake or rand(), and with a count short of 0x13, nothing else.
TEST(HelperTableTest, OutsideItsWindowTheQuakeOnlyCounts)
{
    for (const float frame : {7.99f, 9.51f, std::numeric_limits<float>::quiet_NaN()})
    {
        HelperWorld quake;
        ASSERT_NO_FATAL_FAILURE(quake.Mount());
        quake.Rear(frame, 4);
        float shake = 5.0f;
        ASSERT_TRUE(quake.StepCounting(1, &shake).HasValue()) << frame;
        EXPECT_EQ(quake.Horse().horse.effectCount, 5u) << frame;
        EXPECT_EQ(LiveEffects(quake), 0u) << frame;
        EXPECT_EQ(shake, 5.0f) << frame;
        EXPECT_EQ(quake.random.State(), RandomAfter(QuietSeed, 1).State()) << frame;
    }
}

// 0x51cc8b: a stone falling on a cell with bit 0x04, 0x08 or 0x10 is not born and does not draw its two rand(); the
// type was already drawn. With the whole map at 0x08: the turn, six types and the shake.
TEST(HelperTableTest, TheStonesSkipTheCellsTheirAttributeMasks)
{
    HelperWorld quake;
    ASSERT_NO_FATAL_FAILURE(quake.Mount());
    quake.terrain.attributes->cells.fill(0x08);
    quake.Rear(8.5f, 4);
    float shake = 0.0f;
    ASSERT_TRUE(quake.StepCounting(1, &shake).HasValue());
    EXPECT_EQ(LiveEffects(quake), 0u);
    CrtRandom expected = RandomAfter(QuietSeed, 8);
    EXPECT_EQ(shake, QuakeShake(expected));
    EXPECT_EQ(quake.random.State(), expected.State());
}

// 0x44452e: only the branch of the rider's other actions (the one that gives action 0) clears the count; the gallop
// leaves it.
TEST(HelperTableTest, OnlyTheOtherGaitsClearTheQuakeCount)
{
    HelperWorld standing;
    ASSERT_NO_FATAL_FAILURE(standing.Mount());
    standing.Horse().horse.effectCount = 7;
    ASSERT_TRUE(standing.Step().HasValue());
    EXPECT_EQ(standing.Horse().horse.effectCount, 0u);

    HelperWorld galloping;
    ASSERT_NO_FATAL_FAILURE(galloping.Mount());
    galloping.Gallop();
    galloping.Horse().horse.effectCount = 7;
    ASSERT_TRUE(galloping.Step().HasValue());
    EXPECT_EQ(galloping.Horse().horse.effectCount, 7u);
}

// 0x445581..0x4455b3: outside the window, with the count at 0x13, the 0xfa earthquake with the horse as owner (its type
// and angles, read live), its position, its angles, its light and state 2; its creation draws subtype rand() % 100 and
// closes the own record (+0xf04 rises 1 and, with depth at 0, 3848 rand()). The count goes to 0xfd and the next step
// (0xfe) no longer creates it.
TEST(HelperTableTest, AtCount0x13OutsideItsWindowTheQuakeCracksTheEarth)
{
    HelperWorld quake;
    ASSERT_NO_FATAL_FAILURE(quake.Mount());
    quake.Rear(0.0f, 0x12);
    float shake = 5.0f;
    const auto quaked = quake.StepCounting(1, &shake);
    ASSERT_TRUE(quaked.HasValue()) << quaked.Error();
    EXPECT_EQ(quake.Horse().horse.effectCount, 0xfdu);
    ASSERT_EQ(LiveEffects(quake), 1u);
    const auto& earthquake = quake.world.Map().effects.Effects()[0];
    CrtRandom expected = RandomAfter(QuietSeed, 1);
    EXPECT_EQ(earthquake.type, Client::Effects::EarthquakeEffect);
    EXPECT_EQ(earthquake.subtype, expected.Next() % 100);
    EXPECT_EQ(earthquake.state, 2);
    EXPECT_EQ(earthquake.start, CellCentre(100, 100));
    EXPECT_EQ(earthquake.light, glm::vec3(3.0f, 3.0f, 3.0f));
    EXPECT_EQ(earthquake.owner.type, &quake.Horse().type);
    EXPECT_EQ(earthquake.owner.angles, &quake.Horse().angles);
    EXPECT_EQ(Client::Effects::OwnerTypeOf(earthquake.owner), std::optional<std::uint16_t>(DarkHorseModel));
    EXPECT_EQ(quake.world.OwnCharacter().bytef04, 1u);
    EXPECT_EQ(quake.world.Runtime().ownRecordDepth, 0u);
    for (int draw = 0; draw < Client::Models::OwnRecordCloseRandoms; ++draw)
    {
        static_cast<void>(expected.Next());
    }
    EXPECT_EQ(quake.random.State(), expected.State());
    EXPECT_EQ(shake, 5.0f);

    ASSERT_TRUE(quake.StepCounting(1, &shake).HasValue());
    EXPECT_EQ(quake.Horse().horse.effectCount, 0xfeu);
    EXPECT_EQ(LiveEffects(quake), 1u);
}

// Without the camera shake (0x7d325c8), the window stops at 0x445577 (here, with an even count).
TEST(HelperTableTest, TheQuakeWithoutTheCameraShakeStops)
{
    HelperWorld quake;
    ASSERT_NO_FATAL_FAILURE(quake.Mount());
    quake.Rear(8.0f, 5);
    EXPECT_TRUE(HasError(quake.StepCounting(1, nullptr), "0x445577"));
}

// Without the 0xbf skeleton or the map loaded, a live horse's step stops; with the table empty it needs neither.
TEST(HelperTableTest, ALiveHorseNeedsItsSkeletonAndTheMap)
{
    HelperWorld empty;
    EXPECT_TRUE(
        StepHelpers(HelperStepContext{.world = empty.world, .models = nullptr, .random = empty.random}).HasValue());
    empty.terrain.attributes.reset();
    EXPECT_TRUE(empty.Step().HasValue());

    HelperWorld noSkeleton;
    ASSERT_NO_FATAL_FAILURE(noSkeleton.Mount());
    const auto withoutSkeleton =
        StepHelpers(HelperStepContext{.world = noSkeleton.world, .models = nullptr, .random = noSkeleton.random});
    EXPECT_TRUE(HasError(withoutSkeleton, "0x615700"));

    HelperWorld noMap;
    ASSERT_NO_FATAL_FAILURE(noMap.Mount());
    noMap.terrain.attributes.reset();
    EXPECT_TRUE(HasError(noMap.Step(), "0x4440db"));
}

// The twelve 0x60f/0 of 0x44469d..0x44474c in the world's joint pool (0x74a8d78).
std::span<JointState> Joints(HelperWorld& tested)
{
    return tested.world.DrawTail().joints;
}

// 0x44469d..0x44474c, with the rider's +8 at 1 (set by 0x421db0 with the A9): four turns of z + 90 (0x6b8948), on each
// (0, 50, 0) rotated (−50 · sin z, 50 · cos z, 0) plus the horse's position (the rider's, (10050, 10050, 170)), and
// three 0x530860(0x60f, point, point, angles, 0, horse, 40, 2, 0, 0, −1). Each is born as the width 40 0x60f/0
// (0x5341fc..0x5342d2): life 50, 20 points, speed 40, vy rand() % 500 − 250 and vz (rand() % 250 + 200) · 0.01f, with
// the horse's light (+0xf4, 0x443d70's 3), the horse as owner and key 2. Three rand() each, after the breath's; +8
// goes back to 0 and the horse takes the rider's live byte.
TEST(HelperTableTest, TheOrbitsRaiseTwelveSparklesAroundTheHorse)
{
    HelperWorld tested;
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    EXPECT_EQ(tested.Horse().light, glm::vec3(3.0f));
    tested.Rider().byte8 = 1;

    const auto stepped = tested.Step();

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.Rider().byte8, 0);
    EXPECT_TRUE(tested.Horse().alive);
    constexpr std::array<glm::vec3, 4> Points{{{10000.0f, 10050.0f, 170.0f},
                                               {10050.0f, 10000.0f, 170.0f},
                                               {10100.0f, 10050.0f, 170.0f},
                                               {10050.0f, 10100.0f, 170.0f}}};
    CrtRandom draws = RandomAfter(QuietSeed, 1);
    const double hundredth = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
    const std::span<JointState> joints = Joints(tested);
    for (std::size_t index = 0; index < 12; ++index)
    {
        SCOPED_TRACE(index);
        const JointState& joint = joints[index];
        const std::size_t turn = index / 3;
        static_cast<void>(draws.Next()); // 0x534214: the width 10 vz, which width 40 overwrites.
        const int drift = draws.Next();
        const int rise = draws.Next();
        ASSERT_TRUE(joint.alive);
        EXPECT_EQ(joint.type, 0x60f);
        EXPECT_EQ(joint.subtype, 0);
        EXPECT_EQ(joint.texture, 0x60f);
        EXPECT_EQ(joint.blend, 1);
        EXPECT_EQ(joint.width, 40.0f);
        EXPECT_EQ(joint.key, 2);
        EXPECT_EQ(joint.skill, 0);
        EXPECT_EQ(joint.skillSerial, 0);
        EXPECT_EQ(joint.ownerContext, &tested.Horse());
        EXPECT_EQ(joint.ownerKind, Client::Kernel::JointOwnerKind::Helper);
        EXPECT_EQ(joint.angles, glm::vec3(0.0f, 0.0f, 90.0f * static_cast<float>(turn + 1)));
        EXPECT_EQ(joint.position, Points[turn]);
        EXPECT_EQ(joint.targetPosition, Points[turn]);
        EXPECT_EQ(joint.light, glm::vec3(3.0f));
        EXPECT_EQ(joint.life, 50);
        EXPECT_EQ(joint.maximumCount, 20);
        EXPECT_EQ(joint.speed, 40.0f);
        EXPECT_EQ(joint.velocity, glm::vec3(0.0f, static_cast<float>(drift % 500 - 250),
                                            static_cast<float>(static_cast<double>(rise % 250 + 200) * hundredth)));
        EXPECT_TRUE(joint.tailEnabled);
    }
    EXPECT_FALSE(joints[12].alive);
    EXPECT_EQ(tested.random.State(), draws.State());
    EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 37).State());
}

// 0x530860 walks the pool: the twelve go to the first free slots (with five, the three of the first turn and two of
// the second) and, with the pool full, they are not born and draw no rand(); the step goes on and +8 goes back to 0.
TEST(HelperTableTest, TheOrbitsTakeOnlyTheFreeJoints)
{
    constexpr std::array<std::size_t, 5> FreeSlots{3, 77, 250, 498, 499};
    HelperWorld tested;
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.Rider().byte8 = 1;
    const std::span<JointState> joints = Joints(tested);
    for (JointState& joint : joints)
    {
        joint.alive = true;
        joint.type = 0x697;
    }
    for (const std::size_t slot : FreeSlots)
    {
        joints[slot].alive = false;
    }

    const auto stepped = tested.Step();

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.Rider().byte8, 0);
    for (const std::size_t slot : FreeSlots)
    {
        SCOPED_TRACE(slot);
        EXPECT_EQ(joints[slot].type, 0x60f);
        EXPECT_EQ(joints[slot].angles, glm::vec3(0.0f, 0.0f, slot >= 498 ? 180.0f : 90.0f));
    }
    EXPECT_EQ(std::count_if(joints.begin(), joints.end(), [](const JointState& joint) { return joint.type == 0x60f; }),
              5);
    EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 1 + (5 * 3)).State());

    HelperWorld full;
    ASSERT_NO_FATAL_FAILURE(full.Mount());
    full.Rider().byte8 = 1;
    const std::span<JointState> pool = Joints(full);
    for (JointState& joint : pool)
    {
        joint.alive = true;
        joint.type = 0x697;
    }
    const auto unchanged = full.Step();
    ASSERT_TRUE(unchanged.HasValue()) << unchanged.Error();
    EXPECT_EQ(full.Rider().byte8, 0);
    EXPECT_TRUE(std::all_of(pool.begin(), pool.end(), [](const JointState& joint) { return joint.type == 0x697; }));
    EXPECT_EQ(full.random.State(), RandomAfter(QuietSeed, 1).State());
}

// On the safe cell the step jumps from 0x4440f8 to 0x445006, skipping 0x444752: +8 stays at 1 and the twelve wait for
// the horse to leave it (Q-C437).
TEST(HelperTableTest, TheOrbitsWaitUntilTheHorseLeavesTheSafeCell)
{
    HelperWorld tested;
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.Rider().byte8 = 1;
    tested.Cell(100, 100) = 1;
    const std::span<JointState> joints = Joints(tested);

    ASSERT_TRUE(tested.Step().HasValue());

    EXPECT_EQ(tested.Rider().byte8, 1);
    EXPECT_FALSE(joints[0].alive);
    EXPECT_EQ(tested.random.State(), CrtRandom(QuietSeed).State());

    tested.Cell(100, 100) = 0;
    ASSERT_TRUE(tested.Step().HasValue());

    EXPECT_EQ(tested.Rider().byte8, 0);
    EXPECT_EQ(std::count_if(joints.begin(), joints.end(), [](const JointState& joint) { return joint.alive; }), 12);
    EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 1 + (12 * 3)).State());
}

// The joints step (0x535840) with the horse as owner: the common advance of 40 by its angles, 0x53b239's turn with key
// 2 ((life + vy) / 2) around the target, vz, the tail and one life less. With width 40, 0x53b378 does not read the
// horse's action (+0x111): even in the fall's 0xd, it is still the 0x60f/0. No rand().
TEST(HelperTableTest, TheOrbitsTurnWithoutReadingTheHorse)
{
    HelperWorld tested;
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.Rider().byte8 = 1;
    ASSERT_TRUE(tested.Step().HasValue());
    tested.Horse().horse.animation.action = 0xd;
    const std::span<JointState> joints = Joints(tested);
    std::vector<JointState> expected(joints.begin(), joints.begin() + 12);
    const std::uint32_t state = tested.random.State();

    ASSERT_TRUE(Client::Joints::StepJoints(joints, tested.random).HasValue());

    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        SCOPED_TRACE(index);
        JointState& before = expected[index];
        const glm::vec3 moved = Client::Math::OriginalVectorRotate({0.0f, -before.speed, 0.0f},
                                                                   Client::Math::OriginalAngleMatrix(before.angles));
        const float movedZ = static_cast<float>(static_cast<double>(before.position.z) + moved.z);
        const double argument =
            (static_cast<double>(before.life) + static_cast<double>(before.velocity.y)) / static_cast<double>(2);
        const std::optional<double> cosine = Client::Math::Detail::CosineProductPC53(argument, before.speed);
        const std::optional<double> sine = Client::Math::Detail::SineProductPC53(argument, before.speed);
        ASSERT_TRUE(cosine.has_value());
        ASSERT_TRUE(sine.has_value());
        const JointState& joint = joints[index];
        EXPECT_EQ(joint.position.x, static_cast<float>(*cosine + static_cast<double>(before.targetPosition.x)));
        EXPECT_EQ(joint.position.y, static_cast<float>(static_cast<double>(before.targetPosition.y) - *sine));
        EXPECT_EQ(joint.position.z,
                  static_cast<float>(static_cast<double>(before.velocity.z) + static_cast<double>(movedZ)));
        EXPECT_EQ(joint.subtype, 0);
        EXPECT_TRUE(joint.tailEnabled);
        EXPECT_EQ(joint.count, 1);
        EXPECT_EQ(joint.life, 49);
        EXPECT_EQ(joint.light, glm::vec3(3.0f));
        EXPECT_TRUE(joint.alive);
    }
    EXPECT_EQ(tested.random.State(), state);
}

// 0x445260 in scene 5: +0x16c with the global trapezoid; seen, the scale at 1, 0x602e90's draw and 0x445336's
// rand() % 30.
TEST(HelperTableTest, ASeenHorseIsDrawnAndSpendsOneRandom)
{
    HelperWorld tested;
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.Horse().scale = 0.5f;
    tested.Horse().horse.alpha = 1.0f;
    ModelBatch batch;

    ASSERT_TRUE(tested.Draw(batch, Everything).HasValue());

    EXPECT_TRUE(tested.Horse().seen);
    EXPECT_FLOAT_EQ(tested.Horse().scale, 1.0f);
    EXPECT_FALSE(batch.Commands().empty());
    EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 1).State());
}

TEST(HelperTableTest, AnIntermediateDrawProjectsTheHorseWithoutPublishingItsStateOrRandom)
{
    HelperWorld tested;
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.Horse().scale = 0.5f;
    tested.Horse().horse.alpha = 1.0f;
    tested.Horse().horse.animation.frame = 3.5f;
    tested.Rider().flags78 = 0x1000;
    const auto before = tested.random.State();
    ModelBatch batch;
    ASSERT_TRUE(tested.Draw(batch, Everything, false).HasValue());
    EXPECT_FALSE(batch.Commands().empty());
    EXPECT_EQ(tested.random.State(), before);
    EXPECT_FALSE(tested.Horse().seen);
    EXPECT_FLOAT_EQ(tested.Horse().scale, 0.5f);
    EXPECT_FLOAT_EQ(tested.Horse().horse.animation.frame, 3.5f);
}

TEST(HelperTableTest, OnlyTheOriginalHorseDrawPublishesItsPoseToTheNextFrame)
{
    HelperWorld tested;
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.Horse().horse.alpha = 1.0f;
    tested.trapezoid = Everything;
    ModelBatch batch;
    int publications = 0;
    HelperDrawContext context{.batch = batch,
                              .models = tested.models,
                              .world = tested.world,
                              .random = tested.random,
                              .trapezoid = &tested.trapezoid};
    context.onPose =
        [&](const Client::Actors::Drawing::DrawnObject& object) -> Client::Core::Expected<void, std::string>
    {
        ++publications;
        EXPECT_EQ(object.model, DarkHorseModel);
        EXPECT_EQ(object.transform.position, tested.Horse().position);
        return {};
    };
    ASSERT_TRUE(DrawHelpers(context).HasValue());
    EXPECT_EQ(publications, 1);
    context.originalFrame = false;
    ASSERT_TRUE(DrawHelpers(context).HasValue());
    EXPECT_EQ(publications, 1);
}

TEST(HelperTableTest, AnUnseenHorseOrOneWithoutAPlayerSpendsNothing)
{
    HelperWorld unseen;
    ASSERT_NO_FATAL_FAILURE(unseen.Mount());
    unseen.Horse().horse.alpha = 1.0f;
    unseen.Horse().seen = true;
    ModelBatch far;
    const TerrainPolygon elsewhere =
        Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::Game, 0, CellCentre(200, 200), -45.0f);
    ASSERT_TRUE(unseen.Draw(far, elsewhere).HasValue());
    EXPECT_FALSE(unseen.Horse().seen);
    EXPECT_TRUE(far.Commands().empty());
    EXPECT_EQ(unseen.random.State(), CrtRandom(QuietSeed).State());

    HelperWorld monster;
    ASSERT_NO_FATAL_FAILURE(monster.Mount());
    monster.Horse().horse.alpha = 1.0f;
    monster.Rider().type = 0x100;
    ModelBatch batch;
    ASSERT_TRUE(monster.Draw(batch, Everything).HasValue());
    EXPECT_TRUE(monster.Horse().seen);
    EXPECT_TRUE(batch.Commands().empty());
    EXPECT_EQ(monster.random.State(), CrtRandom(QuietSeed).State());
}

// Q-C223: with the alpha below 0.01, 0x5ffdf0 does not draw it, but 0x445336 draws its rand() anyway.
TEST(HelperTableTest, ATransparentHorseStillSpendsTheRandom)
{
    HelperWorld tested;
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.Horse().horse.alpha = 0.0099f;
    ModelBatch batch;

    ASSERT_TRUE(tested.Draw(batch, Everything).HasValue());

    EXPECT_TRUE(tested.Horse().seen);
    EXPECT_TRUE(batch.Commands().empty());
    EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 1).State());
}

// Q-C224: with the rider's +0x78 & 0x1000, 0x5ffdf0 sets the horse's frame to 0 if it draws it.
TEST(HelperTableTest, TheRidersFrozenFlagResetsTheFrameOfADrawnHorse)
{
    for (const float alpha : {1.0f, 0.0f})
    {
        SCOPED_TRACE(alpha);
        HelperWorld tested;
        ASSERT_NO_FATAL_FAILURE(tested.Mount());
        tested.Horse().horse.alpha = alpha;
        tested.Horse().horse.animation.frame = 3.5f;
        tested.Rider().flags78 = 0x1000;
        ModelBatch batch;

        ASSERT_TRUE(tested.Draw(batch, Everything).HasValue());

        EXPECT_FLOAT_EQ(tested.Horse().horse.animation.frame, alpha > 0.0f ? 0.0f : 3.5f);
    }
}

// 0x5ffdf0 on maps 18 to 23: without the hero's +0x1a1, the horse is drawn as on another map; with it, the stored alpha
// drops 0.15f on each draw of the original.
TEST(HelperTableTest, InChaosCastleTheHerosFlagFadesTheHorse)
{
    HelperWorld plain;
    ASSERT_NO_FATAL_FAILURE(plain.Mount());
    plain.Horse().horse.alpha = 1.0f;
    ModelBatch reference;
    ASSERT_TRUE(plain.Draw(reference, Everything).HasValue());

    HelperWorld chaos;
    ASSERT_NO_FATAL_FAILURE(chaos.Mount());
    chaos.world.Runtime().map = 18;
    chaos.Horse().horse.alpha = 1.0f;
    ModelBatch first;
    ASSERT_TRUE(chaos.Draw(first, Everything).HasValue());
    EXPECT_EQ(chaos.Horse().horse.alpha, 1.0f);
    EXPECT_EQ(first.Commands().size(), reference.Commands().size());

    chaos.world.Hero().byte1a1 = 1;
    ModelBatch second;
    ASSERT_TRUE(chaos.Draw(second, Everything).HasValue());
    EXPECT_EQ(chaos.Horse().horse.alpha, static_cast<float>(1.0 - static_cast<double>(0.15f)));
}

// 0x445310: with the rider's +0x78 & 0x4000, 0x602e90(slot, 0, 0, 10). 0x5ffdf0 poses it anyway (publishes the pose)
// and 0x6002e0 takes 0x60033c instead of its type's case: the model color at 1 and 0x4ecc30(mesh, 0x4040, 0.5, …,
// 0x620) on each mesh, additive with Chrome02 at 0.5 (Actors::Drawing::DrawChromeShell), without the horse's white,
// chrome or shadow, without the pets' RenderBody(2) and without the Fenrir's sprites, bolts, spark or rand(); none
// reads the terrain light, which the white overwrites. Then 0x445336's rand() % 30 + 70 and, with 0x1c12, its glow, as
// always.
TEST(HelperTableTest, TheRidersFlag4000DrawsEveryHelperAsAChromeShell)
{
    std::vector<std::uint16_t> types{DarkHorseModel};
    types.insert(types.end(), PetModels.begin(), PetModels.end());
    for (const std::uint16_t type : types)
    {
        SCOPED_TRACE(type);
        HelperWorld tested;
        if (type == DarkHorseModel)
        {
            ASSERT_NO_FATAL_FAILURE(tested.Mount());
        }
        else
        {
            ASSERT_NO_FATAL_FAILURE(tested.Ride(type));
        }
        tested.Horse().horse.alpha = 1.0f;
        tested.Rider().flags78 = 0x4000;
        tested.trapezoid = Everything;
        const CrtRandom before = tested.random;
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        HelperDrawContext context = tested.DrawContext(batch);
        context.lighting = nullptr;
        context.effects = nullptr;
        context.particles = nullptr;
        context.systemTime = {};
        context.globalPose = {};
        int publications = 0;
        context.onPose =
            [&](const Client::Actors::Drawing::DrawnObject& object) -> Client::Core::Expected<void, std::string>
        {
            ++publications;
            EXPECT_EQ(object.model, type);
            return {};
        };

        const auto drawn = DrawHelpers(context);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(publications, 1);
        ASSERT_EQ(batch.Commands().size(), 1u);
        EXPECT_EQ(batch.Commands()[0].state,
                  (Client::Render::ModelDrawState{TextureSlot{0x620}, Client::Render::ModelBlend::Additive, true}));
        ASSERT_EQ(batch.Commands()[0].vertexCount, 3u);
        for (const Client::Render::ModelVertex& corner : batch.Vertices())
        {
            EXPECT_EQ(corner.color.red, 0.5f);
            EXPECT_EQ(corner.color.green, 0.5f);
            EXPECT_EQ(corner.color.blue, 0.5f);
        }
        CrtRandom after = before;
        static_cast<void>(after.Next());
        EXPECT_EQ(tested.random.State(), after.State());
        EXPECT_EQ(tested.LiveSprites().size(), type == GuardianAngelModel ? 1u : 0u);
    }
}

// 0x600343..0x600399: the 10 path looks for the rider in the pool; if it is not the hero, with the siege (0x4294b0) and
// its +0x370 other than 1, it leaves through 0x602e7d without drawing. With the hero as rider, without the siege or
// with +0x370 at 1, it draws. The pose (0x5ffdf0) and 0x445336's rand() run the same.
TEST(HelperTableTest, InTheSiegeTheFlag4000HidesTheHelperOfAnotherRiderUnlessItsByte370IsOne)
{
    struct Case
    {
        bool riderIsHero;
        bool siege;
        std::uint8_t byte370;
        bool drawn;
    };
    for (const Case probe : {Case{false, true, 0, false}, Case{false, true, 2, false}, Case{false, true, 1, true},
                             Case{false, false, 0, true}, Case{true, true, 0, true}})
    {
        SCOPED_TRACE(testing::Message() << probe.riderIsHero << " " << probe.siege << " " << int{probe.byte370});
        HelperWorld tested;
        ASSERT_NO_FATAL_FAILURE(tested.Mount());
        tested.Horse().horse.alpha = 1.0f;
        tested.Rider().flags78 = 0x4000;
        tested.Rider().byte370 = probe.byte370;
        tested.world.Runtime().castleSiege.siegeActive = probe.siege;
        if (!probe.riderIsHero)
        {
            tested.world.Runtime().hero = OtherSlot;
        }
        tested.trapezoid = Everything;
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        HelperDrawContext context = tested.DrawContext(batch);
        int publications = 0;
        context.onPose = [&](const Client::Actors::Drawing::DrawnObject&) -> Client::Core::Expected<void, std::string>
        {
            ++publications;
            return {};
        };

        ASSERT_TRUE(DrawHelpers(context).HasValue());

        EXPECT_EQ(publications, 1);
        EXPECT_EQ(batch.Commands().size(), probe.drawn ? 1u : 0u);
        EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 1).State());
    }
}

// Q-C293: in scene 4 (0x6342f4) 0x444010 does not turn off a dead rider's helper: the alpha rises, the horse follows
// it, draws the breath's rand() % 3 and turns off its live byte with the rider's (0x44475c). In 5 it turns it off
// before everything, without rand().
TEST(HelperTableTest, TheSelectionStepRunsTheBodyOfAHorseWithoutItsRider)
{
    HelperWorld selection(54, QuietSeed);
    ASSERT_NO_FATAL_FAILURE(selection.Mount());
    selection.Rider().alive = false;
    selection.Rider().position = CellCentre(120, 130);

    const auto stepped = selection.Step(SceneId::CharacterSelect);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const HelperState& horse = selection.Horse();
    EXPECT_FALSE(horse.alive);
    EXPECT_EQ(horse.position, CellCentre(120, 130));
    EXPECT_EQ(horse.horse.alpha, std::bit_cast<float>(0x3dcccccdu));
    EXPECT_FLOAT_EQ(horse.horse.animation.frame, 0.3f);
    EXPECT_EQ(selection.random.State(), RandomAfter(QuietSeed, 1).State());

    HelperWorld game(54, QuietSeed);
    ASSERT_NO_FATAL_FAILURE(game.Mount());
    game.Rider().alive = false;
    ASSERT_TRUE(game.Step().HasValue());
    EXPECT_FALSE(game.Horse().alive);
    EXPECT_EQ(game.Horse().horse.alpha, 0.0f);
    EXPECT_EQ(game.random.State(), CrtRandom(QuietSeed).State());
}

// 0x444010 stops at 0x444083 with a type without a body (0x443d70 does not build it) and at 0x444010 without the type's
// model, the Skill\ one (0x615700) or Player\Helper01.bmd (0x60ff50).
TEST(HelperTableTest, APetNeedsItsBodyAndItsModel)
{
    HelperWorld unknown;
    unknown.world.Helpers()[0] = HelperState{.alive = true, .type = 0xae, .rider = HeroSlot};
    EXPECT_TRUE(HasError(unknown.Step(), "0x444083"));

    for (const std::uint16_t type : PetModels)
    {
        SCOPED_TRACE(type);
        HelperWorld tested;
        ASSERT_NO_FATAL_FAILURE(tested.Ride(type));
        const auto stepped =
            StepHelpers(HelperStepContext{.world = tested.world, .models = nullptr, .random = tested.random});
        EXPECT_TRUE(HasError(stepped, type == GuardianAngelModel ? "0x60ff50" : "0x615700"));
    }
}

// 0x4447c4..0x444ac4 with the rider's gallop (0x0d): the Uniria takes its place and angles, action 2 and speed 0.34
// (0x444ab6), and with rand() % 2 at 0 Lorencia's 0x5f3 dust around, with the same rand() as the horse
// (TheGallopRaisesTheDustOfItsMap). The alpha goes to 1 with the rider's +0x80 at 0.
TEST(HelperTableTest, TheUniriaFollowsItsRiderAndRaisesTheDustOfItsGallop)
{
    HelperWorld tested(0, DustSeed);
    ASSERT_NO_FATAL_FAILURE(tested.Ride(UniriaModel));
    tested.Rider().animation.action = 0x0d;

    const auto stepped = tested.Step();

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const HelperState& uniria = tested.Horse();
    EXPECT_TRUE(uniria.alive);
    EXPECT_EQ(uniria.position, tested.Rider().position);
    EXPECT_EQ(uniria.angles, tested.Rider().angles);
    EXPECT_EQ(uniria.horse.animation.action, 2u);
    EXPECT_EQ(uniria.horse.speed, std::bit_cast<float>(0x3eae147bu));
    EXPECT_EQ(uniria.horse.animation.frame, std::bit_cast<float>(0x3eae147bu));
    EXPECT_EQ(uniria.horse.alpha, 1.0f);
    const Client::Particles::Particle& dust = tested.world.Particles().All()[0];
    EXPECT_TRUE(dust.alive);
    EXPECT_EQ(dust.type, Client::Particles::DustParticle);
    EXPECT_EQ(dust.subtype, 0);
    EXPECT_EQ(dust.position, (glm::vec3{10045.0f, 10033.0f, 157.0f}));
    EXPECT_EQ(dust.light, glm::vec3(1.0f));
    EXPECT_EQ(tested.random.State(), RandomAfter(DustSeed, 8).State());

    // 0x4449d6 and 0x4449f1: in Icarus, or with 0x4000 in the rider's +0x78, only the rand() % 2.
    for (const auto& [map, flags] : {std::pair{10, 0u}, std::pair{0, 0x4000u}})
    {
        SCOPED_TRACE(map);
        HelperWorld calm(map, DustSeed);
        ASSERT_NO_FATAL_FAILURE(calm.Ride(UniriaModel));
        calm.Rider().animation.action = 0x0d;
        calm.Rider().flags78 = flags;
        ASSERT_TRUE(calm.Step().HasValue());
        EXPECT_FALSE(calm.world.Particles().All()[0].alive);
        EXPECT_EQ(calm.random.State(), RandomAfter(DustSeed, 1).State());
    }
}

// 0x444867..0x4449b7: the action of the Uniria and the Dinorant from the rider's and the map, and the Dinorant's z: −10
// in Tarkan (8) and Icarus (10), −30 elsewhere, and the rider's with the world at −1. Only the gallop draws its
// rand() % 2 (with seed 1, no dust).
TEST(HelperTableTest, TheRidingHelpersTakeTheirActionAndHeightFromTheRiderAndTheMap)
{
    struct Case
    {
        std::uint16_t type;
        std::int32_t map;
        std::uint8_t riderAction;
        std::uint8_t action;
        float z;
        int draws;
    };
    constexpr std::array<Case, 14> Cases{{
        {UniriaModel, 0, 0x0d, 2, 170.0f, 1},
        {DinorantModel, 0, 0x21, 2, 140.0f, 1},
        {DinorantModel, 8, 0x46, 3, 160.0f, 1},
        {DinorantModel, 10, 0x47, 3, 160.0f, 1},
        {UniriaModel, 8, 0x0d, 2, 170.0f, 1},
        {UniriaModel, 10, 0x40, 7, 170.0f, 0},
        {DinorantModel, 0, 0x41, 6, 140.0f, 0},
        {UniriaModel, 0, 0x22, 3, 170.0f, 0},
        {DinorantModel, 0, 0x37, 4, 140.0f, 0},
        {DinorantModel, 8, 0x30, 5, 160.0f, 0},
        {UniriaModel, 8, StandingRider, 0, 170.0f, 0},
        {DinorantModel, 10, StandingRider, 1, 160.0f, 0},
        {DinorantModel, 2, StandingRider, 0, 140.0f, 0},
        {DinorantModel, -1, StandingRider, 0, 170.0f, 0},
    }};
    for (const Case& probe : Cases)
    {
        SCOPED_TRACE(::testing::Message()
                     << probe.type << " en " << probe.map << " con " << static_cast<int>(probe.riderAction));
        HelperWorld tested(probe.map, QuietSeed);
        ASSERT_NO_FATAL_FAILURE(tested.Ride(probe.type));
        tested.Rider().animation.action = probe.riderAction;

        ASSERT_TRUE(tested.Step().HasValue());

        const HelperState& helper = tested.Horse();
        EXPECT_EQ(helper.horse.animation.action, probe.action);
        EXPECT_EQ(helper.position.z, probe.z);
        EXPECT_EQ(helper.position.x, tested.Rider().position.x);
        EXPECT_EQ(helper.horse.animation.frame, std::bit_cast<float>(0x3eae147bu));
        EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, probe.draws).State());
    }
}

// 0x44480b..0x44484a: with the rider's +0x80 at 1 or 2, the alpha 0x4e9980 left drops 0.1f; with another, 1.
TEST(HelperTableTest, TheRidersByte80FadesTheRidingHelpers)
{
    struct Case
    {
        std::uint8_t byte80;
        float alpha;
        std::uint32_t expected;
    };
    for (const Case& probe :
         {Case{1, 0.5f, 0x3ee66667u}, Case{2, 0.05f, 0x3d3851eau}, Case{1, 0.0f, 0x0u}, Case{3, 0.5f, 0x3f800000u}})
    {
        for (const std::uint16_t type : {UniriaModel, std::uint16_t{0x14b}})
        {
            SCOPED_TRACE(::testing::Message()
                         << type << " con " << static_cast<int>(probe.byte80) << " y " << probe.alpha);
            HelperWorld tested;
            ASSERT_NO_FATAL_FAILURE(tested.Ride(type));
            tested.Horse().horse.alpha = probe.alpha;
            tested.Rider().byte80 = probe.byte80;
            ASSERT_TRUE(tested.Step().HasValue());
            EXPECT_EQ(tested.Horse().horse.alpha, std::bit_cast<float>(probe.expected));
        }
    }
}

// 0x4447e7..0x444806 and 0x444b13..0x444b36: on the rider's safe cell the alpha goes to 0 and the helper advances with
// the action and speed it had, without moving or drawing rand(). The Uniria does not copy the rider's live byte there.
TEST(HelperTableTest, InTheRidersSafeCellTheRidingHelpersHide)
{
    for (const std::uint16_t type : {UniriaModel, DinorantModel, std::uint16_t{0x14a}})
    {
        SCOPED_TRACE(type);
        HelperWorld tested(0, BreathSeed);
        ASSERT_NO_FATAL_FAILURE(tested.Ride(type));
        tested.Horse().position = {1.0f, 2.0f, 3.0f};
        tested.Horse().horse.alpha = 0.7f;
        tested.Cell(100, 100) = 1;
        tested.Rider().animation.action = 0x0d;

        ASSERT_TRUE(tested.Step(SceneId::CharacterSelect).HasValue());

        const HelperState& helper = tested.Horse();
        EXPECT_TRUE(helper.alive);
        EXPECT_EQ(helper.horse.alpha, 0.0f);
        EXPECT_EQ(helper.position, (glm::vec3{1.0f, 2.0f, 3.0f}));
        EXPECT_EQ(helper.horse.animation.action, 0u);
        EXPECT_FLOAT_EQ(helper.horse.animation.frame, 0.5f);
        EXPECT_EQ(tested.random.State(), CrtRandom(BreathSeed).State());
    }

    HelperWorld noMap;
    ASSERT_NO_FATAL_FAILURE(noMap.Ride(DinorantModel));
    noMap.terrain.attributes.reset();
    EXPECT_TRUE(HasError(noMap.Step(), "0x4447e7"));
}

// 0x444010(helper, 1) from the letter portrait (0x495796): a helper outside the table, with its rider outside the pool
// (slot ActorPoolSize), reads the rider's safe cell but does not hide (0x4440eb, 0x4447f9 and 0x444b25): it does the
// same, with the same rand(), as a table one with its rider off the cell. The table is not touched. Without the live
// byte, nothing (0x444021).
TEST(HelperTableTest, TheLetterPortraitHelperFollowsItsRiderInTheSafeCell)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint16_t type : {DarkHorseModel, UniriaModel, DinorantModel, std::uint16_t{0x14a}})
        {
            SCOPED_TRACE(testing::Message() << map << " " << type);
            const std::uint8_t action = type == DarkHorseModel ? GestureRider : std::uint8_t{0x0d};
            HelperWorld table(map);
            ASSERT_NO_FATAL_FAILURE(table.Ride(type));
            table.Rider().position = CellCentre(7, 9);
            table.Rider().animation.action = action;
            ASSERT_TRUE(table.Step().HasValue());

            HelperWorld portrait(map);
            ActorState rider = portrait.Rider();
            rider.position = CellCentre(7, 9);
            rider.animation.action = action;
            portrait.Cell(7, 9) = 1;
            HelperState helper;
            const auto built =
                ConstructHelper(helper, type, portrait.Rider().position, rider, ActorPoolSize, map, &portrait.random);
            ASSERT_TRUE(built.HasValue()) << built.Error();
            ASSERT_TRUE(built.Value());
            const auto stepped = StepTheHelperOutsideTheTable(helper, rider,
                                                              HelperStepContext{.world = portrait.world,
                                                                                .models = &portrait.models,
                                                                                .random = portrait.random,
                                                                                .terrain = &portrait.terrain});
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();

            const HelperState& expected = table.Horse();
            EXPECT_TRUE(helper.alive);
            EXPECT_EQ(helper.rider, ActorPoolSize);
            EXPECT_EQ(helper.position, expected.position);
            EXPECT_EQ(helper.angles, expected.angles);
            EXPECT_EQ(helper.horse.alpha, expected.horse.alpha);
            EXPECT_EQ(helper.horse.animation.action, expected.horse.animation.action);
            EXPECT_EQ(helper.horse.animation.frame, expected.horse.animation.frame);
            EXPECT_GT(helper.horse.alpha, 0.0f);
            EXPECT_EQ(portrait.random.State(), table.random.State());
            EXPECT_FALSE(portrait.Horse().alive);

            helper.alive = false;
            const HelperState dead = helper;
            const auto before = portrait.random.State();
            ASSERT_TRUE(StepTheHelperOutsideTheTable(helper, rider,
                                                     HelperStepContext{.world = portrait.world,
                                                                       .models = &portrait.models,
                                                                       .random = portrait.random,
                                                                       .terrain = &portrait.terrain})
                            .HasValue());
            EXPECT_EQ(helper.position, dead.position);
            EXPECT_EQ(helper.horse.animation.frame, dead.horse.animation.frame);
            EXPECT_EQ(portrait.random.State(), before);
        }
    }
}

// 0x445260(helper, 1) from the letter portrait (0x4959cc): seen without checking the trapezoid (0x4452a4), with the
// scale at 0x3f3ae148 whatever the type (0x4452d2, Q-C1010), drawn and with 0x445336's rand() % 30 + 70. The table is
// not touched. Without the live byte, nothing (0x44526b).
TEST(HelperTableTest, TheLetterPortraitHelperIsAlwaysSeenAndDrawnAtItsOwnScale)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint16_t type : {DarkHorseModel, UniriaModel, DinorantModel})
        {
            SCOPED_TRACE(testing::Message() << map << " " << type);
            HelperWorld tested(map);
            ActorState rider = tested.Rider();
            HelperState helper;
            const auto built = ConstructHelper(helper, type, rider.position, rider, ActorPoolSize, map, nullptr);
            ASSERT_TRUE(built.HasValue()) << built.Error();
            helper.horse.alpha = 1.0f;
            tested.trapezoid =
                Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::Game, 0, CellCentre(200, 200), -45.0f);
            ModelBatch batch;
            batch.Begin(Client::Render::Pass3D{});

            const auto drawn = DrawTheHelperOutsideTheTable(helper, rider, tested.DrawContext(batch));

            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
            EXPECT_TRUE(helper.seen);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(helper.scale), 0x3f3ae148u);
            EXPECT_FALSE(batch.Commands().empty());
            EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 1).State());
            EXPECT_FALSE(tested.Horse().seen);

            helper.alive = false;
            helper.seen = false;
            ModelBatch none;
            none.Begin(Client::Render::Pass3D{});
            ASSERT_TRUE(DrawTheHelperOutsideTheTable(helper, rider, tested.DrawContext(none)).HasValue());
            EXPECT_FALSE(helper.seen);
            EXPECT_TRUE(none.Commands().empty());
            EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 1).State());
        }
    }
}

// 0x444ac0: the Uniria and the Dinorant keep the rider's live byte, which in scene 4 can be dead.
TEST(HelperTableTest, TheRidingHelpersCopyTheRidersAliveByte)
{
    HelperWorld tested(54, QuietSeed);
    ASSERT_NO_FATAL_FAILURE(tested.Ride(DinorantModel));
    tested.Rider().alive = false;

    ASSERT_TRUE(tested.Step(SceneId::CharacterSelect).HasValue());

    EXPECT_FALSE(tested.Horse().alive);
    EXPECT_EQ(tested.Horse().horse.alpha, 1.0f);
}

// 0x444bbc..0x444c4b: the Fenrir's action and speed from the rider's. The still ones (3, 4, 5 and 0, at 0.4) go
// straight to the advance; the run (1, at 1), the trot (2, at 0.6) and the others (action and speed untouched) go on to
// the dust, which outside 10 draws its rand() % 3 (with seed 1, no dust).
TEST(HelperTableTest, TheFenrirTakesItsGaitFromTheRider)
{
    struct Case
    {
        std::uint8_t riderAction;
        std::uint8_t action;
        std::uint32_t speed;
        int draws;
    };
    constexpr std::array<Case, 14> Cases{{
        {0x56, 3, 0x3ecccccdu, 0},
        {0x61, 3, 0x3ecccccdu, 0},
        {0x62, 4, 0x3ecccccdu, 0},
        {0x65, 4, 0x3ecccccdu, 0},
        {0x66, 5, 0x3ecccccdu, 0},
        {0x69, 5, 0x3ecccccdu, 0},
        {0x76, 0, 0x3ecccccdu, 0},
        {0x79, 0, 0x3ecccccdu, 0},
        {0xc3, 0, 0x3ecccccdu, 0},
        {0x7a, 1, 0x3f800000u, 1},
        {0x7d, 1, 0x3f800000u, 1},
        {0x6a, 2, 0x3f19999au, 1},
        {0x75, 2, 0x3f19999au, 1},
        {StandingRider, 6, 0x3f000000u, 1},
    }};
    for (const Case& probe : Cases)
    {
        SCOPED_TRACE(static_cast<int>(probe.riderAction));
        HelperWorld tested(0, QuietSeed);
        ASSERT_NO_FATAL_FAILURE(tested.Ride(0x14c));
        tested.Horse().horse.animation.action = 6;
        tested.Rider().animation.action = probe.riderAction;

        ASSERT_TRUE(tested.Step().HasValue());

        const HelperState& fenrir = tested.Horse();
        EXPECT_TRUE(fenrir.alive);
        EXPECT_EQ(fenrir.position, tested.Rider().position);
        EXPECT_EQ(fenrir.angles, tested.Rider().angles);
        EXPECT_EQ(fenrir.horse.alpha, 1.0f);
        EXPECT_EQ(fenrir.horse.animation.action, probe.action);
        EXPECT_EQ(fenrir.horse.speed, std::bit_cast<float>(probe.speed));
        EXPECT_EQ(fenrir.horse.animation.frame, std::bit_cast<float>(probe.speed));
        EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, probe.draws).State());
    }
}

// 0x444e1b..0x444f25: outside 10, with rand() % 3 at 0, outside Kalima and without the rider's 0x4000, the dust around
// the Fenrir: 0x5f2 in Devias, at (0, −26, −13) with seed 2, and 0x5f3 elsewhere. The Fenrir does not copy the
// rider's live byte.
TEST(HelperTableTest, TheRunningFenrirRaisesTheDustOfItsMap)
{
    HelperWorld devias(2, BreathSeed);
    ASSERT_NO_FATAL_FAILURE(devias.Ride(0x14a));
    devias.Rider().animation.action = 0x7a;
    const auto inDevias = devias.Step(SceneId::CharacterSelect);
    ASSERT_TRUE(inDevias.HasValue()) << inDevias.Error();
    const Client::Particles::Particle& smoke = devias.world.Particles().All()[0];
    EXPECT_TRUE(smoke.alive);
    EXPECT_EQ(smoke.type, Client::Particles::BreathParticle);
    EXPECT_EQ(smoke.subtype, 0);
    EXPECT_EQ(smoke.position, (glm::vec3{10050.0f, 10024.0f, 157.0f}));
    EXPECT_EQ(smoke.light, glm::vec3(1.0f));
    EXPECT_EQ(devias.random.State(), RandomAfter(BreathSeed, 6).State());

    HelperWorld lorencia(0, BreathSeed);
    ASSERT_NO_FATAL_FAILURE(lorencia.Ride(0x14a));
    lorencia.Rider().animation.action = 0x6a;
    lorencia.Rider().alive = false;
    ASSERT_TRUE(lorencia.Step(SceneId::CharacterSelect).HasValue());
    EXPECT_TRUE(lorencia.Horse().alive);
    EXPECT_EQ(lorencia.world.Particles().All()[0].type, Client::Particles::DustParticle);
    EXPECT_EQ(lorencia.random.State(), RandomAfter(BreathSeed, 8).State());

    for (const auto& [map, flags] : {std::pair{24, 0u}, std::pair{36, 0u}, std::pair{0, 0x4000u}})
    {
        SCOPED_TRACE(map);
        HelperWorld calm(map, BreathSeed);
        ASSERT_NO_FATAL_FAILURE(calm.Ride(0x14a));
        calm.Rider().animation.action = 0x7a;
        calm.Rider().flags78 = flags;
        ASSERT_TRUE(calm.Step().HasValue());
        EXPECT_FALSE(calm.world.Particles().All()[0].alive);
        EXPECT_EQ(calm.random.State(), RandomAfter(BreathSeed, 1).State());
    }
}

// 0x444c78..0x444e16, the Fenrir in Icarus: no rand() before the frame. Between 1 and 1.2, bone 22 of the global buffer
// with (120, 0, 32) and 40 more in x; between 4.8 and 5 (closed above), 44 with 40 more in x and 700 more in z
// (Q-C421). The 0x6eb/1 wave unshifted, with its scale rand(), and the 0x5f2 (PuffTheIcarusDust): with seed 1, eight
// and 50 rand(). Outside the windows, nothing.
TEST(HelperTableTest, InIcarusTheFenrirRaisesAWaveAndPuffsFromTwoBones)
{
    struct Case
    {
        float frame;
        std::optional<glm::vec3> base;
    };
    const std::array<Case, 7> Cases{{
        {1.1f, glm::vec3{10182.0f, 10122.0f, 224.0f}},
        {4.9f, glm::vec3{10204.0f, 10144.0f, 946.0f}},
        {5.0f, glm::vec3{10204.0f, 10144.0f, 946.0f}},
        {1.0f, std::nullopt},
        {std::bit_cast<float>(0x3f99999au), std::nullopt},
        {std::bit_cast<float>(0x4099999au), std::nullopt},
        {5.01f, std::nullopt},
    }};
    for (const Case& probe : Cases)
    {
        SCOPED_TRACE(probe.frame);
        HelperWorld tested(10, QuietSeed);
        ASSERT_NO_FATAL_FAILURE(tested.Ride(0x14b));
        tested.PoseTheGlobalBones(45);
        tested.Horse().horse.animation.action = 1;
        tested.Horse().horse.animation.frame = probe.frame;
        tested.Rider().animation.action = 0x7a;
        tested.Rider().flags78 = 0x4000;

        const auto stepped = tested.Step();

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const Client::Effects::ModelEffect& wave = tested.world.Map().effects.Effects()[0];
        if (!probe.base)
        {
            EXPECT_FALSE(wave.alive);
            EXPECT_FALSE(tested.world.Particles().All()[0].alive);
            EXPECT_EQ(tested.random.State(), CrtRandom(QuietSeed).State());
            continue;
        }
        EXPECT_TRUE(wave.alive);
        EXPECT_EQ(wave.type, Client::Effects::ShockWaveEffect);
        EXPECT_EQ(wave.subtype, 1);
        EXPECT_EQ(wave.transform.position, *probe.base);
        EXPECT_EQ(wave.transform.angles, (glm::vec3{0.0f, 0.0f, 45.0f}));
        EXPECT_EQ(wave.transform.scale, std::bit_cast<float>(0x3f8ccccdu));
        const Client::Particles::Particle& first = tested.world.Particles().All()[0];
        EXPECT_EQ(first.type, Client::Particles::BreathParticle);
        EXPECT_EQ(first.position, *probe.base + glm::vec3(9.0f, -25.0f, -17.0f));
        const Client::Particles::Particle& last = tested.world.Particles().All()[7];
        EXPECT_TRUE(last.alive);
        EXPECT_EQ(last.position, *probe.base + glm::vec3(-2.0f, -11.0f, -13.0f));
        EXPECT_FALSE(tested.world.Particles().All()[8].alive);
        EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 50).State());
    }
}

// 0x444f2a..0x444fff: without the rider's 0x4000, four 0x5c5/1 with light 0.4 at (rand() & 15) − 8 from the Guardian
// Angel on each axis, with each creation's two rand(). After the advance, the flight (0x445052..0x445221): near the
// rider it does not turn, moves with its velocity rotated by its angles and (rand() & 15) − 8 in z, and draws the
// rand() % 32 (with seed 1, 12: no new course). At 150 above the rider, without raising or lowering the velocity.
TEST(HelperTableTest, TheGuardianAngelSparklesAndFliesWithItsVelocity)
{
    HelperWorld tested(0, QuietSeed);
    HelperState& angel = tested.Horse();
    angel = HelperState{.alive = true,
                        .type = GuardianAngelModel,
                        .scale = 0.7f,
                        .position = {10050.0f, 10050.0f, 320.0f},
                        .angles = {0.0f, 0.0f, 45.0f},
                        .velocity = {0.0f, -5.0f, 0.0f},
                        .rider = HeroSlot};

    const auto stepped = tested.Step();

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    constexpr std::array<glm::vec3, 4> Sparks{{
        {10051.0f, 10045.0f, 326.0f},
        {10054.0f, 10048.0f, 326.0f},
        {10051.0f, 10043.0f, 313.0f},
        {10053.0f, 10045.0f, 318.0f},
    }};
    for (std::size_t index = 0; index < Sparks.size(); ++index)
    {
        SCOPED_TRACE(index);
        const Client::Particles::Particle& spark = tested.world.Particles().All()[index];
        EXPECT_TRUE(spark.alive);
        EXPECT_EQ(spark.type, Client::Particles::FairySparkParticle);
        EXPECT_EQ(spark.subtype, 1);
        EXPECT_EQ(spark.position, Sparks[index]);
        EXPECT_EQ(spark.light, glm::vec3(std::bit_cast<float>(0x3ecccccdu)));
    }
    EXPECT_FALSE(tested.world.Particles().All()[4].alive);
    const glm::vec3 move =
        Client::Math::OriginalVectorRotate({0.0f, -5.0f, 0.0f}, Client::Math::OriginalAngleMatrix({0.0f, 0.0f, 45.0f}));
    EXPECT_EQ(angel.angles, (glm::vec3{0.0f, 0.0f, 45.0f}));
    EXPECT_EQ(angel.position.x, static_cast<float>(static_cast<double>(move.x) + 10050.0));
    EXPECT_EQ(angel.position.y, static_cast<float>(static_cast<double>(move.y) + 10050.0));
    EXPECT_EQ(angel.position.z, 319.0f);
    EXPECT_EQ(angel.velocity, (glm::vec3{0.0f, -5.0f, 0.0f}));
    EXPECT_EQ(angel.horse.alpha, std::bit_cast<float>(0x3dcccccdu));
    EXPECT_FLOAT_EQ(angel.horse.animation.frame, 0.5f);
    EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 22).State());
}

// 0x444f32..0x444f41: with 0x4000 in the rider's +0x78, no sparkles. 0x445117..0x4451cb with rand() % 32 at 0 (seed
// 2): up close, the y velocity ((rand() & 63) + 16) · −0.1f, the course rand() % 360, 0 in x and
// ((rand() & 63) − 32) · 0.1f in z, which stays the same between 100 and 200 above the rider.
TEST(HelperTableTest, NearItsRiderTheGuardianAngelTakesANewCourse)
{
    HelperWorld tested(0, 2);
    tested.Rider().flags78 = 0x4000;
    HelperState& angel = tested.Horse();
    angel = HelperState{.alive = true,
                        .type = GuardianAngelModel,
                        .position = {10050.0f, 10050.0f, 320.0f},
                        .angles = {0.0f, 0.0f, 45.0f},
                        .rider = HeroSlot};

    ASSERT_TRUE(tested.Step().HasValue());

    EXPECT_FALSE(tested.world.Particles().All()[0].alive);
    EXPECT_EQ(angel.position, (glm::vec3{10050.0f, 10050.0f, 325.0f}));
    EXPECT_EQ(angel.angles, (glm::vec3{0.0f, 0.0f, 155.0f}));
    EXPECT_EQ(angel.velocity.x, 0.0f);
    EXPECT_EQ(angel.velocity.y, static_cast<float>(-2.2000000476837158));
    EXPECT_EQ(angel.velocity.z, static_cast<float>(1.2000000476837158));
    EXPECT_EQ(tested.random.State(), RandomAfter(2, 5).State());
}

// 0x445052..0x4450b4: at 150 or more from the rider it turns 20 degrees toward where it was at the step's start
// (0x4e93f0 and 0x4e9560), and with rand() % 32 at 0 (seed 48, after the four sparkles) it takes the far velocity,
// ((rand() & 63) + 128) · −0.1f, without a new course. Below 100 above the rider, the z one rises 1.5; above 200, it
// drops 1.5.
TEST(HelperTableTest, FarFromItsRiderTheGuardianAngelTurnsAndClimbs)
{
    HelperWorld tested(0, 48);
    HelperState& angel = tested.Horse();
    angel = HelperState{.alive = true,
                        .type = GuardianAngelModel,
                        .position = {11050.0f, 10050.0f, 100.0f},
                        .angles = {0.0f, 0.0f, 45.0f},
                        .velocity = {0.0f, -5.0f, 0.0f},
                        .rider = HeroSlot};

    ASSERT_TRUE(tested.Step().HasValue());

    const float course = static_cast<float>(Client::Math::CreateAngle(11050.0f, 10050.0f, 10050.0f, 10050.0f));
    const float turned = static_cast<float>(Client::Math::TurnAngle(45.0f, course, 20.0f));
    EXPECT_NE(turned, 45.0f);
    EXPECT_EQ(angel.angles, (glm::vec3{0.0f, 0.0f, turned}));
    const glm::vec3 move = Client::Math::OriginalVectorRotate({0.0f, -5.0f, 0.0f},
                                                              Client::Math::OriginalAngleMatrix({0.0f, 0.0f, turned}));
    EXPECT_EQ(angel.position.x, static_cast<float>(static_cast<double>(move.x) + 11050.0));
    EXPECT_EQ(angel.position.z, 100.0f);
    EXPECT_EQ(angel.velocity.x, 0.0f);
    EXPECT_EQ(angel.velocity.y, static_cast<float>(-15.90000057220459));
    EXPECT_EQ(angel.velocity.z, 3.0f);
    EXPECT_EQ(tested.random.State(), RandomAfter(48, 24).State());

    HelperWorld high(0, QuietSeed);
    high.Rider().flags78 = 0x4000;
    high.Horse() = HelperState{.alive = true,
                               .type = GuardianAngelModel,
                               .position = {10050.0f, 10050.0f, 400.0f},
                               .velocity = {0.0f, 0.0f, 1.0f},
                               .rider = HeroSlot};
    ASSERT_TRUE(high.Step().HasValue());
    EXPECT_EQ(high.Horse().position.z, 402.0f);
    EXPECT_EQ(high.Horse().velocity.z, -0.5f);
}

// Q-C420: 0x443d70 does not write the velocity (+0xcc): a Guardian Angel created in a used slot keeps the previous
// helper's. Its three place rand() go with the shared rand().
TEST(HelperTableTest, AReusedSlotKeepsTheVelocityOfThePreviousHelper)
{
    HelperWorld tested(0, QuietSeed);
    tested.Horse().velocity = {1.0f, 2.0f, 3.0f};

    ASSERT_NO_FATAL_FAILURE(tested.Ride(GuardianAngelModel));

    const HelperState& angel = tested.Horse();
    EXPECT_EQ(angel.velocity, (glm::vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_FLOAT_EQ(angel.scale, 0.7f);
    EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 3).State());
}

// Q-C225 also for the pets: in scene 5, with the rider without the 1 in +0x88, 0x444010 turns them off without rand().
TEST(HelperTableTest, TheGameStepTurnsOffAPetWithoutItsRider)
{
    for (const std::uint16_t type : PetModels)
    {
        SCOPED_TRACE(type);
        HelperWorld tested(0, BreathSeed);
        ASSERT_NO_FATAL_FAILURE(tested.Ride(type));
        const auto drawn = tested.random.State();
        tested.Rider().byte88 = 0;
        ASSERT_TRUE(tested.Step().HasValue());
        EXPECT_FALSE(tested.Horse().alive);
        EXPECT_EQ(tested.random.State(), drawn);
    }
}

// 0x445260 → 0x602e90 with the Uniria and the Dinorant in the game: the scale at 1 (0x445309), 0x5ffce0's color with
// the per-vertex light (the terrain's plus +0xf4's 3) and RenderBody(2) without blendMesh, with its light at 1, U and V
// at 0 and no hidden mesh (0x600b35 and 0x602360). Then, only 0x445336's rand() % 30.
TEST(HelperTableTest, TheUniriaAndTheDinorantDrawWithTheLightOfTheTerrain)
{
    for (const std::uint16_t type : {UniriaModel, DinorantModel})
    {
        SCOPED_TRACE(type);
        HelperWorld tested;
        ASSERT_NO_FATAL_FAILURE(tested.Ride(type));
        EXPECT_FLOAT_EQ(tested.Horse().scale, 0.9f);
        tested.Horse().horse.alpha = 1.0f;
        tested.trapezoid = Everything;
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        std::vector<Client::Actors::Drawing::DrawnObject> posed;
        HelperDrawContext context = tested.DrawContext(batch);
        context.onPose =
            [&](const Client::Actors::Drawing::DrawnObject& object) -> Client::Core::Expected<void, std::string>
        {
            posed.push_back(object);
            return {};
        };

        ASSERT_TRUE(DrawHelpers(context).HasValue());

        EXPECT_FLOAT_EQ(tested.Horse().scale, 1.0f);
        ASSERT_EQ(posed.size(), 1u);
        const Client::Actors::Drawing::DrawnObject& object = posed.front();
        EXPECT_EQ(object.model, type);
        EXPECT_EQ(object.transform.position, tested.Horse().position);
        EXPECT_EQ(object.transform.angles, tested.Rider().angles);
        EXPECT_FLOAT_EQ(object.transform.scale, 1.0f);
        const glm::vec3 terrain =
            Client::Terrain::LightAt(*tested.lighting, tested.Horse().position.x, tested.Horse().position.y);
        EXPECT_EQ(terrain, glm::vec3(0.25f, 0.5f, 0.75f));
        EXPECT_EQ(object.color, glm::vec3(3.25f, 3.5f, 3.75f));
        EXPECT_TRUE(object.vertexLight);
        EXPECT_FLOAT_EQ(object.alpha, 1.0f);
        EXPECT_EQ(object.state.blendMesh, -1);
        EXPECT_FLOAT_EQ(object.state.light, 1.0f);
        EXPECT_EQ(object.state.uv, glm::vec2(0.0f));
        EXPECT_EQ(object.state.hiddenMesh, -1);
        EXPECT_FALSE(batch.Commands().empty());
        EXPECT_TRUE(tested.LiveSprites().empty());
        EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 1).State());
    }
}

// 0x4452db..0x445309: 1.6 (0x3fcccccd) in scene 2 and 1 in 4; in 5, the Fenrir keep 0x443d70's 0.9 and the others go
// to 1. With the alpha at 0 nothing is drawn, but the scale stays.
TEST(HelperTableTest, TheDrawScaleDependsOnTheSceneAndOnTheFenrir)
{
    struct Case
    {
        SceneId scene;
        std::uint16_t type;
        std::uint32_t scale;
    };
    for (const Case& tested :
         {Case{SceneId::Game, UniriaModel, 0x3f800000u}, Case{SceneId::Game, 0x14a, 0x3f666666u},
          Case{SceneId::Game, 0x14c, 0x3f666666u}, Case{SceneId::Game, DarkHorseModel, 0x3f800000u},
          Case{SceneId::CharacterSelect, 0x14b, 0x3f800000u},
          Case{SceneId::CharacterSelect, DinorantModel, 0x3f800000u}, Case{SceneId::Login, 0x14b, 0x3fcccccdu},
          Case{SceneId::Login, UniriaModel, 0x3fcccccdu}})
    {
        SCOPED_TRACE(testing::Message() << static_cast<int>(tested.scene) << " " << tested.type);
        HelperWorld world;
        ASSERT_NO_FATAL_FAILURE(world.Ride(tested.type));
        world.Horse().horse.alpha = 0.0f;
        ModelBatch batch;

        ASSERT_TRUE(world.Draw(batch, Everything, true, tested.scene).HasValue());

        EXPECT_EQ(std::bit_cast<std::uint32_t>(world.Horse().scale), tested.scale);
        EXPECT_TRUE(batch.Commands().empty());
        EXPECT_EQ(world.random.State(), RandomAfter(QuietSeed, 1).State());
    }
}

// The Guardian Angel (0x1c12) is drawn without a player rider (0x4452c2), with the scale at 1 and its mesh 1 as
// blendMesh (+0x64 at 1, 0x443f1a). Then 0x445336 draws rand() % 30 + 70 (26500 % 30 + 70 = 80 with seed 1, after
// 0x443d70's three) and 0x445390 creates the 0x5ac sprite in its place, scale 1, with 80 · 0.01f times (0.5, 0.8, 0.6)
// on the x87 and stored as float: (0x3ecccccc, 0x3f23d70a, 0x3ef5c290). With the alpha below 0.01 there is no body,
// but there are the rand() and the sprite.
TEST(HelperTableTest, TheGuardianAngelDrawsItsBlendMeshAndLeavesItsGlow)
{
    for (const float alpha : {1.0f, 0.0f})
    {
        SCOPED_TRACE(alpha);
        HelperWorld tested;
        ASSERT_NO_FATAL_FAILURE(tested.Ride(GuardianAngelModel));
        EXPECT_EQ(tested.Horse().blendMesh, 1);
        EXPECT_EQ(tested.Horse().position, glm::vec3(9835.0f, 9829.0f, 360.0f));
        tested.Rider().type = 0x100;
        tested.Horse().horse.alpha = alpha;
        tested.trapezoid = Everything;
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        std::vector<Client::Actors::Drawing::DrawnObject> posed;
        HelperDrawContext context = tested.DrawContext(batch);
        context.onPose =
            [&](const Client::Actors::Drawing::DrawnObject& object) -> Client::Core::Expected<void, std::string>
        {
            posed.push_back(object);
            return {};
        };

        ASSERT_TRUE(DrawHelpers(context).HasValue());

        EXPECT_FLOAT_EQ(tested.Horse().scale, 1.0f);
        ASSERT_EQ(posed.size(), alpha > 0.0f ? 1u : 0u);
        if (!posed.empty())
        {
            EXPECT_EQ(posed.front().model, GuardianAngelModel);
            EXPECT_EQ(posed.front().state.blendMesh, 1);
            EXPECT_EQ(posed.front().color, glm::vec3(3.25f, 3.5f, 3.75f));
            EXPECT_FALSE(batch.Commands().empty());
        }
        EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 4).State());
        const std::vector<Client::Particles::Sprite> glow = tested.LiveSprites();
        ASSERT_EQ(glow.size(), 1u);
        EXPECT_EQ(glow[0].texture, 0x5ac);
        EXPECT_EQ(glow[0].position, glm::vec3(9835.0f, 9829.0f, 360.0f));
        EXPECT_EQ(glow[0].scale, 1.0f);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(glow[0].light.x), 0x3eccccccu);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(glow[0].light.y), 0x3f23d70au);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(glow[0].light.z), 0x3ef5c290u);
        EXPECT_EQ(glow[0].rotation, 0.0f);
        EXPECT_EQ(glow[0].blend, 0);
    }
}

// What the draw lacks: the Fenrir (0x60277b: its passes, the 0x5c6 particle, the shadow, its bones' sprites and
// 0x602b30's two 0x14d effects), which stop without 0x445336's rand(); 0x5ffce0's terrain light for the others; and
// the sprite table for 0x1c12's glow, which stops after its rand().
TEST(HelperTableTest, ThePetDrawStopsWhereMuRetHasNotArrived)
{
    // The Fenrir asks for its tables before drawing and before the first rand().
    for (int missing = 0; missing < 5; ++missing)
    {
        SCOPED_TRACE(missing);
        HelperWorld tested;
        ASSERT_NO_FATAL_FAILURE(tested.Ride(0x14a));
        tested.Horse().horse.alpha = 1.0f;
        tested.Horse().horse.animation.action = 4;
        tested.PoseTheGlobalBones(60);
        tested.trapezoid = Everything;
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        HelperDrawContext context = tested.DrawContext(batch);
        if (missing == 0)
            context.effects = nullptr;
        else if (missing == 1)
            context.particles = nullptr;
        else if (missing == 2)
            context.sprites = nullptr;
        else if (missing == 3)
            context.systemTime = {};
        else
            context.globalPose = {};
        EXPECT_TRUE(HasError(DrawHelpers(context), "0x60277b"));
        EXPECT_TRUE(batch.Commands().empty());
        EXPECT_EQ(tested.random.State(), CrtRandom(QuietSeed).State());
    }

    HelperWorld unlit;
    ASSERT_NO_FATAL_FAILURE(unlit.Ride(UniriaModel));
    unlit.Horse().horse.alpha = 1.0f;
    unlit.lighting.reset();
    ModelBatch dark;
    EXPECT_TRUE(HasError(unlit.Draw(dark, Everything), "0x5ffce0"));
    EXPECT_TRUE(dark.Commands().empty());

    HelperWorld angel;
    ASSERT_NO_FATAL_FAILURE(angel.Ride(GuardianAngelModel));
    angel.Horse().horse.alpha = 0.0f;
    angel.trapezoid = Everything;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    HelperDrawContext context = angel.DrawContext(batch);
    context.sprites = nullptr;
    EXPECT_TRUE(HasError(DrawHelpers(context), "0x445390"));
    EXPECT_EQ(angel.random.State(), RandomAfter(QuietSeed, 4).State());
}
// 0x60277b..0x602b50 with the red Fenrir (0x14b) in action 4, the clock sine p = sin(T · 0.002f) · 0.2f: the body in
// white and its shadow; the 0x5c6/15 spark from bone 14 with two rand() and its scale p · 0.05f + 0.7f; two 0x5ac
// sprites on each eye (bone 11) and two on the chest (13); and two 0x14d bolts with (0.8f, 0, 0) and their rand().
// Then, 0x445336's rand(). The expected values repeat the creations with another CRT and the test's posed global
// buffer.
TEST(HelperTableTest, TheFenrirDrawsItsSparkItsFlaresAndTwoBolts)
{
    for (const std::uint32_t seed : {QuietSeed, 2u, 3u, 5u, 8u})
    {
        SCOPED_TRACE(seed);
        HelperWorld tested(0, seed);
        ASSERT_NO_FATAL_FAILURE(tested.Ride(0x14b));
        HelperState& fenrir = tested.Horse();
        fenrir.horse.alpha = 1.0f;
        fenrir.horse.animation.action = 4;
        tested.PoseTheGlobalBones(60);
        const CrtRandom before = tested.random;
        ModelBatch batch;

        const auto drawn = tested.Draw(batch, Everything);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

        EXPECT_FALSE(batch.Commands().empty());
        const auto sine = Client::Math::Detail::SineProductPC53(
            static_cast<double>(tested.clock) * static_cast<double>(0.002f), 0.2f);
        ASSERT_TRUE(sine.has_value());
        const double pulse = static_cast<double>(static_cast<float>(*sine));
        CrtRandom draws = before;
        const int x = (draws.Next() % 10) - 10;
        const int z = (draws.Next() % 40) - 20;
        const glm::vec3 sparkAt =
            HelperWorld::PosedBone(14) + glm::vec3(static_cast<float>(x * 0.5), 0.0f, static_cast<float>(z * 0.5));
        Client::Particles::Particles sparks;
        const float sparkScale = static_cast<float>(pulse * static_cast<double>(0.05f) + static_cast<double>(0.7f));
        ASSERT_TRUE(sparks
                        .CreateChecked(0x5c6, sparkAt, fenrir.angles, glm::vec3(1.0f, 0.0f, 0.0f), 15, sparkScale,
                                       tested.clock, draws)
                        .HasValue());
        const Client::Particles::Particle& spark = tested.world.Particles().All()[0];
        ASSERT_TRUE(spark.alive);
        EXPECT_EQ(spark.subtype, 15);
        EXPECT_EQ(spark.position, sparkAt);
        EXPECT_EQ(spark.scale, sparkScale);
        EXPECT_EQ(spark.life, sparks.All()[0].life);
        EXPECT_EQ(spark.velocity, sparks.All()[0].velocity);

        Client::Effects::ModelEffects bolts;
        Client::Particles::FrameSprites boltFlares;
        const auto bonePoint = [](std::size_t bone) { return HelperWorld::PosedBone(bone); };
        for (int bolt = 0; bolt < 2; ++bolt)
            ASSERT_TRUE(bolts
                            .CreateLightning(fenrir.position, fenrir.angles,
                                             glm::vec3(std::bit_cast<float>(0x3f4ccccdu), 0.0f, 0.0f), &fenrir, draws,
                                             bonePoint, boltFlares)
                            .HasValue());
        static_cast<void>(draws.Next()); // 0x445336.
        EXPECT_EQ(tested.random.State(), draws.State());
        for (std::size_t slot = 0; slot < 2; ++slot)
        {
            const Client::Effects::ModelEffect& made = tested.world.Map().effects.Effects()[slot];
            const Client::Effects::ModelEffect& expected = bolts.Effects()[slot];
            EXPECT_EQ(made.alive, expected.alive);
            EXPECT_EQ(made.type, expected.type);
            EXPECT_EQ(made.transform.position, expected.transform.position);
            EXPECT_EQ(made.transform.scale, expected.transform.scale);
            EXPECT_EQ(made.transform.angles, expected.transform.angles);
            EXPECT_EQ(made.light, expected.light);
            EXPECT_EQ(made.ownerContext, &fenrir);
        }
        EXPECT_FALSE(tested.world.Map().effects.Effects()[2].alive);

        const auto live = tested.LiveSprites();
        ASSERT_EQ(live.size(), 8u);
        const glm::vec3 eyeLight(static_cast<float>(pulse + static_cast<double>(0.9f)),
                                 static_cast<float>(pulse * 0.5 + static_cast<double>(0.2f)),
                                 static_cast<float>(pulse * 0.5 + static_cast<double>(0.1f)));
        const float eyeScale = static_cast<float>(pulse * static_cast<double>(0.1f) + 0.5);
        const std::array<glm::vec3, 2> eyes{HelperWorld::PosedBone(11) + glm::vec3(50.0f, 2.0f, 11.0f),
                                            HelperWorld::PosedBone(11) + glm::vec3(50.0f, 2.0f, -11.0f)};
        for (std::size_t index = 0; index < 4; ++index)
        {
            EXPECT_EQ(live[index].texture, 0x5ac);
            EXPECT_EQ(live[index].position, eyes[index / 2]);
            EXPECT_EQ(live[index].scale, eyeScale);
            EXPECT_EQ(live[index].light, eyeLight);
        }
        const glm::vec3 chest = HelperWorld::PosedBone(13) + glm::vec3(40.0f, 15.0f, 0.0f);
        const glm::vec3 chestLight(1.0f, std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3e4ccccdu));
        EXPECT_EQ(live[4].position, chest);
        EXPECT_EQ(live[4].scale, 1.5f);
        EXPECT_EQ(live[4].light, chestLight);
        EXPECT_EQ(live[5].position, chest);
        EXPECT_EQ(live[5].scale, 1.0f);
        EXPECT_EQ(live[5].light, chestLight);
        for (std::size_t index = 0; index < 2; ++index)
        {
            EXPECT_EQ(live[6 + index].position, boltFlares.Sprites()[index].position);
            EXPECT_EQ(live[6 + index].light, boltFlares.Sprites()[index].light);
        }
    }
}

// Without action 4 there is no spark or chrome pass, and the bolts of the black (0x14a) and blue (0x14c) Fenrir go with
// (1, 1, 0.2f) and (0.1f, 0.1f, 0.8f). The 0x14e decals, in white, with the color subtype (3 and 2) and
// timeGetTime()'s hour: with action 1 and the frame in [0, 1.5], on bones 22, 28, 36 and 44; with 2, on 22 and 28 with
// the frame in (1, 1.4f] and on 36 and 44 in (4.8f, 5.2f]; otherwise, none.
TEST(HelperTableTest, TheFenrirLeavesItsDecalsInTheWindowsOfItsStrikes)
{
    struct Case
    {
        std::uint8_t action;
        float frame;
        std::vector<std::size_t> bones;
    };
    const std::vector<std::size_t> all{22, 28, 36, 44}, front{22, 28}, back{36, 44}, none{};
    const float justBelow = std::nextafter(0.0f, -1.0f);
    for (const Case& tested :
         {Case{1, justBelow, none}, Case{1, 0.0f, all}, Case{1, 1.5f, all}, Case{1, std::nextafter(1.5f, 2.0f), none},
          Case{2, 1.0f, none}, Case{2, std::nextafter(1.0f, 2.0f), front}, Case{2, 1.4f, front},
          Case{2, std::nextafter(1.4f, 2.0f), none}, Case{2, 4.8f, none}, Case{2, 5.0f, back}, Case{2, 5.2f, back},
          Case{2, std::nextafter(5.2f, 6.0f), none}, Case{0, 1.0f, none}, Case{3, 1.0f, none}})
    {
        for (const std::uint16_t type : {std::uint16_t{0x14a}, std::uint16_t{0x14c}})
        {
            SCOPED_TRACE(testing::Message() << static_cast<int>(tested.action) << " " << tested.frame << " " << type);
            HelperWorld world;
            ASSERT_NO_FATAL_FAILURE(world.Ride(type));
            world.Horse().horse.alpha = 1.0f;
            world.Horse().horse.animation.action = tested.action;
            world.Horse().horse.animation.frame = tested.frame;
            world.PoseTheGlobalBones(60);
            ModelBatch batch;

            const auto drawn = world.Draw(batch, Everything);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

            EXPECT_FALSE(world.world.Particles().All()[0].alive);
            const glm::vec3 boltLight =
                type == 0x14a ? glm::vec3(1.0f, 1.0f, std::bit_cast<float>(0x3e4ccccdu))
                              : glm::vec3(std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu),
                                          std::bit_cast<float>(0x3f4ccccdu));
            std::vector<glm::vec3> decals;
            for (const Client::Effects::ModelEffect& effect : world.world.Map().effects.Effects())
            {
                if (!effect.alive)
                    continue;
                if (effect.type == Client::Effects::LightningEffect)
                {
                    EXPECT_EQ(effect.light, boltLight);
                    continue;
                }
                ASSERT_EQ(effect.type, Client::Effects::LightningDecalEffect);
                EXPECT_EQ(effect.subtype, type == 0x14a ? 3 : 2);
                EXPECT_EQ(effect.light, glm::vec3(1.0f));
                EXPECT_EQ(effect.textureTime, HelperWorld::FenrirHour);
                EXPECT_EQ(effect.transform.angles, world.Horse().angles);
                decals.push_back(effect.transform.position);
            }
            std::vector<glm::vec3> expected;
            for (const std::size_t bone : tested.bones)
                expected.push_back(HelperWorld::PosedBone(bone) * glm::vec3(1.0f, 1.0f, 0.0f));
            EXPECT_EQ(decals, expected);
            EXPECT_EQ(world.LiveSprites().size(), 8u);
        }
    }
}

// The port the character set and the viewport reach the table through (Actors::IHelperHooks): 0x443d40 on the riders'
// helpers, and 0x443fa0 in the world's table with the port's rand(), as CreateHelper does it.
TEST(HelperTableTest, TheTablesPortTurnsOffAndCreatesInTheWorldsTable)
{
    HelperWorld tested;
    auto& helpers = tested.world.Helpers();
    helpers[1] = HelperState{.alive = true, .type = DarkHorseModel, .rider = HeroSlot};
    helpers[2] = HelperState{.alive = true, .type = DarkHorseModel, .rider = OtherSlot};
    const Client::Pets::TableHelpers port(tested.world, &tested.random);

    port.TurnOffTheHelpersOf(HeroSlot);

    EXPECT_FALSE(helpers[1].alive);
    EXPECT_TRUE(helpers[2].alive);
    const glm::vec3 place{1234.0f, 5678.0f, 90.0f};
    ASSERT_TRUE(port.CreateTheHelper(GuardianAngelModel, place, HeroSlot).HasValue());
    HelperWorld twin;
    ASSERT_TRUE(CreateHelper(twin.world, GuardianAngelModel, place, HeroSlot, &twin.random).HasValue());
    EXPECT_TRUE(tested.Horse().alive);
    EXPECT_EQ(tested.Horse().type, GuardianAngelModel);
    EXPECT_EQ(tested.Horse().rider, HeroSlot);
    EXPECT_EQ(tested.Horse().position, twin.Horse().position);
    EXPECT_EQ(tested.random.State(), twin.random.State());
    EXPECT_NE(tested.random.State(), CrtRandom(QuietSeed).State());
    const auto unknown = port.CreateTheHelper(0xbe, place, HeroSlot);
    ASSERT_FALSE(unknown.HasValue());
    EXPECT_NE(unknown.Error().find("0x443d70"), std::string::npos) << unknown.Error();
}

// A character outside the pool (the letters' portrait): the port turns its own helper off instead of 0x443d40 and
// builds on it with 0x443d70 on the world's map (nothing in Chaos Castle), without touching the table.
TEST(HelperTableTest, TheOwnHelpersPortTurnsOffAndBuildsOnlyItsHelper)
{
    for (const std::int32_t map : {0, 18})
    {
        SCOPED_TRACE(map);
        HelperWorld tested(map);
        const ActorState rider = tested.Rider();
        HelperState helper{.alive = true, .type = UniriaModel};
        tested.world.Helpers()[0] = HelperState{.alive = true, .type = UniriaModel, .rider = ActorPoolSize};
        const Client::Pets::OwnHelper port(helper, rider, tested.world, &tested.random);

        port.TurnOffTheHelpersOf(ActorPoolSize);

        EXPECT_FALSE(helper.alive);
        EXPECT_TRUE(tested.Horse().alive);
        ASSERT_TRUE(port.CreateTheHelper(DarkHorseModel, rider.position, ActorPoolSize).HasValue());
        HelperState expected;
        const auto built =
            ConstructHelper(expected, DarkHorseModel, rider.position, rider, ActorPoolSize, map, nullptr);
        ASSERT_TRUE(built.HasValue()) << built.Error();
        EXPECT_EQ(helper.alive, built.Value());
        EXPECT_EQ(helper.type, built.Value() ? DarkHorseModel : UniriaModel);
        EXPECT_EQ(helper.position, expected.position);
        EXPECT_EQ(helper.angles, expected.angles);
        EXPECT_EQ(tested.Horse().type, UniriaModel);
        EXPECT_TRUE(tested.Horse().alive);
    }
}
} // namespace
