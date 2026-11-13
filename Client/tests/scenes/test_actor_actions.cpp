#include "Scenes/ActorActions.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActionSend.h"
#include "Scenes/ActorState.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Client::Scenes::SetActorStop: 0x4f2250, la acción de parado (06, "Caminar"; P-C14 c). Los valores
// salen del ASM cotejado con el PE c32bbd5f; ninguno de una corrida del original.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::ActorStopContext;
using Client::Scenes::NoItemModel;
using Client::Scenes::SceneId;
using Client::Tests::AfterDraws;
using Client::Tests::FirstSeedWhere;
using Client::Tests::UnarmedPlayer;

// La primera semilla cuyo rand() % 16 no es 0: el sufijo saca uno y termina.
std::uint32_t QuietSeed()
{
    return FirstSeedWhere([](CrtRandom& random) { return random.Next() % 16 != 0; });
}

class ActorStopTest : public ::testing::Test
{
protected:
    // 0x4f2250 sobre `actor`, en el mapa y la escena de la prueba, con la semilla de m_random.
    Client::Core::Expected<Client::Scenes::ActorStopResult, std::string> Stop(ActorState& actor)
    {
        Client::Scenes::ActionSender send{};
        if (m_withSender)
            send = [this](std::uint8_t direction, std::uint8_t action) { m_sent.emplace_back(direction, action); };
        const ActorStopContext context{.runtime = m_runtime,
                                       .scene = m_scene,
                                       .playerSkeleton = m_skeleton,
                                       .items = m_withItems ? &m_items : nullptr,
                                       .random = m_random,
                                       .sendAction = send};
        return Client::Scenes::SetActorStop(actor, context);
    }

    // La acción que queda después de un 0x4f2250 que no corta.
    std::uint8_t StandOf(ActorState actor)
    {
        const auto stopped = Stop(actor);
        EXPECT_TRUE(stopped.HasValue()) << (stopped.HasValue() ? "" : stopped.Error());
        return actor.animation.action;
    }

    Client::Scenes::WorldRuntime m_runtime{.map = 0, .movementSpeed = {}};
    SceneId m_scene = SceneId::Game;
    Client::Render::Models::Skeleton m_skeleton = Client::Tests::PlayerActionsSkeleton();
    // Las armas de la matriz que llevan la marca +0x1e en Item.bmd.
    Client::Data::Tables::ItemTable m_items =
        Client::Tests::ItemsMarking({0x213, 0x227, 0x229, 0x231, 0x811, 0x815, 0xc12, 0xe11});
    bool m_withItems = true;
    CrtRandom m_random{QuietSeed()};
    // Los C1 18 que manda sentarse, con la dirección y la acción. Sin m_withSender, nadie los manda.
    bool m_withSender = true;
    std::vector<std::pair<std::uint8_t, std::uint8_t>> m_sent;
};

ActorState Armed(std::uint16_t right, std::uint16_t left = NoItemModel, std::uint8_t characterClass = 0)
{
    ActorState actor = UnarmedPlayer(characterClass);
    actor.hands[0].model = right;
    actor.hands[1].model = left;
    return actor;
}

// 0x4f2bf6..0x4f2c2c y 0x4f2d5b..0x4f2d72: sin nada en las manos, o en la zona segura de los mapas de
// menos de 11 o más de 23, la 1, y la 2 con la clase 2 (sin mirar el bit de la segunda clase).
TEST_F(ActorStopTest, SafeZoneOrNoWeaponStands1Or2ForTheElf)
{
    EXPECT_EQ(StandOf(UnarmedPlayer(0)), 1);
    EXPECT_EQ(StandOf(UnarmedPlayer(1)), 1);
    EXPECT_EQ(StandOf(UnarmedPlayer(2)), 2);
    EXPECT_EQ(StandOf(UnarmedPlayer(2 | 8)), 2);

    ActorState armedSafe = Armed(0x212);
    armedSafe.safeZone = 0x80;
    for (const std::int32_t map : {0, 10, 30})
    {
        SCOPED_TRACE(map);
        m_runtime.map = map;
        EXPECT_EQ(StandOf(armedSafe), 1);
        armedSafe.characterClass = 2;
        EXPECT_EQ(StandOf(armedSafe), 2);
        armedSafe.characterClass = 0;
    }
    // De 11 a 23, la zona segura no cambia nada: elige el arma (0x4f2c16..0x4f2c1e).
    for (const std::int32_t map : {11, 23})
    {
        SCOPED_TRACE(map);
        m_runtime.map = map;
        EXPECT_EQ(StandOf(armedSafe), 3);
    }
}

// Run en 0 al entrar (0x4f227f), y la acción por 0x4ea030: con la misma no toca el frame.
TEST_F(ActorStopTest, RunGoesToZeroAndTheActionGoesThroughSetAction)
{
    ActorState walking = UnarmedPlayer();
    walking.run = 40;
    walking.animation.action = 0x16;
    walking.animation.frame = 3.5f;
    ASSERT_TRUE(Stop(walking).HasValue());
    EXPECT_EQ(walking.run, 0);
    EXPECT_EQ(walking.animation.action, 1);
    EXPECT_EQ(walking.animation.previousAction, 0x16);
    EXPECT_EQ(walking.animation.previousKey, 3);
    EXPECT_EQ(walking.animation.frame, 0.0f);

    ActorState standing = UnarmedPlayer();
    standing.animation.action = 1;
    standing.animation.frame = 2.25f;
    ASSERT_TRUE(Stop(standing).HasValue());
    EXPECT_EQ(standing.animation.frame, 2.25f);
}

// 0x4f2c31..0x4f2d82, con los modelos como WORD con signo: los grupos 0 a 2 por la marca +0x1e del
// registro, 0x813 y 0x814, los grupos 3 y 5, la izquierda del grupo 4 y la derecha del grupo 4.
TEST_F(ActorStopTest, WeaponMatrix)
{
    struct Case
    {
        std::uint16_t right;
        std::uint16_t left;
        std::uint8_t stand;
    };
    const std::vector<Case> cases = {
        {0x212, NoItemModel, 3},    {0x213, NoItemModel, 4},    {0x811, NoItemModel, 4}, {0x227, NoItemModel, 0x8a},
        {0x229, NoItemModel, 0x8a}, {0x231, NoItemModel, 0x8a}, {0x228, NoItemModel, 3}, {0x812, NoItemModel, 3},
        {0x815, NoItemModel, 6},    {0x813, NoItemModel, 5},    {0x814, NoItemModel, 5}, {0xc12, NoItemModel, 6},
        {0xe11, NoItemModel, 6},    {0xc13, NoItemModel, 3},    {NoItemModel, 0xa12, 7}, {NoItemModel, 0xa18, 7},
        {NoItemModel, 0xa23, 7},    {NoItemModel, 0xa26, 7},    {NoItemModel, 0xa27, 7}, {NoItemModel, 0xa19, 1},
        {0xa1a, NoItemModel, 8},    {0xa20, NoItemModel, 8},    {0xa22, NoItemModel, 8}, {0xa24, NoItemModel, 8},
        {0xc11, NoItemModel, 8},    {0xa21, NoItemModel, 1},    {0xa23, NoItemModel, 1}, {0xa1a, 0xa12, 7},
        {0x213, 0xa12, 4},          {0xe12, NoItemModel, 1},    {NoItemModel, 0xe12, 1}, {0x1c17, NoItemModel, 1},
    };
    for (const Case& weapon : cases)
    {
        SCOPED_TRACE(testing::Message() << "derecha " << weapon.right << ", izquierda " << weapon.left);
        EXPECT_EQ(StandOf(Armed(weapon.right, weapon.left)), weapon.stand);
    }
    EXPECT_EQ(StandOf(Armed(0xa1a, NoItemModel, 2)), 8);
    EXPECT_EQ(StandOf(Armed(NoItemModel, 0xe12, 2)), 2);
}

// La marca sale de *[0x7758550]: sin Item.bmd no se inventa. 0x813 y 0x814 y el grupo 4 no la leen.
TEST_F(ActorStopTest, WithoutItemBmdTheMarkIsAnError)
{
    m_withItems = false;
    ActorState sword = Armed(0x212);
    const auto stopped = Stop(sword);
    ASSERT_FALSE(stopped.HasValue());
    EXPECT_NE(stopped.Error().find("+0x1e"), std::string::npos);

    EXPECT_EQ(StandOf(Armed(0x813)), 5);
    EXPECT_EQ(StandOf(Armed(0xa1a)), 8);
    EXPECT_EQ(StandOf(Armed(NoItemModel, 0xa12)), 7);
}

// 0x4f22fb..0x4f230f: el caballo oscuro fuera de la zona segura, con armas o con alas, es la 0x4a. En
// la zona segura sigue de largo.
TEST_F(ActorStopTest, DarkHorseOutsideIs4a)
{
    ActorState rider = Armed(0x212, 0xa12);
    rider.pet.model = 0x1c16;
    rider.wings.model = 0x1a12;
    EXPECT_EQ(StandOf(rider), 0x4a);

    ActorState inTown = UnarmedPlayer();
    inTown.pet.model = 0x1c16;
    inTown.safeZone = 1;
    EXPECT_EQ(StandOf(inTown), 1);
}

// 0x4f2314..0x4f2360: el cuervo en la izquierda en la zona segura es la 0x48; 0x1c14 y 0x1c15 fuera de
// ella, la 0xb sin armas y la 0xc con alguna.
TEST_F(ActorStopTest, TheRavenInTheSafeZoneIs48AndRidingIsBOrC)
{
    ActorState raven = Armed(0x212, 0x1c17);
    raven.safeZone = 1;
    EXPECT_EQ(StandOf(raven), 0x48);
    raven.safeZone = 0;
    EXPECT_EQ(StandOf(raven), 3);

    for (const std::uint16_t pet : {std::uint16_t{0x1c14}, std::uint16_t{0x1c15}})
    {
        SCOPED_TRACE(pet);
        ActorState rider = UnarmedPlayer();
        rider.pet.model = pet;
        EXPECT_EQ(StandOf(rider), 0xb);
        rider.hands[1].model = 0xa12;
        EXPECT_EQ(StandOf(rider), 0xc);
        rider.safeZone = 1;
        EXPECT_EQ(StandOf(rider), 1);
    }
}

// 0x4f228c..0x4f22f6: Fenrir fuera de la zona segura pone en 0 el latch 0x7d3a0d0 y elige por las
// manos, antes que el caballo, las alas o sentarse.
TEST_F(ActorStopTest, FenrirIs76To79)
{
    struct Case
    {
        std::uint16_t right;
        std::uint16_t left;
        std::uint8_t stand;
    };
    const std::vector<Case> cases = {
        {NoItemModel, NoItemModel, 0x76}, {0x212, 0xa12, 0x77}, {0x212, NoItemModel, 0x78}, {NoItemModel, 0xa12, 0x79}};
    for (const Case& hands : cases)
    {
        SCOPED_TRACE(hands.stand);
        m_runtime.movementSpeed = {.itemLatch = 7, .itemCounterBits = 9};
        ActorState rider = Armed(hands.right, hands.left);
        rider.pet.model = 0x1c37;
        rider.wings.model = 0x1a12;
        rider.flags7c = 0x200000;
        EXPECT_EQ(StandOf(rider), hands.stand);
        EXPECT_EQ(m_runtime.movementSpeed.itemLatch, 0u);
        EXPECT_EQ(m_runtime.movementSpeed.itemCounterBits, 9u);
    }

    m_runtime.movementSpeed.itemLatch = 7;
    ActorState inTown = UnarmedPlayer();
    inTown.pet.model = 0x1c37;
    inTown.safeZone = 1;
    EXPECT_EQ(StandOf(inTown), 1);
    EXPECT_EQ(m_runtime.movementSpeed.itemLatch, 7u);
}

// 0x4f2365..0x4f27fc: vuela con alas fuera de la zona segura o con +0x376 en 0x101; la 10 con la
// derecha del grupo 4 y si no la 9.
TEST_F(ActorStopTest, WingsOutsideOrWord376FlyAs9Or10)
{
    ActorState winged = Armed(0x212);
    winged.wings.model = 0x1a12;
    EXPECT_EQ(StandOf(winged), 9);
    winged.hands[0].model = 0xa1a;
    EXPECT_EQ(StandOf(winged), 10);
    winged.safeZone = 1;
    EXPECT_EQ(StandOf(winged), 1);

    ActorState word376 = UnarmedPlayer();
    word376.safeZone = 1;
    word376.word376 = 0x101;
    EXPECT_EQ(StandOf(word376), 9);
}

// 0x4f2376..0x4f23ce: en los mapas 7, 24 a 29 y 36 de la escena 5 decide la celda del TerrainWall vivo,
// que MuRet no tiene en el juego. Fuera de la escena 5 no la mira.
TEST_F(ActorStopTest, FlyingMapsOfTheGameSceneAreAFrontier)
{
    for (const std::int32_t map : {7, 24, 29, 36})
    {
        SCOPED_TRACE(map);
        m_runtime.map = map;
        m_scene = SceneId::Game;
        ActorState hero = UnarmedPlayer();
        const auto stopped = Stop(hero);
        ASSERT_FALSE(stopped.HasValue());
        EXPECT_NE(stopped.Error().find("0x7d0d348"), std::string::npos);
        EXPECT_EQ(hero.animation.action, 0);

        m_scene = SceneId::CharacterSelect;
        EXPECT_EQ(StandOf(UnarmedPlayer()), 1);
    }
    m_scene = SceneId::Game;
    for (const std::int32_t map : {6, 8, 23, 30, 35, 37})
    {
        SCOPED_TRACE(map);
        m_runtime.map = map;
        EXPECT_EQ(StandOf(UnarmedPlayer()), 1);
    }
}

// 0x4f23e3..0x4f2bea y 0x4f2801..0x4f2bea: con +0x7c & 0x200000, ya sentado no cambia nada; si no, pone la 0xca y
// manda el C1 05 18 con la dirección de su propio ángulo (+0x24) y 0x82, por las dos copias (volando y por el piso),
// y sigue al sufijo, que saca su rand().
TEST_F(ActorStopTest, SittingSendsTheActionWithItsOwnAngle)
{
    using Sent = std::vector<std::pair<std::uint8_t, std::uint8_t>>;
    const std::uint32_t quiet = QuietSeed();
    ActorState sitting = Armed(0x212);
    sitting.flags7c = 0x200000;
    sitting.angles.z = 100.0f;
    const auto stopped = Stop(sitting);
    ASSERT_TRUE(stopped.HasValue()) << stopped.Error();
    EXPECT_EQ(sitting.animation.action, 0xca);
    EXPECT_EQ(m_sent, (Sent{{3, 0x82}}));
    EXPECT_EQ(m_random.State(), AfterDraws(quiet, 1).State());

    m_sent.clear();
    ActorState winged = UnarmedPlayer();
    winged.wings.model = 0x1a12;
    winged.flags7c = 0x200000;
    winged.angles.z = -30.0f;
    EXPECT_EQ(StandOf(winged), 0xca);
    EXPECT_EQ(m_sent, (Sent{{0, 0x82}}));

    m_sent.clear();
    ActorState seated = UnarmedPlayer();
    seated.flags7c = 0x200000;
    seated.animation.action = 0xca;
    seated.animation.frame = 4.5f;
    EXPECT_EQ(StandOf(seated), 0xca);
    EXPECT_EQ(seated.animation.frame, 4.5f);
    EXPECT_TRUE(m_sent.empty());
}

// Sin quien mande el C1 18 (una llamada que todavía no lo ata), sentarse corta con su dirección después de la 0xca.
TEST_F(ActorStopTest, SittingWithoutASenderIsAnExplicitError)
{
    m_withSender = false;
    ActorState sitting = Armed(0x212);
    sitting.flags7c = 0x200000;
    const auto stopped = Stop(sitting);
    ASSERT_FALSE(stopped.HasValue());
    EXPECT_NE(stopped.Error().find("0x4f2416"), std::string::npos);
    EXPECT_EQ(sitting.animation.action, 0xca);
}

// 0x4f36be..0x4f37a5: un rand() siempre; con rand() % 16 en 0 y Fenrir, otro % 3 y, si da 0, el sonido
// 0x279 + rand() & 1. La mascota decide, no la acción.
TEST_F(ActorStopTest, TheSuffixDrawsOneRandAndFenrirMaySound)
{
    const std::uint32_t quiet = QuietSeed();
    ActorState plain = UnarmedPlayer();
    const auto calm = Stop(plain);
    ASSERT_TRUE(calm.HasValue());
    EXPECT_EQ(calm.Value().sound, std::nullopt);
    EXPECT_EQ(m_random.State(), AfterDraws(quiet, 1).State());

    const std::uint32_t lucky = FirstSeedWhere([](CrtRandom& random) { return random.Next() % 16 == 0; });
    m_random = CrtRandom(lucky);
    const auto noFenrir = Stop(plain);
    ASSERT_TRUE(noFenrir.HasValue());
    EXPECT_EQ(noFenrir.Value().sound, std::nullopt);
    EXPECT_EQ(m_random.State(), AfterDraws(lucky, 1).State());

    ActorState fenrir = UnarmedPlayer();
    fenrir.pet.model = 0x1c37;
    fenrir.safeZone = 1;
    const std::uint32_t silent =
        FirstSeedWhere([](CrtRandom& random) { return random.Next() % 16 == 0 && random.Next() % 3 != 0; });
    m_random = CrtRandom(silent);
    const auto withoutSound = Stop(fenrir);
    ASSERT_TRUE(withoutSound.HasValue());
    EXPECT_EQ(withoutSound.Value().sound, std::nullopt);
    EXPECT_EQ(m_random.State(), AfterDraws(silent, 2).State());

    const std::uint32_t loud =
        FirstSeedWhere([](CrtRandom& random) { return random.Next() % 16 == 0 && random.Next() % 3 == 0; });
    CrtRandom expected = AfterDraws(loud, 2);
    const int variant = expected.Next() % 2;
    m_random = CrtRandom(loud);
    const auto withSound = Stop(fenrir);
    ASSERT_TRUE(withSound.HasValue());
    EXPECT_EQ(withSound.Value().sound, 0x279 + variant);
    EXPECT_EQ(m_random.State(), expected.State());
}

// 0x4f36e2..0x4f36f3: un jugador de subtipo 209 a 211 va a la tabla de sonidos de los modelos
// (0x5769ad8), sin portar: error, solo en el camino del rand() % 16 en 0.
TEST_F(ActorStopTest, TheModelSoundSubtypesAreAFrontierOnlyWhenTheSuffixReachesThem)
{
    const std::uint32_t lucky = FirstSeedWhere([](CrtRandom& random) { return random.Next() % 16 == 0; });
    for (const std::int32_t subtype : {209, 210, 211})
    {
        SCOPED_TRACE(subtype);
        ActorState player = UnarmedPlayer();
        player.subtype = subtype;
        m_random = CrtRandom(QuietSeed());
        EXPECT_TRUE(Stop(player).HasValue());
        m_random = CrtRandom(lucky);
        const auto stopped = Stop(player);
        ASSERT_FALSE(stopped.HasValue());
        EXPECT_NE(stopped.Error().find("0x5769ad8"), std::string::npos);
    }
    for (const std::int32_t subtype : {208, 212})
    {
        SCOPED_TRACE(subtype);
        ActorState player = UnarmedPlayer();
        player.subtype = subtype;
        m_random = CrtRandom(lucky);
        EXPECT_TRUE(Stop(player).HasValue());
    }
}

// 0x4f2286 → 0x4f2d87: el que no es jugador lee sus celdas protegidas y el TerrainWall: error, con Run
// ya en 0 y sin acción nueva.
TEST_F(ActorStopTest, ANonPlayerIsAFrontierAfterRunGoesToZero)
{
    ActorState monster = UnarmedPlayer();
    monster.type = 0x172;
    monster.run = 40;
    monster.animation.action = 2;
    const auto stopped = Stop(monster);
    ASSERT_FALSE(stopped.HasValue());
    EXPECT_EQ(monster.run, 0);
    EXPECT_EQ(monster.animation.action, 2);
    EXPECT_EQ(m_random.State(), CrtRandom(QuietSeed()).State());
}
} // namespace
