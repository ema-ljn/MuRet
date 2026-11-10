#include "Scenes/HelperTable.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/WorldState.h"
#include "World/TerrainCulling.h"
#include "World/WorldTerrain.h"

#include "support/GoldenDir.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Scenes::HelperTable: la tabla de los ayudantes del juego (0x7d335d0) con el caballo oscuro (0xbf):
// 0x443fa0 → 0x443d70, 0x443d40, el paso de la escena 5 (0x445230 → 0x444010) y su dibujo (0x4453b0 →
// 0x445260). 06 § El caballo oscuro.
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
using Client::Scenes::DrawHelpers;
using Client::Scenes::HelperCount;
using Client::Scenes::HelperDrawContext;
using Client::Scenes::HelperState;
using Client::Scenes::HelperStepContext;
using Client::Scenes::IModelLibrary;
using Client::Scenes::StepHelpers;
using Client::Scenes::TurnOffHelpersOf;
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

// Las siete acciones de DarkHorse.bmd, de 10 keys cada una, en un hueso.
Skeleton HorseSkeleton()
{
    Skeleton skeleton;
    SkeletonBone bone;
    bone.parent = -1;
    bone.actions.assign(7, std::vector<BoneKey>(10, BoneKey{}));
    for (std::vector<BoneKey>& keys : bone.actions)
    {
        for (BoneKey& key : keys)
        {
            key.rotation = Client::Render::Models::AngleQuaternion({0.0f, 0.0f, 0.0f});
        }
    }
    skeleton.bones = {bone};
    skeleton.actions.assign(7, ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 10, .lockPositions = false});
    return skeleton;
}

// El caballo como un modelo de una malla con un triángulo, colgado del hueso 0.
class HorseModels final : public IModelLibrary
{
public:
    HorseModels() : m_skeleton(HorseSkeleton())
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
        return model == DarkHorseModel ? &m_model : nullptr;
    }
    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == DarkHorseModel ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t model) const override
    {
        return model == DarkHorseModel ? std::span<const TextureSlot>(m_slots) : std::span<const TextureSlot>{};
    }

private:
    Skeleton m_skeleton;
    Model m_model;
    std::array<TextureSlot, 1> m_slots{TextureSlot{0x3bc}};
};

// Un mundo con el héroe vivo en HeroSlot, como lo deja 0x511290 (+0x88 en 1), en la celda (100, 100) sin
// atributos, y la tabla de los ayudantes vacía.
struct HelperWorld
{
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

    Client::Core::Expected<void, std::string> Step()
    {
        return StepHelpers(HelperStepContext{.world = world, .horseSkeleton = &skeleton, .random = random});
    }

    Client::Core::Expected<void, std::string> Draw(ModelBatch& batch, const TerrainPolygon& polygon)
    {
        world.Runtime().terrainPolygon = polygon;
        batch.Begin(Client::Render::Pass3D{});
        return DrawHelpers(HelperDrawContext{.batch = batch,
                                             .models = models,
                                             .world = world,
                                             .random = random,
                                             .frameClock = std::chrono::milliseconds{0},
                                             .stepAlpha = 0.0f});
    }

    WorldState world;
    CrtRandom random;
    Skeleton skeleton = HorseSkeleton();
    HorseModels models;
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

// 0x44456b..0x4445d9: con rand() % 3 en 0, el aliento sale del hueso 27 de la pose del dibujo anterior a las
// partículas del juego, que MuRet no tiene: corta después de ese rand(). Con la acción 1 del caballo, o con
// +0x78 & 0x4000 del jinete, no lo saca.
TEST(HelperTableTest, TheBreathStopsAfterItsRandom)
{
    HelperWorld tested(0, BreathSeed);
    ASSERT_NO_FATAL_FAILURE(tested.Mount());

    const auto stepped = tested.Step();

    EXPECT_TRUE(HasError(stepped, "0x44459d")) << (stepped.HasValue() ? "" : stepped.Error());
    EXPECT_EQ(tested.random.State(), RandomAfter(BreathSeed, 1).State());

    HelperWorld calm(0, BreathSeed);
    ASSERT_NO_FATAL_FAILURE(calm.Mount());
    calm.Rider().flags78 = 0x4000;
    ASSERT_TRUE(calm.Step().HasValue());
    EXPECT_EQ(calm.random.State(), CrtRandom(BreathSeed).State());
}

// 0x44416b..0x4444ef, el galope (0x4b) del jinete: la acción 1, sin aliento. En el mapa 10 corta antes de
// sacar rand(). Si no, rand() % 2: con 1, nada; con 0, en Kalima (24 a 29 y 36) o con +0x78 & 0x4000 del
// jinete, nada; si no, los tres rand() del lugar y el polvo a las partículas del juego, que corta.
TEST(HelperTableTest, TheGallopRaisesDustWithItsRandoms)
{
    HelperWorld icarus(10, DustSeed);
    ASSERT_NO_FATAL_FAILURE(icarus.Mount());
    icarus.Rider().animation.action = GallopingRider;
    const auto inIcarus = icarus.Step();
    EXPECT_TRUE(HasError(inIcarus, "0x444196")) << (inIcarus.HasValue() ? "" : inIcarus.Error());
    EXPECT_EQ(icarus.Horse().horse.animation.action, 1u);
    EXPECT_EQ(icarus.random.State(), CrtRandom(DustSeed).State());

    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        HelperWorld quiet(map, QuietSeed);
        ASSERT_NO_FATAL_FAILURE(quiet.Mount());
        quiet.Rider().animation.action = GallopingRider;
        ASSERT_TRUE(quiet.Step().HasValue());
        EXPECT_EQ(quiet.Horse().horse.animation.action, 1u);
        EXPECT_FLOAT_EQ(quiet.Horse().horse.speed, 0.34f);
        EXPECT_EQ(quiet.random.State(), RandomAfter(QuietSeed, 1).State());

        HelperWorld dusty(map, DustSeed);
        ASSERT_NO_FATAL_FAILURE(dusty.Mount());
        dusty.Rider().animation.action = GallopingRider;
        const auto stepped = dusty.Step();
        EXPECT_TRUE(HasError(stepped, "0x4444a6")) << (stepped.HasValue() ? "" : stepped.Error());
        EXPECT_EQ(dusty.random.State(), RandomAfter(DustSeed, 4).State());
    }

    for (const std::int32_t map : {24, 29, 36})
    {
        SCOPED_TRACE(map);
        HelperWorld kalima(map, DustSeed);
        ASSERT_NO_FATAL_FAILURE(kalima.Mount());
        kalima.Rider().animation.action = GallopingRider;
        ASSERT_TRUE(kalima.Step().HasValue());
        EXPECT_EQ(kalima.random.State(), RandomAfter(DustSeed, 1).State());
    }

    HelperWorld flagged(0, DustSeed);
    ASSERT_NO_FATAL_FAILURE(flagged.Mount());
    flagged.Rider().animation.action = GallopingRider;
    flagged.Rider().flags78 = 0x4000;
    ASSERT_TRUE(flagged.Step().HasValue());
    EXPECT_EQ(flagged.random.State(), RandomAfter(DustSeed, 1).State());
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
    EXPECT_TRUE(StepHelpers(HelperStepContext{.world = empty.world, .horseSkeleton = nullptr, .random = empty.random})
                    .HasValue());
    empty.world.Map().terrain.attributes.reset();
    EXPECT_TRUE(empty.Step().HasValue());

    HelperWorld noSkeleton;
    ASSERT_NO_FATAL_FAILURE(noSkeleton.Mount());
    const auto withoutSkeleton = StepHelpers(
        HelperStepContext{.world = noSkeleton.world, .horseSkeleton = nullptr, .random = noSkeleton.random});
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

// Lo que el dibujo no tiene: +0x1a1 del héroe en los mapas 18 a 23 (0x5ffe09) y el dibujo con 10 de un
// jinete con +0x78 & 0x4000 (0x60033c), que corta sin dibujar ni sacar el rand().
TEST(HelperTableTest, TheDrawStopsInChaosCastleAndWithTheRidersFlag4000)
{
    HelperWorld chaos;
    ASSERT_NO_FATAL_FAILURE(chaos.Mount());
    chaos.world.Runtime().map = 18;
    ModelBatch first;
    EXPECT_TRUE(HasError(chaos.Draw(first, Everything), "0x5ffe09"));

    HelperWorld flagged;
    ASSERT_NO_FATAL_FAILURE(flagged.Mount());
    flagged.Horse().horse.alpha = 1.0f;
    flagged.Rider().flags78 = 0x4000;
    ModelBatch second;
    EXPECT_TRUE(HasError(flagged.Draw(second, Everything), "0x60033c"));
    EXPECT_TRUE(second.Commands().empty());
    EXPECT_EQ(flagged.random.State(), CrtRandom(QuietSeed).State());
}
} // namespace
