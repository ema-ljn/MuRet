#include "Scenes/HelperTable.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/PetHelperSteps.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldState.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainCulling.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

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
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Scenes::HelperTable: la tabla de los ayudantes del juego (0x7d335d0) con el caballo oscuro (0xbf) y las
// mascotas: 0x443fa0 → 0x443d70, 0x443d40, el paso de las escenas 4 y 5 (0x445230 → 0x444010) y su dibujo (0x4453b0
// → 0x445260). 06 § El caballo oscuro y § Las mascotas.
namespace
{
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Render::ModelBatch;
using Client::Render::TextureSlot;
using Client::Render::Models::ActionInfo;
using Client::Render::Models::BoneKey;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::Skeleton;
using Client::Render::Models::SkeletonBone;
using Client::Scenes::ActorState;
using Client::Scenes::CreateHelper;
using Client::Scenes::DarkHorseModel;
using Client::Scenes::DinorantModel;
using Client::Scenes::DrawHelpers;
using Client::Scenes::GuardianAngelModel;
using Client::Scenes::HelperCount;
using Client::Scenes::HelperDrawContext;
using Client::Scenes::HelperState;
using Client::Scenes::HelperStepContext;
using Client::Scenes::IModelLibrary;
using Client::Scenes::SceneId;
using Client::Scenes::StepHelpers;
using Client::Scenes::TurnOffHelpersOf;
using Client::Scenes::UniriaModel;
using Client::Scenes::WorldState;
using Client::World::TerrainPolygon;

// Las semillas del rand() de la CRT, por su primer rand(): con QuietSeed, % 3 da 2 y % 2 da 1 (ni aliento
// ni polvo); con BreathSeed, % 3 da 0; con DustSeed, % 2 da 0.
constexpr std::uint32_t QuietSeed = 1;
constexpr std::uint32_t BreathSeed = 2;
constexpr std::uint32_t DustSeed = 3;
constexpr std::size_t HeroSlot = 42;
constexpr std::size_t OtherSlot = 7;
// Las acciones del jinete (+0x111) que mira 0x444010.
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

// Las siete acciones de DarkHorse.bmd (u otras tantas), de 10 keys cada una, en `boneCount` huesos: el 0 en el origen
// y los demás colgados de él, cada uno a su número de unidades en z.
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
                key.rotation = Client::Render::Models::AngleQuaternion({0.0f, 0.0f, 0.0f});
            }
        }
        skeleton.bones.push_back(bone);
    }
    skeleton.actions.assign(actionCount,
                            ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 10, .lockPositions = false});
    return skeleton;
}

// Las mascotas que mueve 0x444010 además del caballo.
constexpr std::array<std::uint16_t, 6> PetModels{UniriaModel, DinorantModel, 0x14a, 0x14b, 0x14c, GuardianAngelModel};
// El Uniria y el Dinorant llegan a la acción 7 (0x44498b).
constexpr std::size_t PetActions = 8;

bool IsPet(std::uint16_t model)
{
    return std::find(PetModels.begin(), PetModels.end(), model) != PetModels.end();
}

// El caballo como un modelo de una malla con un triángulo, colgado del hueso 0.
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

    [[nodiscard]] const Model* Find(std::uint16_t model) const override
    {
        return model == DarkHorseModel || IsPet(model) ? &m_model : nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        if (model == DarkHorseModel)
        {
            return &m_skeleton;
        }
        return IsPet(model) ? &m_petSkeleton : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t model) const override
    {
        return model == DarkHorseModel || IsPet(model) ? std::span<const TextureSlot>(m_slots)
                                                       : std::span<const TextureSlot>{};
    }

private:
    Skeleton m_skeleton;
    Skeleton m_petSkeleton;
    Model m_model;
    std::array<TextureSlot, 1> m_slots{TextureSlot{0x3bc}};
};

// Un mundo con el héroe vivo en HeroSlot, como lo deja 0x511290 (+0x88 en 1), en la celda (100, 100) sin
// atributos, y la tabla de los ayudantes vacía.
class HelperWorld
{
private:
    // Varios escenarios conviven en un test: el pool durable no debe agotar la pila de Windows.
    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();

public:
    explicit HelperWorld(std::int32_t map = 0, std::uint32_t seed = QuietSeed) : random(seed)
    {
        world.Runtime().map = map;
        world.Runtime().hero = HeroSlot;
        world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
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
        return world.Map().terrain.attributes->cells[Client::Data::Terrain::CellIndex(x, y)];
    }

    // 0x443fa0(0xbf, héroe + 0x10, héroe, 0, 0), como lo llama 0x5bc997.
    void Mount()
    {
        const auto created = CreateHelper(world, DarkHorseModel, Rider().position, HeroSlot);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        ASSERT_EQ(created.Value(), std::optional<std::size_t>{0});
    }

    // 0x443fa0(tipo, héroe + 0x10, héroe, 0, 0) con el rand() compartido.
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
                                             .terrain = nullptr});
    }

    // El buffer global de la pose (0x68b181c) como lo dejaría un dibujo de 39 huesos (o `count`): cada uno sin giro,
    // en PosedBone(hueso).
    void PoseTheGlobalBones(std::size_t count = PosedBones)
    {
        auto& bones = world.PoseCarry().bones;
        bones.assign(count, Client::Render::Models::BoneTransform(1.0f));
        for (std::size_t bone = 0; bone < count; ++bone)
        {
            bones[bone][3] = PosedBone(bone);
        }
    }

    // El galope (0x4b) con el caballo ya en su acción 1 y en `frame`.
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

    // Lo que el dibujo de `scene` lee: la luz del terreno de este mundo y la tabla de los sprites.
    HelperDrawContext DrawContext(ModelBatch& batch, bool originalFrame = true, SceneId scene = SceneId::Game)
    {
        return HelperDrawContext{
            .batch = batch,
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
            { return std::span<const Client::Render::Models::BoneTransform>(world.PoseCarry().bones); }};
    }

    // El timeGetTime() que ve el dibujo de los Fenrir.
    static constexpr std::uint32_t FenrirHour = 4321;

    Client::Core::Expected<void, std::string> Draw(ModelBatch& batch, const TerrainPolygon& polygon,
                                                   bool originalFrame = true, SceneId scene = SceneId::Game)
    {
        world.Runtime().terrainPolygon = polygon;
        batch.Begin(Client::Render::Pass3D{});
        return DrawHelpers(DrawContext(batch, originalFrame, scene));
    }

    // Los sprites vivos de la tabla del frame.
    std::vector<Client::World::Sprite> LiveSprites()
    {
        std::vector<Client::World::Sprite> live;
        for (const Client::World::Sprite& sprite : sprites.Sprites())
        {
            if (sprite.alive)
            {
                live.push_back(sprite);
            }
        }
        return live;
    }

    WorldState& world = *m_world;
    CrtRandom random;
    // El reloj del mundo (0x5767b88) con que 0x53fe70 gira las 5f2: 12.345 ms, 105 grados.
    float clock = 12345.0f;
    HorseModels models;
    // La luz del terreno (0x5ffce0), (0,25; 0,5; 0,75) en cada celda, y los sprites del frame (0x5491e0).
    std::unique_ptr<Client::World::TerrainLighting> lighting = UniformLighting();
    Client::World::FrameSprites sprites;

private:
    static std::unique_ptr<Client::World::TerrainLighting> UniformLighting()
    {
        auto uniform = std::make_unique<Client::World::TerrainLighting>();
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

// 0x443d70 en el primer lugar libre: vivo, el 0xbf, el jinete, la posición que recibe, los ángulos del
// jinete, la escala en 1, la velocidad en 0,5, el alpha en 0, sin verse, y el frame y la key anterior en 0.
// Sin rand(). El siguiente va al lugar 1.
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

// Q-C221: 0x443d70 no escribe la acción (+0x111) ni la anterior (+0x112): el lugar que se vuelve a usar se
// queda con las del caballo de antes. En la celda segura del jinete, el primer paso avanza esa acción.
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

// 0x443faa..0x443fc7: en los mapas 18 a 23, o con un jinete que no es un jugador, nada; con los 10 vivos,
// tampoco. En el 17 y el 24, sí.
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

// Un tipo sin constructor portado corta donde el original lo crearía; en Chaos Castle, nada.
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

// 0x443d40: apaga los vivos de ese jinete; los de otro y lo demás del lugar quedan.
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

// Q-C225: en la escena 5, con el jinete muerto o con su +0x88 distinto de 1, 0x444010 apaga el caballo y no
// hace más: ni el alpha, ni rand().
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

// 0x4440fd..0x445039 fuera de la celda segura: copia la posición y los ángulos del jinete, toma la acción y
// la velocidad de la suya y avanza; el aliento saca su rand() % 3 (con QuietSeed, 2: sin aliento). Con +8
// del jinete en otro valor que 1, lo vuelve a 0.
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

// 0x4440b8..0x4440f8: en una celda con el bit 0 del byte bajo, la de la posición del jinete por 0,01, el
// alpha va a 0 después de subir y el caballo avanza con la acción y la velocidad que tenía (0,5 de 0x443d70),
// sin copiar nada del jinete ni sacar rand(). El 0x02 del héroe y los bits altos no cuentan.
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

// 0x4440bb..0x4440d6: la celda es la de la posición truncada, con el byte bajo de cada coordenada (0x5f82a0):
// 17.299,99 por 0,01 cae en la 172, y 25.650 (la 256,5) en la 0.
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

// Q-C222, con los .att de Lorencia (el 0) y Devias (el 2): el caballo mira la celda de la posición del
// jinete, la que se dibuja, y no la lógica de +0x41c (la de +0x3e2, 0x505de0, que va una adelante). En el
// borde del pueblo, con el jinete dibujado adentro, la celda lógica afuera y +0x3e2 en 0, el caballo se
// oculta; dibujado afuera, con la lógica adentro y +0x3e2 en 1, se ve y lo sigue.
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
            auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, border.map);
            ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
            tested.world.Map().terrain = std::move(loaded).Value();
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

// 0x44456b..0x4445d9: con rand() % 3 en 0, el aliento sale de (50; −4; 0) en el hueso 27 del buffer global de la
// pose (0x4ec110 sin el origen) a las partículas del juego: la 5f2/0 con la luz 3 del caballo, sus dos rand() y el
// reloj del mundo, o en Atlans (el 7) la gota 607/0, que nace con la luz en 1. Tres rand() en total.
TEST(HelperTableTest, TheBreathLeavesFromBone27OfTheGlobalPose)
{
    HelperWorld tested(0, BreathSeed);
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.PoseTheGlobalBones();

    const auto stepped = tested.Step();

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const Client::World::Particle& breath = tested.world.Particles().All()[0];
    EXPECT_TRUE(breath.alive);
    EXPECT_EQ(breath.type, Client::World::BreathParticle);
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
    const Client::World::Particle& drop = atlans.world.Particles().All()[0];
    EXPECT_EQ(drop.type, Client::World::DropParticle);
    EXPECT_EQ(drop.position, (glm::vec3{10077.0f, 10123.0f, 197.0f}));
    EXPECT_EQ(drop.light, glm::vec3(1.0f));
    EXPECT_EQ(drop.life, 36);
    EXPECT_EQ(drop.scale, std::bit_cast<float>(0x3df5c28fu));
    EXPECT_EQ(atlans.random.State(), RandomAfter(BreathSeed, 3).State());
}

// Sin el hueso 27 en el buffer global (ningún dibujo lo escribió todavía) el hueso es la matriz en cero de la
// .bss (200 huesos): el aliento sale del origen, con los mismos tres rand(). Con la acción 1 del caballo, o con
// +0x78 & 0x4000 del jinete, no lo saca.
TEST(HelperTableTest, TheBreathWithoutItsBoneLeavesFromTheOrigin)
{
    HelperWorld tested(0, BreathSeed);
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.world.PoseCarry().bones.resize(27);

    const auto stepped = tested.Step();

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const Client::World::Particle& breath = tested.world.Particles().All()[0];
    EXPECT_TRUE(breath.alive);
    EXPECT_EQ(breath.type, Client::World::BreathParticle);
    EXPECT_EQ(breath.position, glm::vec3(0.0f));
    EXPECT_EQ(tested.random.State(), RandomAfter(BreathSeed, 3).State());

    HelperWorld calm(0, BreathSeed);
    ASSERT_NO_FATAL_FAILURE(calm.Mount());
    calm.Rider().flags78 = 0x4000;
    ASSERT_TRUE(calm.Step().HasValue());
    EXPECT_EQ(calm.random.State(), CrtRandom(BreathSeed).State());
}

// 0x445260 → 0x602e90 deja la pose del caballo en el buffer global (WorldState::PoseCarry) y el paso siguiente
// saca el aliento del hueso 27 de esa pose.
TEST(HelperTableTest, TheBreathReadsThePoseTheHorseDrawLeft)
{
    HelperWorld tested(0, BreathSeed);
    tested.models = HorseModels(28);
    const HorseModels& models = tested.models;
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.Horse().horse.alpha = 1.0f;
    tested.world.Runtime().terrainPolygon = Everything;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    HelperDrawContext context{.batch = batch, .models = models, .world = tested.world, .random = tested.random};
    context.onPose = [&](const Client::Scenes::DrawnObject& object)
    { return Client::Scenes::PublishFaunaPose(tested.world.PoseCarry(), models, object, true); };
    ASSERT_TRUE(DrawHelpers(context).HasValue());
    ASSERT_EQ(tested.world.PoseCarry().bones.size(), 28u);
    const glm::vec3 expected = Client::Render::Models::OriginalVectorTransform(
        {50.0f, -4.0f, 0.0f}, Client::Render::Models::OriginalMatrixOf(tested.world.PoseCarry().bones[27]));
    tested.random = CrtRandom(BreathSeed);

    ASSERT_TRUE(tested.Step().HasValue());

    const Client::World::Particle& breath = tested.world.Particles().All()[0];
    EXPECT_TRUE(breath.alive);
    EXPECT_EQ(breath.position, expected);
    EXPECT_NE(breath.position, glm::vec3(0.0f));
}

// 0x44416b..0x4444ef, el galope (0x4b) del jinete fuera del 10: la acción 1, sin aliento. rand() % 2: con 1, nada;
// con 0, en Kalima (24 a 29 y 36) o con +0x78 & 0x4000 del jinete, nada; si no, los tres rand() del lugar
// alrededor del caballo, (rand() & 63) − 32 en x y en y y (rand() & 31) − 16 en z, y en Devias (el 2) la 5f2/0 con
// sus dos rand(), y en los demás la 5f3/0 con sus cuatro, las dos con la luz 1.
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
    const Client::World::Particle& dust = lorencia.world.Particles().All()[0];
    EXPECT_TRUE(dust.alive);
    EXPECT_EQ(dust.type, Client::World::DustParticle);
    EXPECT_EQ(dust.subtype, 0);
    EXPECT_EQ(dust.position, (glm::vec3{10045.0f, 10033.0f, 157.0f}));
    EXPECT_EQ(dust.light, glm::vec3(1.0f));
    EXPECT_EQ(dust.angles, (glm::vec3{103.0f, 0.0f, 45.0f}));
    EXPECT_EQ(dust.scale, std::bit_cast<float>(0x3ec28f5cu));
    EXPECT_EQ(dust.velocity, Client::Render::Models::OriginalVectorRotate(
                                 {0.0f, 3.0f, 0.0f}, Client::Render::Models::OriginalAngleMatrix({0.0f, 0.0f, 45.0f})));
    EXPECT_EQ(dust.life, 32);
    EXPECT_FALSE(lorencia.world.Particles().All()[1].alive);
    EXPECT_EQ(lorencia.random.State(), RandomAfter(DustSeed, 8).State());

    HelperWorld devias(2, DustSeed);
    ASSERT_NO_FATAL_FAILURE(devias.Mount());
    devias.Rider().animation.action = GallopingRider;
    const auto inDevias = devias.Step();
    ASSERT_TRUE(inDevias.HasValue()) << inDevias.Error();
    const Client::World::Particle& smoke = devias.world.Particles().All()[0];
    EXPECT_EQ(smoke.type, Client::World::BreathParticle);
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

// 0x44419f..0x4443d1, el galope en Icarus (el 10), sin mirar Kalima ni +0x78 del jinete: (rand() & 63) − 32 en la z
// de (120; 0; z), el punto del hueso del frame en el buffer global, la onda 0x6eb/1 corrida (rand() & 15) − 8 en x
// y en y con su rand() de la escala, el rand() de la guarda y las 5f2/0 de a una, corridas rand() % 50 − 25 en x y en
// y y (rand() & 15) − 18 en z, mientras su cuenta no llega a un rand() % 5 + 5 nuevo en cada vuelta (Q-C400). Con la
// semilla 1 y el frame 1,25 (el hueso 25 en (10.025; 10.125; 195)): el lugar (10.145; 10.125; 204), siete
// nubes y 47 rand().
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
        const Client::World::ModelEffect& wave = tested.world.Map().effects.Effects()[0];
        EXPECT_TRUE(wave.alive);
        EXPECT_EQ(wave.type, Client::World::ShockWaveEffect);
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
            const Client::World::Particle& puff = tested.world.Particles().All()[index];
            EXPECT_TRUE(puff.alive);
            EXPECT_EQ(puff.type, Client::World::BreathParticle);
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

// 0x4441cd..0x4442ad: el hueso por el frame del caballo, en ventanas que se pisan y donde gana la primera: (1; 1,2)
// el 19, (1,1; 1,4) el 25, (1,3; 1,6) el 32 y (1,5; 1,8] el 38, cerrada arriba. Fuera de todas, solo el rand() de la
// z (Q-C404), sin onda ni nubes. La onda queda corrida −5 en x del hueso más 120.
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

        const Client::World::ModelEffect& wave = tested.world.Map().effects.Effects()[0];
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

// Sin el hueso del frame en el buffer global vale la matriz en cero de la .bss: la onda nace en el corrimiento
// de rand() % 16 − 8 alrededor del origen (el −5 de la semilla, el 115 − 120 de los huesos posados), igual que
// con los huesos escritos en cero.
TEST(HelperTableTest, TheIcarusDustWithoutItsBoneReadsAZeroBone)
{
    HelperWorld tested(10, QuietSeed);
    ASSERT_NO_FATAL_FAILURE(tested.Mount());
    tested.Gallop(1.25f);
    HelperWorld zeroed(10, QuietSeed);
    ASSERT_NO_FATAL_FAILURE(zeroed.Mount());
    zeroed.Gallop(1.25f);
    zeroed.world.PoseCarry().bones.assign(HelperWorld::PosedBones, Client::Render::Models::BoneTransform(0.0f));

    const auto stepped = tested.Step();
    const auto zeroedStep = zeroed.Step();

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_TRUE(zeroedStep.HasValue()) << zeroedStep.Error();
    const Client::World::ModelEffect& wave = tested.world.Map().effects.Effects()[0];
    ASSERT_TRUE(wave.alive);
    EXPECT_EQ(wave.transform.position.x, -5.0f);
    EXPECT_EQ(wave.transform.position, zeroed.world.Map().effects.Effects()[0].transform.position);
    EXPECT_TRUE(tested.world.Particles().All()[0].alive);
    EXPECT_EQ(tested.random.State(), zeroed.random.State());
}

// 0x4445e1: con la acción 3 del caballo (la del jinete 0x53), después del aliento, 0x52e470 y 0x4453e0;
// 0x444693: con +8 del jinete en 1, los doce 0x530860. Los dos cortan, y +8 queda en 1.
TEST(HelperTableTest, TheRearingAndTheOrbitsStopAfterTheBreath)
{
    HelperWorld rearing;
    ASSERT_NO_FATAL_FAILURE(rearing.Mount());
    rearing.Rider().animation.action = RearingRider;
    const auto reared = rearing.Step();
    EXPECT_TRUE(HasError(reared, "0x4445e1")) << (reared.HasValue() ? "" : reared.Error());
    EXPECT_EQ(rearing.Horse().horse.animation.action, 3u);
    EXPECT_EQ(rearing.random.State(), RandomAfter(QuietSeed, 1).State());

    HelperWorld orbits;
    ASSERT_NO_FATAL_FAILURE(orbits.Mount());
    orbits.Rider().byte8 = 1;
    const auto orbited = orbits.Step();
    EXPECT_TRUE(HasError(orbited, "0x444693")) << (orbited.HasValue() ? "" : orbited.Error());
    EXPECT_EQ(orbits.Rider().byte8, 1);
}

// Sin el esqueleto del 0xbf o sin el mapa cargado, el paso de un caballo vivo corta; con la tabla vacía
// no los necesita.
TEST(HelperTableTest, ALiveHorseNeedsItsSkeletonAndTheMap)
{
    HelperWorld empty;
    EXPECT_TRUE(
        StepHelpers(HelperStepContext{.world = empty.world, .models = nullptr, .random = empty.random}).HasValue());
    empty.world.Map().terrain.attributes.reset();
    EXPECT_TRUE(empty.Step().HasValue());

    HelperWorld noSkeleton;
    ASSERT_NO_FATAL_FAILURE(noSkeleton.Mount());
    const auto withoutSkeleton =
        StepHelpers(HelperStepContext{.world = noSkeleton.world, .models = nullptr, .random = noSkeleton.random});
    EXPECT_TRUE(HasError(withoutSkeleton, "0x615700"));

    HelperWorld noMap;
    ASSERT_NO_FATAL_FAILURE(noMap.Mount());
    noMap.world.Map().terrain.attributes.reset();
    EXPECT_TRUE(HasError(noMap.Step(), "0x4440db"));
}

// 0x445260 en la escena 5: +0x16c con el trapecio global; visto, la escala en 1, el dibujo de 0x602e90 y
// el rand() % 30 de 0x445336.
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
    tested.world.Runtime().terrainPolygon = Everything;
    ModelBatch batch;
    int publications = 0;
    HelperDrawContext context{.batch = batch, .models = tested.models, .world = tested.world, .random = tested.random};
    context.onPose = [&](const Client::Scenes::DrawnObject& object) -> Client::Core::Expected<void, std::string>
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

// Sin verse no se dibuja ni saca rand(); con un jinete que no es un jugador, tampoco, aunque se vea.
TEST(HelperTableTest, AnUnseenHorseOrOneWithoutAPlayerSpendsNothing)
{
    HelperWorld unseen;
    ASSERT_NO_FATAL_FAILURE(unseen.Mount());
    unseen.Horse().horse.alpha = 1.0f;
    unseen.Horse().seen = true;
    ModelBatch far;
    const TerrainPolygon elsewhere =
        Client::World::TerrainPolygonOf(Client::World::CullingScene::Game, 0, CellCentre(200, 200), -45.0f);
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

// Q-C223: con el alpha por debajo de 0,01, 0x5ffdf0 no lo dibuja, pero 0x445336 saca su rand() igual.
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

// Q-C224: con +0x78 & 0x1000 del jinete, 0x5ffdf0 pone el frame del caballo en 0 si lo dibuja.
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

// 0x5ffdf0 en los mapas 18 a 23: sin +0x1a1 del héroe, el caballo se dibuja como en otro mapa; con él, el alpha
// guardado baja 0,15f en cada dibujo del original.
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

    chaos.world.Actors()[chaos.world.Runtime().hero].byte1a1 = 1;
    ModelBatch second;
    ASSERT_TRUE(chaos.Draw(second, Everything).HasValue());
    EXPECT_EQ(chaos.Horse().horse.alpha, static_cast<float>(1.0 - static_cast<double>(0.15f)));
}

// Lo que el dibujo no tiene: el dibujo con 10 de un jinete con +0x78 & 0x4000 (0x60033c), que corta sin dibujar
// ni sacar el rand().
TEST(HelperTableTest, TheDrawStopsWithTheRidersFlag4000)
{
    HelperWorld flagged;
    ASSERT_NO_FATAL_FAILURE(flagged.Mount());
    flagged.Horse().horse.alpha = 1.0f;
    flagged.Rider().flags78 = 0x4000;
    ModelBatch second;
    EXPECT_TRUE(HasError(flagged.Draw(second, Everything), "0x60033c"));
    EXPECT_TRUE(second.Commands().empty());
    EXPECT_EQ(flagged.random.State(), CrtRandom(QuietSeed).State());
}

// Q-C293: en la escena 4 (0x6342f4) 0x444010 no apaga el ayudante de un jinete muerto: el alpha sube, el caballo lo
// sigue, saca el rand() % 3 del aliento y apaga su byte vivo con el del jinete (0x44475c). En la 5 lo apaga antes de
// todo, sin rand().
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

// 0x444010 corta en 0x444083 con un tipo sin cuerpo (0x443d70 no lo construye) y en 0x444010 sin el modelo del
// tipo, el de Skill\ (0x615700) o Player\Helper01.bmd (0x60ff50).
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

// 0x4447c4..0x444ac4 con el galope del jinete (0x0d): el Uniria toma su lugar y sus ángulos, la acción 2 y la
// velocidad 0,34 (0x444ab6), y con rand() % 2 en 0 el polvo 5f3 de Lorencia alrededor, con los mismos rand() que el
// caballo (TheGallopRaisesTheDustOfItsMap). El alpha va a 1 con +0x80 del jinete en 0.
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
    const Client::World::Particle& dust = tested.world.Particles().All()[0];
    EXPECT_TRUE(dust.alive);
    EXPECT_EQ(dust.type, Client::World::DustParticle);
    EXPECT_EQ(dust.subtype, 0);
    EXPECT_EQ(dust.position, (glm::vec3{10045.0f, 10033.0f, 157.0f}));
    EXPECT_EQ(dust.light, glm::vec3(1.0f));
    EXPECT_EQ(tested.random.State(), RandomAfter(DustSeed, 8).State());

    // 0x4449d6 y 0x4449f1: en Icarus, o con 0x4000 en +0x78 del jinete, solo el rand() % 2.
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

// 0x444867..0x4449b7: la acción del Uniria y del Dinorant por la del jinete y el mapa, y la z del Dinorant: −10 en
// Tarkan (8) e Icarus (10), −30 en los demás y la del jinete con el mundo en −1. Solo el galope saca su rand() % 2
// (con la semilla 1, sin polvo).
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

// 0x44480b..0x44484a: con +0x80 del jinete en 1 o en 2, el alpha que dejó 0x4e9980 baja 0,1f; con otro, 1.
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

// 0x4447e7..0x444806 y 0x444b13..0x444b36: en la celda segura del jinete el alpha va a 0 y el ayudante avanza con la
// acción y la velocidad que tenía, sin moverse ni sacar rand(). El Uniria no copia el byte vivo del jinete ahí.
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
    noMap.world.Map().terrain.attributes.reset();
    EXPECT_TRUE(HasError(noMap.Step(), "0x4447e7"));
}

// 0x444ac0: el Uniria y el Dinorant se quedan con el byte vivo del jinete, que en la escena 4 puede estar muerto.
TEST(HelperTableTest, TheRidingHelpersCopyTheRidersAliveByte)
{
    HelperWorld tested(54, QuietSeed);
    ASSERT_NO_FATAL_FAILURE(tested.Ride(DinorantModel));
    tested.Rider().alive = false;

    ASSERT_TRUE(tested.Step(SceneId::CharacterSelect).HasValue());

    EXPECT_FALSE(tested.Horse().alive);
    EXPECT_EQ(tested.Horse().horse.alpha, 1.0f);
}

// 0x444bbc..0x444c4b: la acción y la velocidad del Fenrir por la del jinete. Las quietas (3, 4, 5 y 0, a 0,4) van
// directo al avance; la carrera (1, a 1) y el trote (2, a 0,6) y las demás (sin tocar acción ni velocidad) siguen al
// polvo, que fuera del 10 saca su rand() % 3 (con la semilla 1, sin polvo).
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

// 0x444e1b..0x444f25: fuera del 10, con rand() % 3 en 0, fuera de Kalima y sin el 0x4000 del jinete, el polvo
// alrededor del Fenrir: la 5f2 en Devias, a (0; −26; −13) con la semilla 2, y la 5f3 en los demás. El Fenrir no
// copia el byte vivo del jinete.
TEST(HelperTableTest, TheRunningFenrirRaisesTheDustOfItsMap)
{
    HelperWorld devias(2, BreathSeed);
    ASSERT_NO_FATAL_FAILURE(devias.Ride(0x14a));
    devias.Rider().animation.action = 0x7a;
    const auto inDevias = devias.Step(SceneId::CharacterSelect);
    ASSERT_TRUE(inDevias.HasValue()) << inDevias.Error();
    const Client::World::Particle& smoke = devias.world.Particles().All()[0];
    EXPECT_TRUE(smoke.alive);
    EXPECT_EQ(smoke.type, Client::World::BreathParticle);
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
    EXPECT_EQ(lorencia.world.Particles().All()[0].type, Client::World::DustParticle);
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

// 0x444c78..0x444e16, el Fenrir en Icarus: sin rand() antes del frame. Entre 1 y 1,2, el hueso 22 del buffer global
// con (120; 0; 32) y 40 más en x; entre 4,8 y 5 (cerrada arriba), el 44 con 40 más en x y 700 más en z (Q-C421). La
// onda 0x6eb/1 sin corrimiento, con su rand() de la escala, y las 5f2 (PuffTheIcarusDust): con la semilla 1, ocho y
// 50 rand(). Fuera de las ventanas, nada.
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
        const Client::World::ModelEffect& wave = tested.world.Map().effects.Effects()[0];
        if (!probe.base)
        {
            EXPECT_FALSE(wave.alive);
            EXPECT_FALSE(tested.world.Particles().All()[0].alive);
            EXPECT_EQ(tested.random.State(), CrtRandom(QuietSeed).State());
            continue;
        }
        EXPECT_TRUE(wave.alive);
        EXPECT_EQ(wave.type, Client::World::ShockWaveEffect);
        EXPECT_EQ(wave.subtype, 1);
        EXPECT_EQ(wave.transform.position, *probe.base);
        EXPECT_EQ(wave.transform.angles, (glm::vec3{0.0f, 0.0f, 45.0f}));
        EXPECT_EQ(wave.transform.scale, std::bit_cast<float>(0x3f8ccccdu));
        const Client::World::Particle& first = tested.world.Particles().All()[0];
        EXPECT_EQ(first.type, Client::World::BreathParticle);
        EXPECT_EQ(first.position, *probe.base + glm::vec3(9.0f, -25.0f, -17.0f));
        const Client::World::Particle& last = tested.world.Particles().All()[7];
        EXPECT_TRUE(last.alive);
        EXPECT_EQ(last.position, *probe.base + glm::vec3(-2.0f, -11.0f, -13.0f));
        EXPECT_FALSE(tested.world.Particles().All()[8].alive);
        EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 50).State());
    }
}

// 0x444f2a..0x444fff: sin el 0x4000 del jinete, cuatro 5c5/1 con la luz 0,4 a (rand() & 15) − 8 del Guardian Angel
// en cada eje, con los dos rand() de cada alta. Después del avance, el vuelo (0x445052..0x445221): cerca del jinete no
// gira, se mueve con su velocidad girada por sus ángulos y (rand() & 15) − 8 en z, y saca el rand() % 32 (con la
// semilla 1, 12: sin otro rumbo). A 150 sobre el jinete, sin subir ni bajar la velocidad.
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
        const Client::World::Particle& spark = tested.world.Particles().All()[index];
        EXPECT_TRUE(spark.alive);
        EXPECT_EQ(spark.type, Client::World::FairySparkParticle);
        EXPECT_EQ(spark.subtype, 1);
        EXPECT_EQ(spark.position, Sparks[index]);
        EXPECT_EQ(spark.light, glm::vec3(std::bit_cast<float>(0x3ecccccdu)));
    }
    EXPECT_FALSE(tested.world.Particles().All()[4].alive);
    const glm::vec3 move = Client::Render::Models::OriginalVectorRotate(
        {0.0f, -5.0f, 0.0f}, Client::Render::Models::OriginalAngleMatrix({0.0f, 0.0f, 45.0f}));
    EXPECT_EQ(angel.angles, (glm::vec3{0.0f, 0.0f, 45.0f}));
    EXPECT_EQ(angel.position.x, static_cast<float>(static_cast<double>(move.x) + 10050.0));
    EXPECT_EQ(angel.position.y, static_cast<float>(static_cast<double>(move.y) + 10050.0));
    EXPECT_EQ(angel.position.z, 319.0f);
    EXPECT_EQ(angel.velocity, (glm::vec3{0.0f, -5.0f, 0.0f}));
    EXPECT_EQ(angel.horse.alpha, std::bit_cast<float>(0x3dcccccdu));
    EXPECT_FLOAT_EQ(angel.horse.animation.frame, 0.5f);
    EXPECT_EQ(tested.random.State(), RandomAfter(QuietSeed, 22).State());
}

// 0x444f32..0x444f41: con 0x4000 en +0x78 del jinete, sin chispas. 0x445117..0x4451cb con rand() % 32 en 0 (la
// semilla 2): de cerca, la velocidad en y ((rand() & 63) + 16) · −0,1f, el rumbo rand() % 360, 0 en x y
// ((rand() & 63) − 32) · 0,1f en z, que sigue igual entre 100 y 200 por encima del jinete.
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

// 0x445052..0x4450b4: a 150 o más del jinete gira 20 grados hacia donde estaba al empezar el paso (0x4e93f0 y
// 0x4e9560), y con rand() % 32 en 0 (la semilla 48, después de las cuatro chispas) toma la velocidad de lejos,
// ((rand() & 63) + 128) · −0,1f, sin otro rumbo. Por debajo de 100 sobre el jinete, la de z sube 1,5; por encima de
// 200, baja 1,5.
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

    const float course =
        static_cast<float>(Client::Render::Models::CreateAngle(11050.0f, 10050.0f, 10050.0f, 10050.0f));
    const float turned = static_cast<float>(Client::Render::Models::TurnAngle(45.0f, course, 20.0f));
    EXPECT_NE(turned, 45.0f);
    EXPECT_EQ(angel.angles, (glm::vec3{0.0f, 0.0f, turned}));
    const glm::vec3 move = Client::Render::Models::OriginalVectorRotate(
        {0.0f, -5.0f, 0.0f}, Client::Render::Models::OriginalAngleMatrix({0.0f, 0.0f, turned}));
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

// Q-C420: 0x443d70 no escribe la velocidad (+0xcc): el Guardian Angel que se crea en un lugar usado se queda con la
// del ayudante de antes. Sus tres rand() de lugar van con el rand() compartido.
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

// Q-C225 también para las mascotas: en la escena 5, con el jinete sin el 1 en +0x88, 0x444010 las apaga sin rand().
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

// 0x445260 → 0x602e90 con el Uniria y el Dinorant en el juego: la escala en 1 (0x445309), el color de 0x5ffce0 con la
// luz por vértice (la del terreno más el 3 de +0xf4) y RenderBody(2) sin blendMesh, con su luz en 1, la U y la V en
// 0 y sin malla oculta (0x600b35 y 0x602360). Después, solo el rand() % 30 de 0x445336.
TEST(HelperTableTest, TheUniriaAndTheDinorantDrawWithTheLightOfTheTerrain)
{
    for (const std::uint16_t type : {UniriaModel, DinorantModel})
    {
        SCOPED_TRACE(type);
        HelperWorld tested;
        ASSERT_NO_FATAL_FAILURE(tested.Ride(type));
        EXPECT_FLOAT_EQ(tested.Horse().scale, 0.9f);
        tested.Horse().horse.alpha = 1.0f;
        tested.world.Runtime().terrainPolygon = Everything;
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        std::vector<Client::Scenes::DrawnObject> posed;
        HelperDrawContext context = tested.DrawContext(batch);
        context.onPose = [&](const Client::Scenes::DrawnObject& object) -> Client::Core::Expected<void, std::string>
        {
            posed.push_back(object);
            return {};
        };

        ASSERT_TRUE(DrawHelpers(context).HasValue());

        EXPECT_FLOAT_EQ(tested.Horse().scale, 1.0f);
        ASSERT_EQ(posed.size(), 1u);
        const Client::Scenes::DrawnObject& object = posed.front();
        EXPECT_EQ(object.model, type);
        EXPECT_EQ(object.transform.position, tested.Horse().position);
        EXPECT_EQ(object.transform.angles, tested.Rider().angles);
        EXPECT_FLOAT_EQ(object.transform.scale, 1.0f);
        const glm::vec3 terrain =
            Client::World::LightAt(*tested.lighting, tested.Horse().position.x, tested.Horse().position.y);
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

// 0x4452db..0x445309: 1,6 (0x3fcccccd) en la escena 2 y 1 en la 4; en la 5, los Fenrir se quedan con el 0,9 de
// 0x443d70 y los demás van a 1. Con el alpha en 0 no se dibuja nada, pero la escala queda.
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

// El Guardian Angel (0x1c12) se dibuja sin un jugador de jinete (0x4452c2), con la escala en 1 y su malla 1 de
// blendMesh (+0x64 en 1, 0x443f1a). Después, 0x445336 saca rand() % 30 + 70 (26.500 % 30 + 70 = 80 con la semilla 1,
// después de los tres de 0x443d70) y 0x445390 crea el sprite 0x5ac en su lugar, de escala 1, con 80 · 0,01f por
// (0,5; 0,8; 0,6) en la x87 y guardado en float: (0x3ecccccc; 0x3f23d70a; 0x3ef5c290). Con el alpha por debajo de
// 0,01 no hay cuerpo, pero sí el rand() y el sprite.
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
        tested.world.Runtime().terrainPolygon = Everything;
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        std::vector<Client::Scenes::DrawnObject> posed;
        HelperDrawContext context = tested.DrawContext(batch);
        context.onPose = [&](const Client::Scenes::DrawnObject& object) -> Client::Core::Expected<void, std::string>
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
        const std::vector<Client::World::Sprite> glow = tested.LiveSprites();
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

// Lo que el dibujo no tiene: los Fenrir (0x60277b: sus pases, la partícula 0x5c6, la sombra, los sprites de sus
// huesos y los dos efectos 0x14d de 0x602b30), que cortan sin el rand() de 0x445336; la luz del terreno de 0x5ffce0
// para los demás; y la tabla de los sprites para el brillo del 0x1c12, que corta después de su rand().
TEST(HelperTableTest, ThePetDrawStopsWhereMuRetHasNotArrived)
{
    // El Fenrir pide sus tablas antes de dibujar y de sacar el primer rand().
    for (int missing = 0; missing < 5; ++missing)
    {
        SCOPED_TRACE(missing);
        HelperWorld tested;
        ASSERT_NO_FATAL_FAILURE(tested.Ride(0x14a));
        tested.Horse().horse.alpha = 1.0f;
        tested.Horse().horse.animation.action = 4;
        tested.PoseTheGlobalBones(60);
        tested.world.Runtime().terrainPolygon = Everything;
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
    angel.world.Runtime().terrainPolygon = Everything;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    HelperDrawContext context = angel.DrawContext(batch);
    context.sprites = nullptr;
    EXPECT_TRUE(HasError(DrawHelpers(context), "0x445390"));
    EXPECT_EQ(angel.random.State(), RandomAfter(QuietSeed, 4).State());
}
// 0x60277b..0x602b50 con el Fenrir rojo (0x14b) en la acción 4, el seno del reloj p = sin(T · 0,002f) · 0,2f: el cuerpo
// en blanco y su sombra; la chispa 0x5c6/15 del hueso 14 con dos rand() y su escala p · 0,05f + 0,7f; dos sprites
// 0x5ac en cada ojo (el hueso 11) y dos en el pecho (el 13); y dos rayos 0x14d con (0,8f; 0; 0) y sus rand(). Después,
// el rand() de 0x445336. Los esperados repiten las altas con otra CRT y el buffer global posado del test.
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
        const auto sine = Client::Render::Models::Detail::SineProductPC53(
            static_cast<double>(tested.clock) * static_cast<double>(0.002f), 0.2f);
        ASSERT_TRUE(sine.has_value());
        const double pulse = static_cast<double>(static_cast<float>(*sine));
        CrtRandom draws = before;
        const int x = (draws.Next() % 10) - 10;
        const int z = (draws.Next() % 40) - 20;
        const glm::vec3 sparkAt =
            HelperWorld::PosedBone(14) + glm::vec3(static_cast<float>(x * 0.5), 0.0f, static_cast<float>(z * 0.5));
        Client::World::Particles sparks;
        const float sparkScale = static_cast<float>(pulse * static_cast<double>(0.05f) + static_cast<double>(0.7f));
        ASSERT_TRUE(sparks
                        .CreateChecked(0x5c6, sparkAt, fenrir.angles, glm::vec3(1.0f, 0.0f, 0.0f), 15, sparkScale,
                                       tested.clock, draws)
                        .HasValue());
        const Client::World::Particle& spark = tested.world.Particles().All()[0];
        ASSERT_TRUE(spark.alive);
        EXPECT_EQ(spark.subtype, 15);
        EXPECT_EQ(spark.position, sparkAt);
        EXPECT_EQ(spark.scale, sparkScale);
        EXPECT_EQ(spark.life, sparks.All()[0].life);
        EXPECT_EQ(spark.velocity, sparks.All()[0].velocity);

        Client::World::ModelEffects bolts;
        Client::World::FrameSprites boltFlares;
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
            const Client::World::ModelEffect& made = tested.world.Map().effects.Effects()[slot];
            const Client::World::ModelEffect& expected = bolts.Effects()[slot];
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

// Sin la acción 4 no hay chispa ni pase del cromo, y los rayos del Fenrir negro (0x14a) y del azul (0x14c) van con
// (1; 1; 0,2f) y (0,1f; 0,1f; 0,8f). Los calcos 0x14e, en blanco, con el subtipo del color (3 y 2) y la hora de
// timeGetTime(): con la acción 1 y el frame en [0; 1,5], en los huesos 22, 28, 36 y 44; con la 2, en el 22 y el 28
// con el frame en (1; 1,4f] y en el 36 y el 44 en (4,8f; 5,2f]; si no, ninguno.
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
            for (const Client::World::ModelEffect& effect : world.world.Map().effects.Effects())
            {
                if (!effect.alive)
                    continue;
                if (effect.type == Client::World::LightningEffect)
                {
                    EXPECT_EQ(effect.light, boltLight);
                    continue;
                }
                ASSERT_EQ(effect.type, Client::World::LightningDecalEffect);
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
} // namespace
