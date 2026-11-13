#include "Scenes/CameraTour.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorMotion.h"
#include "Scenes/ActorState.h"
#include "Scenes/EventGlobals.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/LoginModels.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/WorldTerrain.h"

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

// CL9 hito 11, fases 3 y 5 (06 § El recorrido de la cámara): CCameraMove contra el ASM de c32bbd5f. El arranque
// (0x403590, 0x4033f0, 0x4033d0, 0x404c90, 0x403300 y 0x403d60) lo pone solo el BD 00 con la etapa 2; el guion
// (0x4035f0) mueve la cámara (0x403d90, 0x4034e0, 0x403430, 0x5fc800) y los personajes 10000 en adelante (0x4039f0,
// 0x403760, 0x4036d0, 0x403900). El mapa no cuenta: Lorencia, Devias y Crywolf dan lo mismo. Las altas usan el Data del
// repo.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::CameraTour;
using Client::Scenes::CameraTourContext;

constexpr std::uint32_t Seed = 0x7015;
constexpr std::array<std::int32_t, 3> Maps{0, 2, 34};

float CellCenter(std::int32_t cell)
{
    return static_cast<float>(cell * 100 + 50);
}

// El mundo, la UI que suelta 0x403390 y los recursos de la entrada, en el heap: WorldState pesa unos 350 KB.
struct Tour
{
    Tour()
    {
        world.FaunaResources().TextureTable().Bind(store);
        models.Set(Client::Data::Models::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
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

    // El recorrido andando desde la etapa del guion `scriptStage`, con la cámara quieta.
    void Running(std::int32_t scriptStage, bool arrived = true, std::int32_t step = 0)
    {
        CameraTour& tour = State();
        tour.stage = 2;
        tour.waiting = false;
        tour.scriptStage = scriptStage;
        tour.arrived = arrived;
        tour.step = step;
    }

    // Un personaje del guion armado a mano en el registro `slot`, con el esqueleto de Player.bmd.
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
                .log = log};
    }

    [[nodiscard]] Client::Core::Expected<void, std::string> Step(std::uint32_t now = 0)
    {
        resources.emplace(Resources());
        return Client::Scenes::StepTheCameraTour(
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

    // Antes que el mundo: FaunaModels suelta sus texturas al destruirse.
    Client::Tests::FakeTextureStore store;
    Client::Scenes::WorldState world;
    std::filesystem::path dataRoot;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::Scenes::SingleModelLibrary models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    std::optional<Client::Scenes::WorldEntryResources> resources;
    bool withResources = true;
    int released = 0;
};

// Con la etapa 2, 0x404c90(5000) arranca su reloj en el primer paso y vence a los 5.000 ms (con signo, <=): la espera
// se apaga y 0x4033d0 da verdadero. En el mismo paso, 0x403390 y la etapa 0 del guion (0x403e00): el tramo al
// (0x71, 0xe8) a 300 por paso, con +0x144 de vuelta en 0 y +0x140 en 0 aunque el reloj lo subió.
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
        EXPECT_FALSE(Client::Scenes::CameraTourRunning(tour));
        EXPECT_EQ(tested->released, 0);
        ASSERT_TRUE(tested->Step(6000).HasValue());
        EXPECT_FALSE(tour.waiting);
        EXPECT_TRUE(tour.timerRestart);
        EXPECT_TRUE(Client::Scenes::CameraTourRunning(tour));
        EXPECT_EQ(tested->released, 1);
        EXPECT_EQ(tour.scriptStage, 0);
        EXPECT_EQ(tour.step, 0);
        EXPECT_TRUE(tour.moving);
        EXPECT_TRUE(tour.begin);
        EXPECT_EQ(tour.speed, 300.0f);
        EXPECT_EQ(tour.target, glm::vec3(CellCenter(0x71), CellCenter(0xe8), 50.0f));
        // Esc (0x623edc) lo da por cortado: 0x4033d0 vuelve a falso y el paso no hace nada.
        tour.skipped = true;
        EXPECT_TRUE(tested->Step(7000).HasValue());
        EXPECT_EQ(tested->released, 1);
    }
}

// Otras etapas no lo empiezan: el reloj no corre y 0x4033d0 pide la 2. La 3 lo deshace: 0x403d60 borra los personajes
// del guion (0x510ff0) y 0x403300 vuelve todo a sus valores. Sin la UI del juego, 0x403390 corta.
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
    const auto cut = Client::Scenes::StepTheCameraTour(bare);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4035d3"), std::string::npos) << cut.Error();
}

// 0x4034e0 con 0x403430: el primer tramo sale del héroe (+0x144 pasa a 1) y la cámara anda lo andado · rumbo desde la
// salida, 300 por paso; con lo andado en el largo, llega al destino y el guion de la etapa 1 (0x403e30) corre en el
// paso siguiente: su primer alta (0x4039f0) sin los recursos de la entrada corta.
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

// Con +0x36 en 0 (el guion de la etapa terminó), cada etapa sale al tramo siguiente con 0x403d90: +0x140 en 0, +0x144
// uno más y la velocidad del tramo. La 5 no tiene tramo: borra los personajes y da el recorrido por terminado.
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
    // Lo que deja +0x36 en 0: el paso de después del último de cada guion.
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
    EXPECT_FALSE(Client::Scenes::CameraTourRunning(tested->State()));
}

// 0x404c90 en el guion: la etapa 4 espera 3.000 ms y la 1, en el paso 7, 0x294; al vencer, +0x140 uno más.
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

// 0x403760 en el paso 4 de la etapa 1: en su celda (ftol(ftol(x) · 0,01f)), el centro, la acción 0 y +0x38 en 1; si
// no, mientras gira (+0x38), de a 3 grados hacia el ángulo de 0x4036d0 (180 al sur de la celda) con la acción 0; ya de
// frente, deja de girar, +0x386 con la velocidad, la acción 2 y 0x508a80. Con los tres en su celda, +0x140 uno más.
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
        ASSERT_TRUE(Client::Scenes::UpdateActorPosition(
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
        // Ya sin girar camina aunque esté de espaldas; con los tres en su celda, el paso 5.
        turning.position = {CellCenter(0x70), CellCenter(0xe8), 0.0f};
        facing.position = {CellCenter(0x74) - 49.0f, CellCenter(0xe8) + 49.0f, 0.0f};
        ASSERT_TRUE(tested->Step().HasValue());
        EXPECT_EQ(tour.step, 5);
        EXPECT_TRUE(tour.turning[2]);
    }
}

// 0x403900: la acción cambia una vez por vuelta (+0x111 distinta), con +0x114 en 0, y cuenta en +0x78; con la cuenta
// pedida y avanza, +0x78 en 0 y +0x140 uno más. Sin la clave entre los vivos usa el registro 399 (Q-C1349). Con el
// efecto (la etapa 5), el 0xed corta después de contar.
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
    const auto cut = tested->Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4039a0"), std::string::npos) << cut.Error();
    EXPECT_EQ(first.animation.action, 3);
    EXPECT_EQ(tested->State().actionCounts[0x19], 1);
}

// Las altas del guion con el Data del repo, en Lorencia, Devias y Crywolf.
class CameraTourActorsTest : public Client::Tests::GoldenDataTest
{
protected:
    [[nodiscard]] std::unique_ptr<Tour> Load(std::int32_t map) const
    {
        auto tour = std::make_unique<Tour>();
        auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, static_cast<std::uint8_t>(map));
        EXPECT_TRUE(loaded.HasValue()) << loaded.Error();
        if (loaded.HasValue())
            tour->world.Map().terrain = std::move(loaded).Value();
        tour->world.Runtime().map = map;
        tour->dataRoot = m_dataRoot;
        return tour;
    }
};

// 0x404830 en el paso 1: las 27 altas de los altares (0x5141e0 con las claves 10000 en adelante), sin chispas ni
// velocidad, con los rand() de las altas y nada más; la clave en +0x1f4, las manos en 0xffff y el alpha en 0. Dos
// avanzan: +0x140 pasa a 3.
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
                Client::Scenes::CreateMonster(Spawns[index].type, Spawns[index].x, Spawns[index].y,
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

// 0x403e30 con las chispas. En el 34, el alta de la barrera (el 349, paso 0) corta adentro de 0x5141e0, en el joint
// 0x618 de su caso (0x43114d, hito 2); la de un guardia (el 340, paso 1) le pone 0,227f a la acción 2 de su modelo
// 0x1ae, el sonido 0x243 (CL10) y corta en 0x403b09, antes del primer rand() de las chispas. Fuera del 34 el alta es la
// del default de 0x5141e0, que no carga 0x1ae: la velocidad corta (0x403abe). La clave ya subió en los dos cortes.
TEST_F(CameraTourActorsTest, TheSparksOfTheBarrierAreACut)
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
            ASSERT_FALSE(first.HasValue());
            EXPECT_NE(first.Error().find("0x43114d"), std::string::npos) << first.Error();
        }
        auto twin = Load(map);
        ASSERT_TRUE(Client::Scenes::CreateMonster(0x154, 0x71, 0xee, 10000, twin->Resources()).HasValue());
        auto tested = Load(map);
        tested->Running(1, true, 1);
        const auto cut = tested->Step();
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find(map == CrywolfMap ? "0x403b09" : "0x403abe"), std::string::npos) << cut.Error();
        EXPECT_EQ(tested->cloth.random.State(), twin->cloth.random.State());
        EXPECT_EQ(tested->State().nextActorKey, 10001);
        EXPECT_EQ(tested->State().step, 1);
        if (map != CrywolfMap)
            continue;
        EXPECT_EQ(tested->world.Runtime().lastSoundId, 0x243);
        const auto* skeleton = tested->world.FaunaResources().SkeletonOf(0x1ae);
        ASSERT_NE(skeleton, nullptr);
        ASSERT_GT(skeleton->actions.size(), 2u);
        EXPECT_EQ(skeleton->actions[2].speed, std::bit_cast<float>(0x3e6872b0u));
    }
}
} // namespace
