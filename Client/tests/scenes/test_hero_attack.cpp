#include "Scenes/HeroAttack.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Packet.h"
#include "Protocol/Combat.h"
#include "Protocol/Movement.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/ActorState.h"
#include "Scenes/AttackRules.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "World/WorldTerrain.h"

#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// CL9 hito 3: si el elegido se puede atacar (0x558bb0), el freno de 300 ms (0x576791c) y el caso 3 de 0x5650c0, el
// ataque al llegar, con los bytes del D3 de girar y del D7. Las claves y los nombres son inventados.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 99;
constexpr std::uint32_t OwnRecordDraws = 0xf08;
constexpr std::uint32_t CellDraws = 4;

void Name(ActorState& actor, std::string_view name)
{
    actor.name.fill(std::byte{0});
    for (std::size_t i = 0; i < name.size() && i < actor.name.size(); ++i)
        actor.name[i] = static_cast<std::byte>(name[i]);
}

std::vector<std::byte> Plain(const Client::Network::Packet& packet)
{
    const auto decoded = Client::Network::Crypto::Xor32Decode(packet);
    return {decoded.Bytes().begin(), decoded.Bytes().end()};
}

std::vector<std::byte> Bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> bytes;
    for (const int value : values)
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

class HeroAttackTest : public ::testing::Test
{
protected:
    HeroAttackTest()
    {
        Reset(0);
    }

    // El héroe en (182, 115), quieto, llegando con +0x379 en 3 y el blanco de 0x6c8ebc una celda al este.
    void Reset(std::int32_t map)
    {
        m_random = CrtRandom(Seed);
        m_sent.clear();
        Runtime().map = map;
        Runtime().hero = HeroIndex;
        ActorState& hero = Hero();
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        hero.key = 0x2a;
        hero.byte88 = 1;
        hero.cellX = 182;
        hero.cellY = 115;
        hero.position = {18250.0f, 11550.0f, 0.0f};
        hero.movementType = 3;
        hero.movement = 1;
        ActorState& target = Target();
        target.alive = true;
        target.type = 0x10;
        target.key = TargetKey;
        target.byte88 = 2;
        target.position = {18350.0f, 11550.0f, 0.0f};
        Runtime().movement.attackTarget = TargetIndex;
        Runtime().movement.mouseUpdateTime = 0;
        Runtime().movement.mouseUpdateTimeMax = 16;
    }

    [[nodiscard]] Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }
    [[nodiscard]] ActorState& Hero()
    {
        return m_world->Actors()[HeroIndex];
    }
    [[nodiscard]] ActorState& Target()
    {
        return m_world->Actors()[TargetIndex];
    }
    // El contexto de la llegada con los envíos grabados, como los manda la sesión (XOR32 y en claro).
    [[nodiscard]] Client::Scenes::HeroArrivalContext Arrival()
    {
        return {.combat = {.world = *m_world, .random = m_random, .playerSkeleton = &m_skeleton},
                .sendWalk = [this](std::span<const Client::Protocol::Movement::Cell> path,
                                   Client::Protocol::Movement::Cell target,
                                   float angle) -> Client::Core::Expected<void, std::string>
                {
                    const auto packet = Client::Protocol::Movement::WriteWalk(path, target, angle);
                    if (!packet.HasValue())
                        return Client::Core::Unexpected(std::string("D3 inválido"));
                    m_sent.push_back(Plain(packet.Value().packet));
                    return {};
                },
                .sendAttack = [this](std::uint16_t key, std::uint8_t action,
                                     std::uint8_t direction) -> Client::Core::Expected<void, std::string>
                {
                    m_sent.push_back(Plain(Client::Protocol::Combat::WriteAttack(key, action, direction).packet));
                    return {};
                }};
    }
    [[nodiscard]] static std::uint32_t After(std::uint32_t count)
    {
        CrtRandom expected(Seed);
        expected.Discard(count);
        return expected.State();
    }
    // El elegido de 0x6c8e30 para 0x558bb0.
    ActorState& Chosen(std::size_t index)
    {
        Runtime().selectedTargets[2] = static_cast<std::int32_t>(index);
        return m_world->Actors()[index];
    }

    static constexpr std::size_t HeroIndex = 3;
    static constexpr std::size_t TargetIndex = 13;
    static constexpr std::uint16_t TargetKey = 0x1234;
    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    CrtRandom m_random{Seed};
    Client::Render::Models::Skeleton m_skeleton = Client::Tests::PlayerActionsSkeleton();
    std::vector<std::vector<std::byte>> m_sent;
};

// 0x565107..0x56601e: el blanco a una celda al este (90 grados, rumbo 3). El héroe toma la acción de pegar, mira al
// blanco, manda C1 06 D3 X Y 30 (el giro de un paso) y C1 07 D7 12 34 78 03, que FoxAnti saca como A4. El rand():
// el registro propio del alcance y el de 0x4f3f80 (3848 cada uno) y las dos celdas del héroe publicado y las dos del
// que pega (4 cada una).
// El ataque al llegar no mira el mapa: igual en Lorencia, Devias y Blood Castle.
TEST_F(HeroAttackTest, InRangeTheHeroTurnsAndSendsTheD3AndTheD7)
{
    for (const std::int32_t map : {0, 2, 11})
    {
        Reset(map);
        const auto attacked = Client::Scenes::InteractOnArrival(Hero(), Arrival());
        ASSERT_TRUE(attacked.HasValue()) << attacked.Error();

        ASSERT_EQ(m_sent.size(), 2u);
        EXPECT_EQ(m_sent[0], Bytes({0xc1, 0x06, 0xd3, 182, 115, 0x30}));
        EXPECT_EQ(m_sent[1], Bytes({0xc1, 0x07, 0xd7, 0x12, 0x34, 0x78, 0x03}));
        EXPECT_EQ(Hero().animation.action, 0x22);
        EXPECT_EQ(Hero().attackCount, 1);
        EXPECT_EQ(Hero().byte381, 1);
        EXPECT_EQ(Hero().angles.z, 90.0f);
        EXPECT_EQ(Hero().attackPosition, Target().position);
        EXPECT_EQ(Hero().attackTarget, TargetIndex);
        EXPECT_EQ(Hero().movement, 0);
        EXPECT_EQ(Runtime().movement.mouseUpdateTime, 16);
        EXPECT_EQ(Runtime().movement.targetX, 183u);
        EXPECT_EQ(Runtime().movement.targetY, 115u);
        EXPECT_EQ(m_random.State(), After(2 * OwnRecordDraws + 4 * CellDraws));
    }
}

// La clave del blanco viaja entera, y el rumbo del D7 es el del héroe después de girar: el blanco en y + 100 (180
// grados) da el rumbo 5.
TEST_F(HeroAttackTest, TheD7CarriesTheWholeKeyAndTheTurnedDirection)
{
    Target().key = 0x8001;
    Target().position = {18250.0f, 11650.0f, 0.0f};
    ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), Arrival()).HasValue());
    ASSERT_EQ(m_sent.size(), 2u);
    EXPECT_EQ(m_sent[0], Bytes({0xc1, 0x06, 0xd3, 182, 115, 0x50}));
    EXPECT_EQ(m_sent[1], Bytes({0xc1, 0x07, 0xd7, 0x80, 0x01, 0x78, 0x05}));
}

// 0x5659e6..0x565a47: con "webzen" en el nombre del héroe, el D3 sale pero el D7 no.
TEST_F(HeroAttackTest, AHeroNamedWebzenTurnsButDoesNotHit)
{
    Name(Hero(), "xwebzenx");
    ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), Arrival()).HasValue());
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0][2], std::byte{0xd3});
    EXPECT_EQ(Hero().attackTarget, TargetIndex);

    m_sent.clear();
    Name(Hero(), "webze");
    ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(m_sent.size(), 2u);
}

// Sin los envíos (el socket cerrado), pega igual y no manda nada.
TEST_F(HeroAttackTest, WithoutSendersTheHeroStillHits)
{
    auto context = Arrival();
    context.sendWalk = nullptr;
    context.sendAttack = nullptr;
    ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), context).HasValue());
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Hero().animation.action, 0x22);
    EXPECT_EQ(m_random.State(), After(2 * OwnRecordDraws + 4 * CellDraws));
}

// 0x565107..0x56526a: el alcance es 1,8 celdas a mano o con espada, 2,2 con las lanzas (0x600..0x7ff en la derecha),
// 6 con arcos y ballestas y 7 con +0x376 en 9. El blanco a dos celdas queda fuera con 1,8 y dentro con 2,2.
TEST_F(HeroAttackTest, TheRangeFollowsTheOwnWeapons)
{
    Target().position = {18450.0f, 11550.0f, 0.0f};
    ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Hero().byte381, 0);
    EXPECT_EQ(Runtime().movement.targetX, 184u);
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));

    m_world->OwnCharacter().equipment[0].id = 0x600;
    ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(m_sent.size(), 2u);

    // A seis celdas, el arco de la izquierda llega.
    m_sent.clear();
    Hero().animation.action = 0;
    Target().position = {18850.0f, 11550.0f, 0.0f};
    m_world->OwnCharacter().equipment[0].id = 0xffff;
    m_world->OwnCharacter().equipment[1].id = 0x800;
    ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(m_sent.size(), 2u);

    m_sent.clear();
    Target().position = {18950.0f, 11550.0f, 0.0f};
    ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_TRUE(m_sent.empty());
    Hero().word376 = 9;
    ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(m_sent.size(), 2u);
}

// 0x565349..0x566299: fuera de alcance sólo el elfo se acerca. Lee la Y y la X de su celda y busca con PathFinding2 un
// camino hasta las celdas a menos del alcance del blanco (1,8 sin arco: r = 1, el cuadrado de 3 × 3 alrededor de
// (192, 115)); con camino, +0x378 en 1, sin D3 ni D7. Sin TerrainWall corta.
TEST_F(HeroAttackTest, OutOfRangeOnlyTheElfWalksIntoRange)
{
    Target().position = {19250.0f, 11550.0f, 0.0f};
    Hero().movement = 0;
    ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(Hero().movement, 0);
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));

    Hero().characterClass = 2;
    m_random = CrtRandom(Seed);
    const auto cut = Client::Scenes::InteractOnArrival(Hero(), Arrival());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x566272"), std::string::npos);

    m_world->Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    m_random = CrtRandom(Seed);
    ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(Hero().movement, 1);
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Hero().byte381, 0);
    ASSERT_EQ(Hero().path.count, 10u);
    EXPECT_EQ(Hero().path.cells[0].x, 182);
    EXPECT_EQ(Hero().path.cells[9].x, 191);
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws + 2 * CellDraws));
}

// Las salidas antes de pegar: sin blanco (0x6c8ebc en −1) después del alcance, el héroe pegando ya (0x8d, o 3 y 4 si
// no es un jugador) y la zona segura (después de anotar la celda del blanco).
TEST_F(HeroAttackTest, TheEarlyExitsDoNotHit)
{
    Runtime().movement.attackTarget = -1;
    ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));

    Runtime().movement.attackTarget = TargetIndex;
    Hero().animation.action = 0x8d;
    ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(Runtime().movement.targetX, 0u);

    Hero().animation.action = 0;
    Hero().safeZone = 1;
    ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(Runtime().movement.targetX, 183u);
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Hero().byte381, 0);
}

// 0x565289..0x565296: entre 0x22 y 0x97 sólo la 0x8d sale; con otra de esas acciones el héroe vuelve a pegar.
TEST_F(HeroAttackTest, OnlyAction8DStopsAPlayerThatIsAlreadyHitting)
{
    Hero().animation.action = 0x23;
    ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(m_sent.size(), 2u);
}

// 0x5650ef..0x565100: la tabla por +0x379 − 1. El 0 y los de afuera de 1 a 5 no hacen nada ni sacan rand(); el 1, el 2
// y el 4 cortan con la dirección de su hito (el 5, en test_hero_arrival_skill.cpp).
TEST_F(HeroAttackTest, TheOtherArrivalCasesCutWithTheirAddress)
{
    for (const std::uint8_t type : {std::uint8_t{0}, std::uint8_t{6}, std::uint8_t{0xff}})
    {
        Hero().movementType = type;
        ASSERT_TRUE(Client::Scenes::InteractOnArrival(Hero(), Arrival()).HasValue());
    }
    EXPECT_EQ(m_random.State(), After(0));
    struct Case
    {
        std::uint8_t type;
        std::string_view address;
    };
    for (const Case probe : {Case{1, "0x566755"}, Case{2, "0x5674c8"}, Case{4, "0x5695b4"}})
    {
        Hero().movementType = probe.type;
        const auto cut = Client::Scenes::InteractOnArrival(Hero(), Arrival());
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find(probe.address), std::string::npos) << cut.Error();
    }
    EXPECT_TRUE(m_sent.empty());
}

// 0x57f2d1..0x57f2f9: |GetTickCount() − [0x576791c]| de 300 o menos no pasa; si pasa, anota un GetTickCount nuevo.
TEST(ActionBrakeTest, ThreeHundredMillisecondsOrLessDoNotPass)
{
    std::vector<std::uint32_t> ticks;
    std::size_t calls = 0;
    const std::function<std::uint32_t()> tickCount = [&]() { return ticks[calls++]; };

    std::uint32_t last = 1000;
    ticks = {1300};
    EXPECT_FALSE(Client::Scenes::PassesTheActionBrake(last, 0, tickCount));
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(last, 1000u);

    calls = 0;
    ticks = {1301, 1305};
    EXPECT_TRUE(Client::Scenes::PassesTheActionBrake(last, 0, tickCount));
    EXPECT_EQ(calls, 2u);
    EXPECT_EQ(last, 1305u);
}

// La resta es de 32 bits con signo y se toma su valor absoluto (CDQ/XOR/SUB): un reloj que vuelve atrás más de 300
// pasa, y uno que dio la vuelta por poco no.
TEST(ActionBrakeTest, TheDifferenceIsSignedAndAbsolute)
{
    std::uint32_t now = 0;
    const std::function<std::uint32_t()> tickCount = [&]() { return now; };

    std::uint32_t last = 2000;
    now = 1000;
    EXPECT_TRUE(Client::Scenes::PassesTheActionBrake(last, 0, tickCount));
    EXPECT_EQ(last, 1000u);

    last = 1200;
    now = 1000;
    EXPECT_FALSE(Client::Scenes::PassesTheActionBrake(last, 0, tickCount));

    last = 0xffffff00u;
    now = 0x10;
    EXPECT_FALSE(Client::Scenes::PassesTheActionBrake(last, 0, tickCount));
}

// Con la skill 0x28 no hay freno: anota el reloj con una sola llamada.
TEST(ActionBrakeTest, TheSkill28SkipsTheBrake)
{
    std::size_t calls = 0;
    const std::function<std::uint32_t()> tickCount = [&]() { return static_cast<std::uint32_t>(1001 + calls++); };
    std::uint32_t last = 1000;
    EXPECT_TRUE(Client::Scenes::PassesTheActionBrake(last, 0x28, tickCount));
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(last, 1001u);
}

// 0x558bb0: con un ítem en el cursor o sin elegido, no; en Chaos Castle, cualquiera que no sea el héroe.
TEST_F(HeroAttackTest, NothingIsAttackableWithAnItemOnTheCursorOrWithoutAChosen)
{
    EXPECT_FALSE(Client::Scenes::IsAttackable(*m_world, true).Value());
    Chosen(TargetIndex);
    Runtime().cursorItemCount = 1;
    EXPECT_FALSE(Client::Scenes::IsAttackable(*m_world, true).Value());
    Runtime().cursorItemCount = -1;
    EXPECT_TRUE(Client::Scenes::IsAttackable(*m_world, false).Value());

    Runtime().selectedTargets[2] = 401;
    EXPECT_FALSE(Client::Scenes::IsAttackable(*m_world, false).HasValue());
}

TEST_F(HeroAttackTest, InChaosCastleEveryoneButTheHeroIsAttackable)
{
    for (const std::int32_t map : {18, 23})
    {
        Runtime().map = map;
        ActorState& other = Chosen(20);
        other.byte88 = 0;
        EXPECT_TRUE(Client::Scenes::IsAttackable(*m_world, false).Value());
        Chosen(HeroIndex);
        EXPECT_FALSE(Client::Scenes::IsAttackable(*m_world, true).Value());
    }
}

// Un monstruo se puede atacar salvo en la guerra de gremios (0x576796c) sin 0x576796d; un jugador, por el PK de 6 o
// más, +0x370 en 2 u 8 o con Ctrl abajo, y nunca el héroe.
TEST_F(HeroAttackTest, MonstersAndPlayersFollowTheRules)
{
    Chosen(TargetIndex);
    for (const std::int32_t map : {0, 2})
    {
        Runtime().map = map;
        EXPECT_TRUE(Client::Scenes::IsAttackable(*m_world, false).Value());
    }
    Runtime().attackRules.byte576796c = 1;
    EXPECT_FALSE(Client::Scenes::IsAttackable(*m_world, false).Value());
    Runtime().attackRules.byte576796d = 1;
    EXPECT_TRUE(Client::Scenes::IsAttackable(*m_world, false).Value());
    Runtime().attackRules = {};

    ActorState& player = Chosen(20);
    player = Client::Tests::UnarmedPlayer();
    player.byte88 = 1;
    player.pk = 3;
    EXPECT_FALSE(Client::Scenes::IsAttackable(*m_world, false).Value());
    EXPECT_TRUE(Client::Scenes::IsAttackable(*m_world, true).Value());
    player.pk = 6;
    EXPECT_TRUE(Client::Scenes::IsAttackable(*m_world, false).Value());
    player.pk = 3;
    player.byte370 = 8;
    EXPECT_TRUE(Client::Scenes::IsAttackable(*m_world, false).Value());

    Chosen(HeroIndex);
    EXPECT_FALSE(Client::Scenes::IsAttackable(*m_world, true).Value());
}

// En la guerra de gremios cuenta +0x374 en 2; con el PK de 6 y gremio, comparar los nombres de 0x787aa55 es del hito 8.
TEST_F(HeroAttackTest, TheGuildWarLooksAtTheEnemyMark)
{
    Runtime().attackRules.byte576796c = 1;
    ActorState& player = Chosen(20);
    player = Client::Tests::UnarmedPlayer();
    player.byte88 = 1;
    EXPECT_FALSE(Client::Scenes::IsAttackable(*m_world, true).Value());
    player.byte374 = 2;
    EXPECT_TRUE(Client::Scenes::IsAttackable(*m_world, false).Value());
    player.pk = 6;
    player.word1f2 = 3;
    const auto cut = Client::Scenes::IsAttackable(*m_world, false);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x558da4"), std::string::npos);
}

// En el duelo (0x78abad8), sólo el rival: la clave de 0x78abadc y el nombre de 0x78abae0.
TEST_F(HeroAttackTest, InADuelOnlyTheRivalIsAttackable)
{
    ActorState& player = Chosen(20);
    player = Client::Tests::UnarmedPlayer();
    player.byte88 = 1;
    player.key = 0x77;
    player.pk = 6;
    Name(player, "rival");
    Client::Scenes::AttackRules& rules = Runtime().attackRules;
    rules.duelActive = true;
    rules.duelKey = 0x77;
    for (std::size_t i = 0; i < 5; ++i)
        rules.duelName[i] = static_cast<std::byte>(std::string_view("rival")[i]);
    EXPECT_TRUE(Client::Scenes::IsAttackable(*m_world, false).Value());
    rules.duelKey = 0x78;
    EXPECT_FALSE(Client::Scenes::IsAttackable(*m_world, true).Value());
}

// 0x558c2a..0x558d6d: en el asedio, un atacante (+0x360 en 6 a 11) contra un defensor (12 o 13) decide por el escudo
// 0x4000 del elegido.
TEST_F(HeroAttackTest, InTheSiegeTheSidesDecideByTheShield)
{
    Runtime().siegeActive = true;
    Hero().byte360 = 9;
    ActorState& player = Chosen(20);
    player = Client::Tests::UnarmedPlayer();
    player.byte88 = 1;
    player.byte360 = 13;
    EXPECT_TRUE(Client::Scenes::IsAttackable(*m_world, false).Value());
    player.flags78 = 0x4000;
    EXPECT_FALSE(Client::Scenes::IsAttackable(*m_world, true).Value());

    // Del mismo lado no decide: sigue como sin asedio.
    player.flags78 = 0;
    player.byte360 = 9;
    EXPECT_FALSE(Client::Scenes::IsAttackable(*m_world, false).Value());
    EXPECT_TRUE(Client::Scenes::IsAttackable(*m_world, true).Value());
}
} // namespace
