#include "Scenes/CharacterStep.h"

#include "Core/Expected.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterSlots.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SceneCameras.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldState.h"
#include "World/TerrainCulling.h"
#include "World/TerrainWalls.h"
#include "World/WorldTerrain.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Scenes::StepCharacters: 0x508f00. El TerrainWall, si cada personaje se ve, el 0x508ba0 de los que no
// son el héroe, el cuerpo (0x4fd5f0) por el camino del 0x20a quieto y 0x505de0: entero con el héroe, hasta la
// cabeza con los demás. 06 § El paso de los personajes, § El paso de los personajes en la selección y el login,
// § El cuerpo de los personajes (0x4fd5f0) y § Caminar.
namespace
{
using Client::Render::Models::ActionInfo;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::Skeleton;
using Client::Scenes::ActorState;
using Client::Scenes::CharacterSeen;
using Client::Scenes::CharacterStepContext;
using Client::Scenes::SceneId;
using Client::Scenes::StepCharacters;
using Client::Scenes::WorldState;
using Client::World::TerrainPolygon;

constexpr std::uint32_t Seed = 0x2468ace1u;
// Los dos accesos de 0x508f00 a la celda lógica (4 rand() por cierre).
constexpr int WallDraws = 8;
// 0x4fc450 con el héroe: el registro propio, que se cierra en 0 con los 3.848 de 0x40b4d0.
constexpr int OwnRecordDraws = Client::Render::Models::OwnRecordCloseRandoms;
// El rand() % 6 de 0x4fd7e4.
constexpr int BodyDraws = 1;
// 0x505de0 con el héroe visto: los dos accesos de la celda y el rand() & 7 de 0x506cac.
constexpr int SeenEffectsDraws = 8 + 1;
// El héroe visto, quieto, sin terminar la acción y antes del frame 3: sin el rand() de 0x432bf1 (0x4fcee0).
constexpr int SeenHeroDraws = WallDraws + OwnRecordDraws + BodyDraws + SeenEffectsDraws;
// El rand() de 0x432bf1, desde el frame 3 (0x4fcee0), y el del sufijo de 0x4f2250 al terminar la acción.
constexpr int TrailDraws = 1;
constexpr int StopDraws = 1;
// Los dos accesos de 0x508ba0 con otro que el héroe, la X (0x412890) y la Y (0x40b800).
constexpr int OtherCellDraws = 8;
// Los dos accesos de 0x505de0 con otro visto, antes de cortar en su cabeza (0x5068b9).
constexpr int OtherSeenCellDraws = 8;
// La selección (0x633f20, World55) y el login (0x6349a0, World56).
constexpr std::uint8_t SelectionMap = 54;
constexpr std::uint8_t LoginMap = 55;

CrtRandom RandomAfter(int draws)
{
    CrtRandom random(Seed);
    for (int i = 0; i < draws; ++i)
    {
        static_cast<void>(random.Next());
    }
    return random;
}

glm::vec3 CellCentre(std::uint8_t x, std::uint8_t y)
{
    return {(static_cast<float>(x) * 100.0f) + 50.0f, (static_cast<float>(y) * 100.0f) + 50.0f, 0.0f};
}

// El trapecio del juego alrededor de `centre`, como el que deja 0x5fb570 con la cámara de arranque.
TerrainPolygon GamePolygon(std::uint8_t map, const glm::vec3& centre)
{
    return Client::World::TerrainPolygonOf(Client::World::CullingScene::Game, map, centre, -45.0f);
}

bool HasError(const Client::Core::Expected<void, std::string>& stepped, std::string_view address)
{
    return !stepped.HasValue() && stepped.Error().find(address) != std::string::npos;
}

std::string ErrorOf(const Client::Core::Expected<void, std::string>& stepped)
{
    return stepped.HasValue() ? std::string() : stepped.Error();
}

// Player.bmd con las 205 acciones sin keys y la acción `action` con `keys` keys a `speed` por paso.
Skeleton SkeletonWith(std::uint8_t action, std::int16_t keys, float speed)
{
    Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    skeleton.actions[action] = ActionInfo{.repeats = true, .speed = speed, .keyCount = keys, .lockPositions = false};
    return skeleton;
}

// Un mundo con el héroe vivo en el lugar HeroSlot, de pie (la acción 1 de 0x511290), con el PK de 0x511290, y
// Player.bmd con sus acciones sin keys: el cuerpo no avanza ni termina. El registro propio, con el equipo vacío
// que deja la respuesta del F3 03 (0x4c47c8): 0x4fc450 no pide Item.bmd.
struct GameWorld
{
    explicit GameWorld(std::int32_t map = 0)
    {
        world.Runtime().map = map;
        world.Runtime().hero = HeroSlot;
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
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

    std::uint16_t& Cell(std::uint8_t x, std::uint8_t y)
    {
        return world.Map().terrain.attributes->cells[Client::Data::Terrain::CellIndex(x, y)];
    }

    // Con `polygon` en el global que deja el último 0x5fb570 (WorldRuntime::terrainPolygon).
    Client::Core::Expected<void, std::string> Step(const TerrainPolygon& polygon, SceneId scene = SceneId::Game)
    {
        world.Runtime().terrainPolygon = polygon;
        return Step(scene);
    }

    // Con el global como esté.
    Client::Core::Expected<void, std::string> Step(SceneId scene = SceneId::Game)
    {
        return StepCharacters(
            CharacterStepContext{.world = world, .random = random, .scene = scene, .playerSkeleton = player});
    }

    static constexpr std::size_t HeroSlot = 42;
    WorldState world;
    CrtRandom random{Seed};
    Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    const Skeleton* player = &skeleton;
};

const TerrainPolygon Everything{.everything = true};

class GoldenCharacterStep : public Client::Tests::GoldenDataTest
{
protected:
    // Item.bmd del Data\ del repo.
    std::optional<Client::Data::Tables::LoadedItemTable> LoadItems() const
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
        auto loaded = Client::Data::Tables::LoadItemTable(std::as_bytes(std::span(*file)));
        if (!loaded.HasValue())
        {
            return std::nullopt;
        }
        return std::move(loaded).Value();
    }
};

// Player.bmd del Data\ del repo, con sus 205 acciones (P-C14 a).
std::optional<Skeleton> LoadPlayerSkeleton(const std::filesystem::path& dataRoot)
{
    for (const Client::Data::Models::ModelFile& file : Client::Data::Models::PlayerModelFiles())
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
        auto skeleton = Client::Render::Models::MakePlayerSkeleton(loaded.Value());
        if (!skeleton.HasValue())
        {
            return std::nullopt;
        }
        return std::move(skeleton).Value();
    }
    return std::nullopt;
}

// El plan del hito 1, T11, y la observación 8: cruzando el borde del pueblo, +0x3e2 sigue a la celda lógica
// (+0x41c, +0x420), la que va una adelante, y no a la que se dibuja. En Lorencia (el 0), el borde del este
// en (172, 128) y (173, 128); en Devias (el 2), el del norte en (220, 76) y (220, 77). Con el héroe visto:
// los 8 rand() del TerrainWall, los 3.848 del registro propio y el rand() % 6 de 0x4fd5f0, los 8 de la celda
// de 0x505de0 y el rand() & 7.
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
            auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, border.map);
            ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
            tested.world.Map().terrain = std::move(loaded).Value();
            ASSERT_EQ(tested.Cell(border.insideX, border.insideY) & 1, 1);
            ASSERT_EQ(tested.Cell(border.outsideX, border.outsideY) & 1, 0);
            ActorState& hero = tested.Hero();
            hero.position = CellCentre(walk.drawnX, walk.drawnY);
            hero.cellX = walk.logicalX;
            hero.cellY = walk.logicalY;
            // Lo que dejó 0x511290 con la celda dibujada.
            hero.safeZone = static_cast<std::uint8_t>(1 - walk.safeZone);
            const TerrainPolygon polygon = GamePolygon(border.map, hero.position);
            ASSERT_TRUE(CharacterSeen(polygon, hero.position));

            const auto stepped = tested.Step(polygon);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_TRUE(hero.seen);
            EXPECT_EQ(hero.safeZone, walk.safeZone);
            EXPECT_EQ(tested.Cell(walk.logicalX, walk.logicalY) & Client::World::CharacterWallBit,
                      Client::World::CharacterWallBit);
            EXPECT_EQ(tested.Cell(walk.drawnX, walk.drawnY) & Client::World::CharacterWallBit, 0);
            EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
        }
    }
}

// Lo que saca por paso el héroe quieto en la zona segura, con Player.bmd del repo: la acción 1 (6 keys, 0,28 por
// paso, repite) de un jugador sin armas. Por paso, los 8 del TerrainWall, los 3.848 del registro propio
// (0x4fc450), el rand() % 6 (0x4fd7e4), y los 8 de la celda y el rand() & 7 de 0x505de0: 3.866. Desde el paso 11
// el frame pasa de 3 y 0x432bf1 saca uno más; en el 22 la acción termina (6,16), vuelve al frame 0,16 y 0x4f2250
// saca el de su sufijo, sin el de 0x432bf1. En Lorencia y en Devias.
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
        auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, place.map);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        tested.world.Map().terrain = std::move(loaded).Value();
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

// 0x505ef2: sin verse, 0x505de0 no lee la celda ni toca +0x3e2; quedan los 8 rand() del TerrainWall y los del
// cuerpo, que corre igual.
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
    EXPECT_EQ(tested.Cell(100, 100), Client::World::CharacterWallBit);
    EXPECT_EQ(tested.random.State(), RandomAfter(WallDraws + OwnRecordDraws + BodyDraws).State());
}

// 0x505de0 mira el bit 0x01 del byte bajo del WORD: el 0x02 del héroe y los bits altos no cuentan.
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

// 0x508f14..0x5091a0: el 0x02 sale de toda la grilla y vuelve en la celda lógica (los ocho bits bajos) de
// cada personaje vivo, sin morirse (+0x389) y sin el 8 de +0x88, con sus dos accesos; si se ve, de los 400
// primeros, vivos o no; el 401 no. Después, el primer vivo que no es el héroe, el que se muere, sale de 0x508ba0
// sin leer su celda (0x508bb6) y el paso corta en su cuerpo, en 0x4fb9e0.
TEST(CharacterStepTest, TheWallsAndTheViewCoverTheFirstFourHundred)
{
    GameWorld tested;
    auto actors = tested.world.Actors();
    actors[1].alive = true;
    actors[1].dying = 1;
    actors[1].cellX = 1;
    actors[1].cellY = 1;
    actors[2].alive = true;
    actors[2].byte88 = Client::World::NonBlockingKind;
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

    ASSERT_TRUE(HasError(stepped, "0x4fb9e0")) << ErrorOf(stepped);
    EXPECT_NE(stepped.Error().find("lugar 1 "), std::string::npos) << stepped.Error();
    EXPECT_EQ(tested.Cell(200, 201), 0x0104);
    EXPECT_EQ(tested.Cell(1, 1), 0);
    EXPECT_EQ(tested.Cell(2, 2), 0);
    EXPECT_EQ(tested.Cell(3, 4), Client::World::CharacterWallBit);
    EXPECT_EQ(tested.Cell(30, 40), Client::World::CharacterWallBit);
    EXPECT_EQ(tested.Cell(50, 50), 0);
    EXPECT_TRUE(actors[0].seen);
    EXPECT_TRUE(actors[399].seen);
    EXPECT_FALSE(actors[400].seen);
    EXPECT_EQ(tested.random.State(), RandomAfter(2 * WallDraws).State());
}

// Una cuenta abierta en la tabla de protección no se vuelve a cifrar al cerrar: la celda no gasta, y quedan el
// registro propio, el rand() % 6 del cuerpo y el rand() & 7.
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

// Sin vivos, el paso borra el TerrainWall, escribe +0x16c y no gasta rand().
TEST(CharacterStepTest, WithoutLiveCharactersOnlyTheWallsAndTheViewChange)
{
    GameWorld tested;
    tested.Hero().alive = false;
    tested.Cell(5, 5) = Client::World::CharacterWallBit;

    ASSERT_TRUE(tested.Step(Everything).HasValue());

    EXPECT_EQ(tested.Cell(5, 5), 0);
    EXPECT_TRUE(tested.Hero().seen);
    EXPECT_EQ(tested.random.State(), RandomAfter(0).State());
}

// Q-C217: antes de cualquier 0x5fb570 el global tiene los ceros de la .bss, un trapecio sin área en (0, 0): los
// cuatro lados dan 0, que pasa el margen de −20, y se ven todos, en cualquier lugar, también el muerto del lugar
// 0. Con un WorldState nuevo (lo que deja WinMain) el paso sigue entero.
TEST(CharacterStepTest, WithTheTrapezoidOfTheBssEveryoneIsSeen)
{
    GameWorld tested;
    ASSERT_EQ(tested.world.Runtime().terrainPolygon.x, (std::array<float, 4>{}));
    ASSERT_EQ(tested.world.Runtime().terrainPolygon.y, (std::array<float, 4>{}));
    ASSERT_FALSE(tested.world.Runtime().terrainPolygon.everything);
    tested.Hero().position = CellCentre(250, 3);
    tested.world.Actors()[0].position = CellCentre(7, 200);

    const auto stepped = tested.Step();

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(tested.Hero().seen);
    EXPECT_TRUE(tested.world.Actors()[0].seen);
    EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
}

// 0x412140 arma los objetos del asedio en el mapa 30, y sin el mapa cargado no hay TerrainWall: corta antes de
// tocar nada.
TEST(CharacterStepTest, TheCastleMapAndAnUnloadedMapStopBeforeTheWalls)
{
    GameWorld castle(30);
    castle.Cell(5, 5) = Client::World::CharacterWallBit;
    const auto inCastle = castle.Step(Everything);
    EXPECT_TRUE(HasError(inCastle, "0x412140")) << ErrorOf(inCastle);
    EXPECT_EQ(castle.Cell(5, 5), Client::World::CharacterWallBit);
    EXPECT_EQ(castle.random.State(), RandomAfter(0).State());

    GameWorld unloaded;
    unloaded.world.Map().terrain.attributes.reset();
    const auto withoutMap = unloaded.Step(Everything);
    EXPECT_TRUE(HasError(withoutMap, "0x508f14")) << ErrorOf(withoutMap);
    EXPECT_EQ(unloaded.random.State(), RandomAfter(0).State());
}

// 0x4fc450, solo con el héroe publicado: con el registro propio abierto, cada WORD de +0x3e, +0x40 y +0x42
// baja si es positivo y, en 0, apaga su bit de +0x3c y llama a sus helpers; con +0x40 en 0, las 16 marcas de
// la lista de [0x6eed3c] vuelven a 0. Al cerrar, los 3.848 rand() si la cuenta queda en 0; con el registro ya
// abierto, ninguno.
TEST(CharacterStepTest, TheOwnRecordTimersRunWithTheHero)
{
    for (const std::uint8_t depth : {std::uint8_t{0}, std::uint8_t{1}})
    {
        SCOPED_TRACE(::testing::Message() << "con la cuenta en " << static_cast<int>(depth));
        GameWorld tested;
        Client::Scenes::OwnCharacterRecord& own = tested.world.OwnCharacter();
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

// Con otro que el héroe, 0x4fc450 sale en 0x4fc464: el registro propio queda como estaba y no gasta.
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

// 0x4e9980 desde 0x4fd72e: con +0x16d en 1 (0x511290) el alpha sube de a 0,05f, en doble, y llega a 1 en el paso
// 20, el de la entrada (la respuesta del F3 03 lo deja en 0); la luz del blendMesh (+0x68) baja con él.
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

// 0x4f9580: la velocidad de la acción (0,4 por paso acá) por 1,5 en las 3 y 4 con +0x3e3, por 0,5 en la 0x3f
// pasado el frame 6 y en la 0x44, por 0,1f en las 0x4d, 0x4f y 0x5a pasado el 5,5, y de nuevo por 0,5 en la 0x44.
// En la clase 4, las 0x94, 0x51 y 0x5b entre el frame 1 y el 3 cortan en 0x59e680 sin avanzar.
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
          Case{0x5a, 0, 1, 5.6f, 5.6f + (Speed * 0.1f), "0x4fdb70"}, Case{0x44, 0, 1, 0.0f, Speed * 0.5f, "0x506e5d"},
          Case{0x51, 0, 4, 2.0f, 2.0f, "0x4f969d"}, Case{0x51, 0, 4, 3.5f, 3.5f + Speed, ""},
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

// 0x4fcd00 con la acción que terminó (+0x78 & 0x1000: 0x4f9580 devuelve 0 sin avanzar, en cada paso) va a
// 0x4fcb10: las de antes de la 0x0d, las 0xbd a 0xbf, las 0x54 y 0x55 y, de la 0x22 a la 0xc2, las que no
// están en la lista vuelven a la de parado (0x4f2250: la 1 del jugador sin armas, con el rand() de su sufijo);
// las demás quedan; la 0x44 vuelve al frame 0; las 0xc3 y 0xc4 cortan en 0x52e810.
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
          Case{0xc2, 1, "", Stopped}, Case{0x0d, 0x0d, "0x508668", SeenHeroDraws},
          Case{0x8c, 0x8c, "0x508668", SeenHeroDraws}, Case{0x46, 0x46, "", SeenHeroDraws},
          Case{0x6a, 0x6a, "", SeenHeroDraws}, Case{0x75, 0x75, "", SeenHeroDraws}, Case{0x76, 1, "", Stopped},
          Case{0xc5, 0xc5, "", SeenHeroDraws}, Case{0xc3, 0xc3, "0x4fcb87", WallDraws + OwnRecordDraws},
          Case{0xc4, 0xc4, "0x4fcb87", WallDraws + OwnRecordDraws}})
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
        EXPECT_EQ(tested.random.State(), RandomAfter(probe.draws).State());
    }
}

// 0x4fcb71: la 0x44 que terminó se queda y vuelve al frame 0; después, 0x505de0 corta en sus efectos.
TEST(CharacterStepTest, AnEndedActionFortyFourRestartsItsFrame)
{
    GameWorld tested;
    ActorState& hero = tested.Hero();
    hero.flags78 = 0x1000;
    hero.animation.action = 0x44;
    hero.animation.frame = 2.5f;

    const auto stepped = tested.Step(Everything);

    EXPECT_TRUE(HasError(stepped, "0x506e5d")) << ErrorOf(stepped);
    EXPECT_EQ(hero.animation.action, 0x44);
    EXPECT_EQ(hero.animation.frame, 0.0f);
    EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
}

// 0x4fcee0: antes del frame 3, salvo en la 0x8d, sale; si no, con el 0x20a, la estela según la acción y el arma
// de la mano (la izquierda con las 0x25 y 0x26) corta en 0x52e470, y sin estela 0x432bd0 saca su rand() sin
// condición (0x432bf1).
TEST(CharacterStepTest, TheWeaponTrailDrawsFromFrameThree)
{
    constexpr std::uint16_t NoItem = Client::Scenes::NoItemModel;
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

        const auto stepped = tested.Step(Everything);

        if (probe.trail)
        {
            EXPECT_TRUE(HasError(stepped, "0x4fd236")) << ErrorOf(stepped);
        }
        else
        {
            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        EXPECT_EQ(tested.random.State(), RandomAfter(probe.draws).State());
    }
}

// Lo que el cuerpo (0x4fd5f0) no tiene corta, en su orden y después de los rand() de antes: los mapas 18 a 23
// (0x4fbe10), el 0xc5 (0x4fd603), el 0xf1 (0x4fb910), muriéndose (0x4fb9e0), +0x88 en 4 (0x4fcd0c), el nombre de
// 0x6c88b0 (0x4fcda2), otro modelo, Player.bmd sin cargar o una acción fuera de él (0x4f9580, 0x4f95c0) y la
// partícula de las 0x4d, 0x4f y 0x5a (0x4fdb70).
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
        Case{"mapa 18", [](GameWorld& tested) { tested.world.Runtime().map = 18; }, "0x4fbe10", WallDraws},
        Case{"mapa 23", [](GameWorld& tested) { tested.world.Runtime().map = 23; }, "0x4fbe10", WallDraws},
        Case{"el 0xc5", [](GameWorld& tested) { tested.Hero().type = 0xc5; }, "0x4fd603", WallDraws},
        Case{"el 0xf1", [](GameWorld& tested) { tested.Hero().type = 0xf1; }, "0x4fb910", AfterTheTimers},
        Case{"se muere", [](GameWorld& tested) { tested.Hero().dying = 1; }, "0x4fb9e0", OwnRecordDraws},
        Case{"+0x88 en 4", [](GameWorld& tested) { tested.Hero().byte88 = 4; }, "0x4fcd0c", AfterTheTimers},
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
             "0x4fcda2", AfterTheTimers},
        Case{"otro modelo", [](GameWorld& tested) { tested.Hero().type = 0x100; }, "0x4f9580", AfterTheTimers},
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
             "0x4fdb70", AfterTheTimers + BodyDraws},
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
}

// Los bordes de esos cortes: los mapas 17 y 24, el nombre con +0x1e3 en 0 (el que deja la respuesta del F3 03) y
// la partícula en el frame 5,5 justo siguen el camino entero.
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

// 0x506acf..0x506b62: con +0x375 debajo de 6, un arma con luz en cualquiera de las dos manos pide el buffer
// dinámico (0x5f9780), que MuRet no tiene; corta después de la zona segura. Con 6 o más, no hay luces.
TEST(CharacterStepTest, TheHandLightsStopBelowPkSix)
{
    for (const std::uint16_t model : std::array<std::uint16_t, 5>{0x21e, 0x225, 0xa24, 0xc1c, 0x61f})
    {
        for (const std::size_t hand : {std::size_t{0}, std::size_t{1}})
        {
            SCOPED_TRACE(::testing::Message() << model << " en la mano " << hand);
            GameWorld tested;
            tested.Hero().hands[hand].model = model;
            tested.Cell(0, 0) = 1;

            const auto stepped = tested.Step(Everything);

            EXPECT_TRUE(HasError(stepped, "0x5f9780")) << ErrorOf(stepped);
            EXPECT_EQ(tested.Hero().safeZone, 1);
            EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws - 1).State());

            GameWorld outlaw;
            outlaw.Hero().hands[hand].model = model;
            outlaw.Hero().pk = 6;
            EXPECT_TRUE(outlaw.Step(Everything).HasValue());
            EXPECT_EQ(outlaw.random.State(), RandomAfter(SeenHeroDraws).State());
        }
    }
}

// El caso del 0x20a en 0x506cf8 y los pasos del héroe (0x50862f), después del rand() & 7: los mapas 7 y 9, las
// acciones 0x44, 0x45 y 0x96, y las de los pasos (0x0d a 0x21, 0x8b, 0x8c y 0x4b) cortan. Las demás, no.
TEST(CharacterStepTest, ThePlayerEffectsAndStepsStopAfterTheRandom)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t action;
        std::string_view cut;
    };
    for (const Case& probe :
         {Case{7, 1, "0x506d01"}, Case{9, 1, "0x506d8b"}, Case{0, 0x44, "0x506e5d"}, Case{0, 0x45, "0x506e5d"},
          Case{0, 0x96, "0x506f62"}, Case{0, 0x0d, "0x508668"}, Case{0, 0x21, "0x508668"}, Case{2, 0x8b, "0x508668"},
          Case{2, 0x8c, "0x508668"}, Case{2, 0x4b, "0x508668"}, Case{2, 0x0c, ""}, Case{0, 0x22, ""}})
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
        EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
    }
}

// 0x508ba0 con otro que el héroe, quieto: la X lógica y la Y en dos accesos que se cierran (8 rand()), iguales a
// la copia en claro (0x511290 escribe las dos), así que no busca camino (Q-C236). Su cuerpo (0x4fd5f0) saca el
// rand() % 6, sin el registro propio, y su 0x505de0, visto, la celda (8 más) y corta en la cabeza (0x5068b9),
// antes del héroe del lugar 42. En Lorencia y en Devias.
TEST(CharacterStepTest, AnotherCharacterSpendsItsCellsAndItsBodyAndStopsAtItsHead)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(::testing::Message() << "mapa " << map);
        GameWorld tested(map);
        ActorState& other = tested.world.Actors()[3];
        other = Client::Tests::UnarmedPlayer(2);
        other.alive = true;
        other.byte88 = 1;
        other.cellX = 3;
        other.cellY = 4;

        const auto stepped = tested.Step(Everything);

        ASSERT_TRUE(HasError(stepped, "0x5068b9")) << ErrorOf(stepped);
        EXPECT_NE(stepped.Error().find("lugar 3 "), std::string::npos) << stepped.Error();
        EXPECT_TRUE(other.seen);
        EXPECT_EQ(other.cellXAccesses, 0);
        EXPECT_EQ(other.cellYAccesses, 0);
        EXPECT_EQ(tested.Cell(3, 4), Client::World::CharacterWallBit);
        EXPECT_EQ(tested.random.State(),
                  RandomAfter((2 * WallDraws) + OtherCellDraws + BodyDraws + OtherSeenCellDraws).State());
    }
}

// Las salidas de 0x508ba0 antes de la celda: el que se muere sale (0x508bb6) y corta en su cuerpo (0x4fb9e0),
// salvo el 0x154, que toma la altura del terreno (0x508e89); el que camina sigue su camino (0x508e27); el 0x159
// sale sin leer la celda y corta en su modelo (0x4f9580). Una cuenta abierta no se vuelve a cifrar al cerrar: ni
// en 0x508ba0 ni en 0x505de0, que corta en la cabeza. Cada uno, con el héroe vivo en el lugar 42, después del
// TerrainWall.
TEST(CharacterStepTest, TheExitsOf508ba0BeforeTheCell)
{
    struct Case
    {
        std::string_view name;
        std::uint16_t type;
        std::uint8_t dying;
        std::uint8_t movement;
        std::uint8_t accesses;
        std::string_view cut;
        int draws;
    };
    constexpr std::uint16_t Player = Client::Data::Models::PlayerSkeletonModel;
    for (const Case& probe : {Case{"se muere", Player, 1, 0, 0, "0x4fb9e0", WallDraws},
                              Case{"se muere el 0x154", 0x154, 1, 0, 0, "0x508e89", WallDraws},
                              Case{"camina", Player, 0, 1, 0, "0x508e27", 2 * WallDraws},
                              Case{"el 0x159", 0x159, 0, 0, 0, "0x4f9580", 2 * WallDraws},
                              Case{"con las cuentas abiertas", Player, 0, 0, 1, "0x5068b9", WallDraws + BodyDraws}})
    {
        SCOPED_TRACE(probe.name);
        GameWorld tested;
        ActorState& other = tested.world.Actors()[3];
        other.alive = true;
        other.byte88 = 1;
        other.type = probe.type;
        other.dying = probe.dying;
        other.movement = probe.movement;
        other.cellXAccesses = probe.accesses;
        other.cellYAccesses = probe.accesses;

        const auto stepped = tested.Step(Everything);

        EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        EXPECT_EQ(other.cellXAccesses, probe.accesses);
        EXPECT_EQ(other.cellYAccesses, probe.accesses);
        EXPECT_EQ(tested.random.State(), RandomAfter(probe.draws).State());
    }
}

// La selección (0x6342f9) y el login (0x634d13) sobre su mapa del repo, con el .att del mapa en el global del
// TerrainWall (0x7d0d348, que 0x61a560 pisa), el mundo en curso y el héroe publicado en `hero`. Los vivos son
// jugadores de pie en sus lugares, con las dos copias de la celda en (0, 0), como los deja 0x511290 desde
// 0x517270 o 0x6349a0, y +0x88 en 1. El registro propio queda como lo deja el login, en cero
// (0x4baf79..0x4bafc2): su equipo es del tipo 0, y 0x54efe0 lee el +0x2d de sus alas en Item.bmd (`items`).
struct SceneCharacters
{
    SceneCharacters(const std::filesystem::path& dataRoot, std::uint8_t map, std::size_t hero)
    {
        auto loaded = Client::World::LoadWorldTerrain(dataRoot, map);
        if (!loaded.HasValue())
        {
            ADD_FAILURE() << loaded.Error();
            return;
        }
        world.Map().terrain = std::move(loaded).Value();
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

    // Los cinco lugares de 0x4c20a0, con la z de 0x634680.
    void PlaceTheFiveSlots()
    {
        for (std::size_t slot = 0; slot < Client::Scenes::CharacterSlots.size(); ++slot)
        {
            const glm::vec2 xy = Client::Scenes::CharacterSlots[slot].position;
            Place(slot, glm::vec3(xy.x, xy.y, Client::Scenes::CharacterSlotHeight));
        }
    }

    Client::Core::Expected<void, std::string> Step(SceneId scene)
    {
        return StepCharacters(CharacterStepContext{
            .world = world, .random = random, .scene = scene, .playerSkeleton = &skeleton, .items = items});
    }

    WorldState world;
    CrtRandom random{Seed};
    Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    const Client::Data::Tables::ItemTable* items = nullptr;
};

// El trapecio que deja el dibujo de la selección (0x63475c), el de su punto fijo.
TerrainPolygon SelectionPolygon()
{
    return Client::World::TerrainPolygonOf(Client::World::CullingScene::CharacterSelect, SelectionMap,
                                           Client::Scenes::SelectionCullingPoint,
                                           Client::Scenes::SelectionCamera().angles.z);
}

// La selección con los cinco lugares y el héroe en el 0, con el trapecio de su dibujo: los cinco se ven (P-C46 b,
// con la cuenta de MuRet) y marcan su celda lógica, la (0, 0), con 8 rand() cada uno. El héroe sale de 0x508ba0
// sin gastar (0x508bad) y hace el camino del juego: su cuerpo con el registro propio y su 0x505de0 entero. El
// lugar 1 gasta su 0x508ba0, su cuerpo y su celda, y corta en su cabeza (0x5068b9).
TEST_F(GoldenCharacterStep, TheSelectionStopsAtTheHeadOfTheSecondSlot)
{
    const auto items = LoadItems();
    ASSERT_TRUE(items.has_value());
    SceneCharacters tested(m_dataRoot, SelectionMap, 0);
    tested.items = &items->table;
    tested.PlaceTheFiveSlots();
    tested.world.Runtime().terrainPolygon = SelectionPolygon();

    const auto stepped = tested.Step(SceneId::CharacterSelect);

    ASSERT_TRUE(HasError(stepped, "0x5068b9")) << ErrorOf(stepped);
    EXPECT_NE(stepped.Error().find("lugar 1 "), std::string::npos) << stepped.Error();
    for (std::size_t slot = 0; slot < Client::Scenes::CharacterSlots.size(); ++slot)
    {
        EXPECT_TRUE(tested.world.Actors()[slot].seen) << "lugar " << slot;
    }
    const std::uint16_t origin = tested.world.Map().terrain.attributes->cells[0];
    EXPECT_EQ(origin & Client::World::CharacterWallBit, Client::World::CharacterWallBit);
    const int hero = OwnRecordDraws + BodyDraws + SeenEffectsDraws;
    const int second = OtherCellDraws + BodyDraws + OtherSeenCellDraws;
    EXPECT_EQ(tested.random.State(), RandomAfter((5 * WallDraws) + hero + second).State());
}

// Sin Item.bmd, el héroe de la selección corta en las alas del tipo 0 de su registro en cero (0x54efe0, desde
// 0x4fc450), después de los TerrainWall y de cerrar el registro propio.
TEST_F(GoldenCharacterStep, WithoutItemBmdTheSelectionHeroStopsAtItsWings)
{
    SceneCharacters tested(m_dataRoot, SelectionMap, 0);
    tested.PlaceTheFiveSlots();
    tested.world.Runtime().terrainPolygon = SelectionPolygon();

    const auto stepped = tested.Step(SceneId::CharacterSelect);

    ASSERT_TRUE(HasError(stepped, "0x54efe0 (0x54f30e)")) << ErrorOf(stepped);
    EXPECT_EQ(tested.random.State(), RandomAfter((5 * WallDraws) + OwnRecordDraws).State());
}

// Después de la vuelta, el héroe publicado sigue en el lugar del juego (Q-C213), que 0x510f70(−1) mató: los cinco
// son otros, y el lugar 0 gasta su 0x508ba0, su cuerpo sin el registro propio y su celda antes de cortar en su
// cabeza.
TEST_F(GoldenCharacterStep, AfterTheReturnTheFirstSlotStopsAtItsHead)
{
    SceneCharacters tested(m_dataRoot, SelectionMap, 42);
    tested.PlaceTheFiveSlots();
    tested.world.Runtime().terrainPolygon = SelectionPolygon();

    const auto stepped = tested.Step(SceneId::CharacterSelect);

    ASSERT_TRUE(HasError(stepped, "0x5068b9")) << ErrorOf(stepped);
    EXPECT_NE(stepped.Error().find("lugar 0 "), std::string::npos) << stepped.Error();
    EXPECT_EQ(tested.random.State(),
              RandomAfter((5 * WallDraws) + OtherCellDraws + BodyDraws + OtherSeenCellDraws).State());
}

// El login con el personaje del lugar 2 en su lugar (0x6349a0, LoginCharacterTransform) y el héroe del lugar 0 sin
// construir, con el trapecio de la .bss del primer paso (Q-C217): 8 rand() del TerrainWall, 8 de su 0x508ba0, el
// de su cuerpo y 8 de su celda, y corta en su cabeza.
TEST_F(GoldenCharacterStep, TheLoginStopsAtTheHeadOfItsCharacter)
{
    SceneCharacters tested(m_dataRoot, LoginMap, 0);
    ActorState& login = tested.Place(2, glm::vec3(10603.0f, 14112.0f, 580.0f));

    const auto stepped = tested.Step(SceneId::Login);

    ASSERT_TRUE(HasError(stepped, "0x5068b9")) << ErrorOf(stepped);
    EXPECT_NE(stepped.Error().find("lugar 2 "), std::string::npos) << stepped.Error();
    EXPECT_TRUE(login.seen);
    EXPECT_FALSE(tested.world.Actors()[0].alive);
    EXPECT_EQ(tested.random.State(), RandomAfter(WallDraws + OtherCellDraws + BodyDraws + OtherSeenCellDraws).State());
}

// Fuera del juego el héroe hace el mismo camino: con el héroe solo, en un mapa del juego (el 0 y el 2) y con la
// selección o el login de escena, el cuerpo y 0x505de0 enteros, con los mismos rand().
TEST(CharacterStepTest, OutsideTheGameTheHeroTakesTheSamePath)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const SceneId scene : {SceneId::CharacterSelect, SceneId::Login})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", " << Client::Scenes::SceneName(scene));
            GameWorld tested(map);
            tested.Hero().safeZone = 1;

            const auto stepped = tested.Step(Everything, scene);

            EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(tested.Hero().safeZone, 0);
            EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
        }
    }
}

// 0x506cf8: el 0x607 del mapa 7 es solo de la escena 5; en las otras el mapa 7 sigue de largo.
TEST(CharacterStepTest, TheClockEffectOfMapSevenIsOnlyInTheGame)
{
    GameWorld game(7);
    EXPECT_TRUE(HasError(game.Step(Everything), "0x506d01"));
    GameWorld selection(7);
    const auto stepped = selection.Step(Everything, SceneId::CharacterSelect);
    EXPECT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(selection.random.State(), RandomAfter(SeenHeroDraws).State());
}

// En la selección (la escena 4) 0x4ec200 decide el final con el frame + 2 (Q-C21): la acción de 6 keys a 1 por
// paso termina en el frame 4 y vuelve a la de parado, con el rand() del sufijo de 0x4f2250; en el juego, no.
TEST(CharacterStepTest, TheSelectionEndsTheActionTwoFramesEarlier)
{
    for (const SceneId scene : {SceneId::Game, SceneId::CharacterSelect})
    {
        SCOPED_TRACE(Client::Scenes::SceneName(scene));
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
