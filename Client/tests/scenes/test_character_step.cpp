#include "Scenes/CharacterStep.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/WorldState.h"
#include "World/TerrainCulling.h"
#include "World/TerrainWalls.h"
#include "World/WorldTerrain.h"

#include "support/GoldenDir.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// Client::Scenes::StepCharacters: 0x508f00 en el juego, después de la espera, con el héroe. El
// TerrainWall, si cada personaje se ve y, del héroe, la zona segura de 0x505de0 con la celda lógica.
// 06 § El paso de los personajes y § Caminar.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::CharacterSeen;
using Client::Scenes::CharacterStepContext;
using Client::Scenes::StepCharacters;
using Client::Scenes::WorldState;
using Client::World::TerrainPolygon;

constexpr std::uint32_t Seed = 0x2468ace1u;
// Los dos accesos de 0x508f00 y los dos de 0x505de0 (4 rand() por cierre) y el rand() & 7 de 0x506cac.
constexpr int WallDraws = 8;
constexpr int SeenHeroDraws = WallDraws + 8 + 1;

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

// Un mundo con el héroe vivo en el lugar HeroSlot, de pie (la acción 1 de 0x511290), con el PK de 0x511290.
struct GameWorld
{
    explicit GameWorld(std::int32_t map = 0)
    {
        world.Runtime().map = map;
        world.Runtime().hero = HeroSlot;
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

    Client::Core::Expected<void, std::string> Step(const std::optional<TerrainPolygon>& polygon)
    {
        return StepCharacters(CharacterStepContext{.world = world, .polygon = polygon, .random = random});
    }

    static constexpr std::size_t HeroSlot = 42;
    WorldState world;
    CrtRandom random{Seed};
};

const std::optional<TerrainPolygon> Everything = TerrainPolygon{.everything = true};
const std::optional<TerrainPolygon> Nothing = std::nullopt;

using GoldenCharacterStep = Client::Tests::GoldenDataTest;

// El plan del hito 1, T11, y la observación 8: cruzando el borde del pueblo, +0x3e2 sigue a la celda lógica
// (+0x41c, +0x420), la que va una adelante, y no a la que se dibuja. En Lorencia (el 0), el borde del este
// en (172, 128) y (173, 128); en Devias (el 2), el del norte en (220, 76) y (220, 77). Con el héroe visto:
// los 8 rand() del TerrainWall, los 8 de la celda de 0x505de0 y el rand() & 7.
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
            const std::optional<TerrainPolygon> polygon = GamePolygon(border.map, hero.position);
            ASSERT_TRUE(CharacterSeen(*polygon, hero.position));

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

// 0x505ef2: sin verse, 0x505de0 no lee la celda ni toca +0x3e2; quedan los 8 rand() del TerrainWall.
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
    const std::optional<TerrainPolygon> polygon = GamePolygon(0, CellCentre(200, 200));
    ASSERT_FALSE(CharacterSeen(*polygon, hero.position));

    const auto stepped = tested.Step(polygon);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(hero.seen);
    EXPECT_EQ(hero.safeZone, 1);
    EXPECT_EQ(tested.Cell(100, 100), Client::World::CharacterWallBit);
    EXPECT_EQ(tested.random.State(), RandomAfter(WallDraws).State());
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
// primeros, vivos o no; el 401 no. Después, 0x508ea0 corta en el primer vivo que no es el héroe.
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

    ASSERT_TRUE(HasError(stepped, "0x508ea0")) << (stepped.HasValue() ? "" : stepped.Error());
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

// Una cuenta abierta en la tabla de protección no se vuelve a cifrar al cerrar: solo queda el rand() & 7.
TEST(CharacterStepTest, AnOpenAccessSpendsNothingOnClosing)
{
    GameWorld tested;
    tested.Hero().cellXAccesses = 1;
    tested.Hero().cellYAccesses = 1;

    ASSERT_TRUE(tested.Step(Everything).HasValue());

    EXPECT_EQ(tested.Hero().cellXAccesses, 1);
    EXPECT_EQ(tested.Hero().cellYAccesses, 1);
    EXPECT_EQ(tested.random.State(), RandomAfter(1).State());
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

// 0x5fba90 lee el trapecio del último dibujo; antes del primero del juego es el de la selección, que MuRet
// no tiene. Corta en el primer lugar, después de sus accesos si está vivo.
TEST(CharacterStepTest, WithoutTheTrapezoidTheStepStopsAtTheFirstSlot)
{
    for (const std::size_t heroSlot : {std::size_t{0}, GameWorld::HeroSlot})
    {
        SCOPED_TRACE(heroSlot);
        GameWorld tested;
        ActorState hero = tested.Hero();
        tested.Hero() = ActorState{};
        tested.world.Runtime().hero = heroSlot;
        tested.world.Actors()[heroSlot] = hero;
        tested.Cell(9, 9) = Client::World::CharacterWallBit;

        const auto stepped = tested.Step(Nothing);

        EXPECT_TRUE(HasError(stepped, "0x5fba90")) << (stepped.HasValue() ? "" : stepped.Error());
        EXPECT_EQ(tested.Cell(9, 9), 0);
        EXPECT_EQ(tested.random.State(), RandomAfter(heroSlot == 0 ? WallDraws : 0).State());
    }
}

// 0x412140 arma los objetos del asedio en el mapa 30, y sin el mapa cargado no hay TerrainWall: corta antes de
// tocar nada.
TEST(CharacterStepTest, TheCastleMapAndAnUnloadedMapStopBeforeTheWalls)
{
    GameWorld castle(30);
    castle.Cell(5, 5) = Client::World::CharacterWallBit;
    const auto inCastle = castle.Step(Everything);
    EXPECT_TRUE(HasError(inCastle, "0x412140")) << (inCastle.HasValue() ? "" : inCastle.Error());
    EXPECT_EQ(castle.Cell(5, 5), Client::World::CharacterWallBit);
    EXPECT_EQ(castle.random.State(), RandomAfter(0).State());

    GameWorld unloaded;
    unloaded.world.Map().terrain.attributes.reset();
    const auto withoutMap = unloaded.Step(Everything);
    EXPECT_TRUE(HasError(withoutMap, "0x508f14")) << (withoutMap.HasValue() ? "" : withoutMap.Error());
    EXPECT_EQ(unloaded.random.State(), RandomAfter(0).State());
}

// 0x505e0b: el héroe sin el modelo 0x20a no está leído; corta después del TerrainWall.
TEST(CharacterStepTest, OnlyThePlayerModelIsStepped)
{
    GameWorld tested;
    tested.Hero().type = 0x100;

    const auto stepped = tested.Step(Everything);

    EXPECT_TRUE(HasError(stepped, "0x505e0b")) << (stepped.HasValue() ? "" : stepped.Error());
    EXPECT_EQ(tested.random.State(), RandomAfter(WallDraws).State());
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

            EXPECT_TRUE(HasError(stepped, "0x5f9780")) << (stepped.HasValue() ? "" : stepped.Error());
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
            EXPECT_TRUE(HasError(stepped, probe.cut)) << (stepped.HasValue() ? "" : stepped.Error());
        }
        EXPECT_EQ(tested.random.State(), RandomAfter(SeenHeroDraws).State());
    }
}
} // namespace
