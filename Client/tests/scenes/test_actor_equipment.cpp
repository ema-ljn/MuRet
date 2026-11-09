#include "Scenes/ActorEquipment.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorState.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "World/ItemState.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Client::Scenes::ApplyActorEquipment: 0x512a30, con 0x550d10 hasta 0x550990 (06, "El
// equipo propio en el personaje" y "Los stats del registro propio"; 04, "El F3 10"). Los valores salen del
// ASM cotejado con el PE c32bbd5f; ninguno de una corrida.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorItemPart;
using Client::Scenes::ActorState;
using Client::Scenes::NoItemModel;
using Client::Tests::AfterDraws;
using Client::Tests::FirstSeedWhere;

// La primera semilla cuyo rand() % 16 no es 0: el sufijo de 0x4f2250 saca uno y termina.
std::uint32_t QuietSeed()
{
    return FirstSeedWhere([](CrtRandom& random) { return random.Next() % 16 != 0; });
}

// Lo que no escribe 0x512a30 queda con esto.
constexpr std::uint8_t Untouched = 0x5a;
constexpr std::uint16_t UntouchedModel = 0x5a5a;

ActorItemPart UntouchedPart()
{
    return {.model = UntouchedModel, .level = Untouched, .flags1b = Untouched, .flags1c = Untouched};
}

// Un jugador con todas sus partes, la cabeza y la escala en Untouched, quieto en la acción 0x22: así
// 0x512a30 no llama a 0x4f2250 salvo que la prueba lo pida.
ActorState DressedInUntouched(std::uint8_t characterClass)
{
    ActorState actor = Client::Tests::UnarmedPlayer(characterClass);
    actor.body.fill(UntouchedPart());
    actor.hands.fill(UntouchedPart());
    actor.wings = UntouchedPart();
    actor.pet = UntouchedPart();
    actor.head = UntouchedModel;
    actor.scale = 7.0f;
    actor.animation.action = 0x22;
    actor.run = 40;
    return actor;
}

class ActorEquipmentTest : public ::testing::Test
{
protected:
    ActorEquipmentTest()
    {
        // Un tipo, un B1 y dos bytes distintos por lugar. Los lugares 9 a 11 también: 0x512a30 no los lee.
        for (std::size_t slot = 0; slot < m_own.equipment.size(); ++slot)
        {
            Client::World::ItemState& item = m_own.equipment[slot];
            item.id = static_cast<std::uint16_t>(0x10 * (slot + 1));
            item.scalar1Low = static_cast<std::uint32_t>((slot + 1) << 3) | 0x87u;
            item.flags1b = static_cast<std::uint8_t>(0xb0 + slot);
            item.flags1c = static_cast<std::uint8_t>(0xc0 + slot);
        }
    }

    Client::Core::Expected<Client::Scenes::ActorStopResult, std::string> Apply(ActorState& actor)
    {
        const Client::Scenes::ActorEquipmentContext context{.own = m_own,
                                                            .ownRecordAccesses = m_accesses,
                                                            .publishedHero = m_hero,
                                                            .stop = {.runtime = m_runtime,
                                                                     .scene = Client::Scenes::SceneId::Game,
                                                                     .playerSkeleton = m_skeleton,
                                                                     .items = &m_items,
                                                                     .random = m_random},
                                                            .itemSetTables = &m_sets,
                                                            .itemSets = &m_itemSets};
        return Client::Scenes::ApplyActorEquipment(actor, context);
    }

    Client::Scenes::OwnCharacterRecord m_own;
    std::uint8_t m_accesses = 0;
    ActorState m_hero;
    Client::Scenes::WorldRuntime m_runtime{.map = 0, .movementSpeed = {}};
    Client::Render::Models::Skeleton m_skeleton = Client::Tests::PlayerActionsSkeleton();
    Client::Data::Tables::ItemTable m_items = Client::Tests::ItemsMarking({});
    // Las tablas de los conjuntos sin ningún conjunto: 0x40e1b0 recorre el equipo y no aplica nada.
    Client::Data::Tables::ItemSetTables m_sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize));
    Client::Scenes::ItemSetState m_itemSets;
    CrtRandom m_random{QuietSeed()};
};

// 0x512abf..0x512b9b y 0x512cc7..0x512ec9: el modelo es el tipo + 0x212, el nivel (B1 >> 3) & 0xf, y
// los bytes +0x1b y +0x1c del ítem van tal cual. Las manos llevan el +0x1b, las alas y la mascota ningún
// byte, y el cuerpo los dos. Los lugares 9 a 11 no se leen.
TEST_F(ActorEquipmentTest, ProjectsTheNineSlotsItReads)
{
    ActorState actor = DressedInUntouched(1);
    const auto applied = Apply(actor);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x550990"), std::string::npos);

    const auto expectedModel = [this](std::size_t slot)
    { return static_cast<std::uint16_t>(m_own.equipment[slot].id + 0x212); };
    const auto expectedLevel = [](std::size_t slot) { return static_cast<std::uint8_t>((slot + 1) & 0xf); };
    for (std::size_t side = 0; side < 2; ++side)
    {
        SCOPED_TRACE(side);
        EXPECT_EQ(actor.hands[side].model, expectedModel(side));
        EXPECT_EQ(actor.hands[side].level, expectedLevel(side));
        EXPECT_EQ(actor.hands[side].flags1b, static_cast<std::uint8_t>(0xb0 + side));
        EXPECT_EQ(actor.hands[side].flags1c, Untouched);
    }
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        SCOPED_TRACE(part);
        const std::size_t slot = 2 + part;
        EXPECT_EQ(actor.body[part].model, expectedModel(slot));
        EXPECT_EQ(actor.body[part].level, expectedLevel(slot));
        EXPECT_EQ(actor.body[part].flags1b, static_cast<std::uint8_t>(0xb0 + slot));
        EXPECT_EQ(actor.body[part].flags1c, static_cast<std::uint8_t>(0xc0 + slot));
    }
    EXPECT_EQ(actor.wings.model, expectedModel(7));
    EXPECT_EQ(actor.wings.level, expectedLevel(7));
    EXPECT_EQ(actor.wings.flags1b, Untouched);
    EXPECT_EQ(actor.wings.flags1c, Untouched);
    EXPECT_EQ(actor.pet.model, expectedModel(8));
    EXPECT_EQ(actor.pet.level, expectedLevel(8));
    EXPECT_EQ(actor.pet.flags1b, Untouched);
    EXPECT_EQ(actor.pet.flags1c, Untouched);
}

// Un tipo −1 deja 0xffff en las manos, las alas y la mascota, y la parte de la clase en el cuerpo:
// 0x2212, 0x221a, 0x2222, 0x222a y 0x2232 más (clase >> 3) · 5 + (clase & 7). El nivel se copia igual.
TEST_F(ActorEquipmentTest, EmptySlotsLeaveNoItemOrTheClassPart)
{
    for (Client::World::ItemState& item : m_own.equipment)
    {
        item.id = 0xffff;
        item.scalar1Low = 0x78;
    }
    ActorState actor = DressedInUntouched(0x0a);
    EXPECT_FALSE(Apply(actor).HasValue());

    EXPECT_EQ(actor.hands[0].model, NoItemModel);
    EXPECT_EQ(actor.hands[1].model, NoItemModel);
    EXPECT_EQ(actor.wings.model, NoItemModel);
    EXPECT_EQ(actor.pet.model, NoItemModel);
    EXPECT_EQ(actor.hands[0].level, 15);
    const std::uint16_t classOffset = 1 * 5 + 2;
    const std::array<std::uint16_t, 5> classParts = {0x2212, 0x221a, 0x2222, 0x222a, 0x2232};
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        SCOPED_TRACE(part);
        EXPECT_EQ(actor.body[part].model, classParts[part] + classOffset);
        EXPECT_EQ(actor.body[part].level, 15);
    }
    // 0x512930: la parte de la clase no es un casco abierto.
    EXPECT_EQ(actor.head, NoItemModel);
}

// 0x512a3c..0x512a42: con otro tipo sale sin abrir el registro ni tocar al personaje.
TEST_F(ActorEquipmentTest, ANonPlayerIsLeftAsIs)
{
    ActorState monster = DressedInUntouched(0);
    monster.type = 0x172;
    const auto applied = Apply(monster);
    ASSERT_TRUE(applied.HasValue());
    EXPECT_EQ(monster.hands[0].model, UntouchedModel);
    EXPECT_EQ(monster.body[0].model, UntouchedModel);
    EXPECT_EQ(monster.run, 40);
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), CrtRandom(QuietSeed()).State());
}

// +0x3e3 distinto de 0: después de las manos y de 0x4f2250 cierra el registro (0x512bea..0x512cc6) y no
// toca la ropa, la cabeza ni la escala. El cierre que deja la cuenta en 0 saca 3.848 rand(), después
// del de 0x4f2250; si quien llama lo tiene abierto, ninguno.
TEST_F(ActorEquipmentTest, WithByte3e3TheClothesStayAndTheRecordCloses)
{
    const std::uint32_t seed = QuietSeed();
    ActorState actor = DressedInUntouched(0);
    actor.byte3e3 = 1;
    actor.animation.action = 0;
    const auto applied = Apply(actor);
    ASSERT_TRUE(applied.HasValue());
    EXPECT_EQ(applied.Value().sound, std::nullopt);
    EXPECT_EQ(actor.hands[0].model, m_own.equipment[0].id + 0x212);
    EXPECT_EQ(actor.body[0].model, UntouchedModel);
    EXPECT_EQ(actor.head, UntouchedModel);
    EXPECT_EQ(actor.scale, 7.0f);
    EXPECT_EQ(actor.run, 0);
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), AfterDraws(seed, 1 + Client::Render::Models::OwnRecordCloseRandoms).State());

    m_random = CrtRandom(seed);
    m_accesses = 1;
    ASSERT_TRUE(Apply(actor).HasValue());
    EXPECT_EQ(m_accesses, 1);
    EXPECT_EQ(m_random.State(), AfterDraws(seed, 1).State());
}

// 0x512ba1..0x512bd4: llama a 0x4f2250 con las manos ya puestas, salvo en los mapas 18 a 23, con las
// acciones 0xc5 a 0xcc o con las de 0x22 a 0x97.
TEST_F(ActorEquipmentTest, TheStopRunsInTheMiddleUnlessBusyOrInMaps18To23)
{
    m_own.equipment[0].id = 0;
    m_own.equipment[1].id = 0xffff;
    const auto stopsWith = [this](std::uint8_t action, std::int32_t map)
    {
        m_runtime.map = map;
        ActorState actor = DressedInUntouched(0);
        actor.byte3e3 = 1;
        actor.animation.action = action;
        EXPECT_TRUE(Apply(actor).HasValue());
        return actor.run == 0;
    };
    for (const std::uint8_t action : std::array<std::uint8_t, 6>{0x00, 0x21, 0x98, 0xc4, 0xcd, 0xff})
    {
        SCOPED_TRACE(action);
        EXPECT_TRUE(stopsWith(action, 0));
        EXPECT_TRUE(stopsWith(action, 17));
    }
    for (const std::uint8_t action : std::array<std::uint8_t, 6>{0x22, 0x50, 0x97, 0xc5, 0xca, 0xcc})
    {
        SCOPED_TRACE(action);
        EXPECT_FALSE(stopsWith(action, 0));
    }
    for (const std::int32_t map : {18, 23})
    {
        SCOPED_TRACE(map);
        EXPECT_FALSE(stopsWith(0, map));
    }

    // 0x4f2250 ve lo que acaba de poner: con las alas nuevas vuela (la 9); sin ellas, la derecha nueva,
    // 0x212 sin la marca, elige la 3.
    m_runtime.map = 0;
    ActorState actor = DressedInUntouched(0);
    actor.byte3e3 = 1;
    actor.animation.action = 0;
    ASSERT_TRUE(Apply(actor).HasValue());
    EXPECT_EQ(actor.animation.action, 9);
    m_own.equipment[7].id = 0xffff;
    ASSERT_TRUE(Apply(actor).HasValue());
    EXPECT_EQ(actor.animation.action, 3);
}

// Un error de 0x4f2250 corta antes de la ropa, y el acceso al registro se cierra igual.
TEST_F(ActorEquipmentTest, AStopErrorStopsBeforeTheClothes)
{
    ActorState actor = DressedInUntouched(0);
    actor.animation.action = 0;
    actor.flags7c = 0x200000;
    const auto applied = Apply(actor);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("C1 .. 18"), std::string::npos);
    EXPECT_EQ(actor.body[0].model, UntouchedModel);
    EXPECT_EQ(m_accesses, 0);
}

// La ropa sigue con 0x4053f0, 0x512930, 0x505820 con el héroe publicado y 0x550d10: MuRet corta en la
// primera que no tiene, con lo de antes hecho. 0x550d10 corta en 0x550990, que lee lo de 0x505820.
TEST_F(ActorEquipmentTest, TheClothesReachAnExplicitFrontier)
{
    // 0x1012 es un casco abierto: 0x512930 pone la cabeza de la clase.
    m_own.equipment[2].id = 0x1012 - 0x212;

    ActorState someone = DressedInUntouched(2);
    someone.skin = 1;
    const auto stats = Apply(someone);
    ASSERT_FALSE(stats.HasValue());
    EXPECT_NE(stats.Error().find("0x550d10"), std::string::npos);
    EXPECT_NE(stats.Error().find("0x550990"), std::string::npos);
    EXPECT_EQ(someone.head, 0x2212 + 2);
    EXPECT_EQ(someone.scale, 0.86f);
    EXPECT_EQ(m_accesses, 0);

    ActorState& hero = m_hero;
    hero = DressedInUntouched(5);
    const auto setBonus = Apply(hero);
    ASSERT_FALSE(setBonus.HasValue());
    EXPECT_NE(setBonus.Error().find("0x505820"), std::string::npos);
    EXPECT_EQ(hero.head, 0x2212 + 5);
    // La clase 5 no tiene caso en las tablas de 0x512930: la escala queda.
    EXPECT_EQ(hero.scale, 7.0f);
}

// 0x550d10 (0x512ef5) corre después de la cabeza y antes del cierre, con el registro abierto por 0x512a30:
// 0x54ef00 pone en 0 los WORD de +0x2a a +0x36, y corta en 0x550990 antes de la cola, que no escribió
// nada. Los 33 accesos de 0x40e1b0 no lo dejan en 0: el único rand() es el del cierre de 0x512a30.
TEST_F(ActorEquipmentTest, TheStatsCutAt0x550990BeforeTheTail)
{
    const std::uint32_t seed = QuietSeed();
    m_own.word2a = 0x2a2a;
    m_own.word36 = 0x3636;
    m_own.wordef8 = 0xef8;
    m_own.bytef03 = 3;
    ActorState actor = DressedInUntouched(0);

    const auto applied = Apply(actor);

    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x550990"), std::string::npos);
    EXPECT_EQ(m_own.word2a, 0);
    EXPECT_EQ(m_own.word36, 0);
    EXPECT_EQ(m_own.wordef8, 0xef8);
    EXPECT_EQ(m_own.bytef03, 3);
    EXPECT_EQ(actor.head, NoItemModel);
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), AfterDraws(seed, Client::Render::Models::OwnRecordCloseRandoms).State());
}

// 0x4053f0 en los mapas 18 a 23, después de la ropa: 0x4053a0 deja sin ítem las manos, las alas y la
// mascota, con el nivel en 0 y el +0x1b de las manos en 0 (no el de las alas ni el de la mascota), y la
// clase (+0x1d4 & 7) elige el arma: 0xc17 a la derecha de la 0, 0x222 en las dos manos de la 1, la 3 y
// la 4, 0xa25 a la derecha de la 2 y nada en la 5 a la 7. Sigue con 0x512930 y 0x550d10.
TEST_F(ActorEquipmentTest, Maps18To23LeaveOnlyTheWeaponOfTheClass)
{
    struct Armed
    {
        std::uint16_t right;
        std::uint16_t left;
    };
    const std::array<Armed, 8> byClass = {Armed{0xc17, NoItemModel},
                                          Armed{0x222, 0x222},
                                          Armed{0xa25, NoItemModel},
                                          Armed{0x222, 0x222},
                                          Armed{0x222, 0x222},
                                          Armed{NoItemModel, NoItemModel},
                                          Armed{NoItemModel, NoItemModel},
                                          Armed{NoItemModel, NoItemModel}};
    m_own.equipment[8].id = 0x1c15 - 0x212;
    for (const std::int32_t map : {18, 23})
    {
        m_runtime.map = map;
        for (std::uint8_t characterClass = 0; characterClass < 16; ++characterClass)
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", clase " << int{characterClass});
            ActorState actor = DressedInUntouched(characterClass);
            actor.previousHad1c15 = true;
            const auto applied = Apply(actor);
            ASSERT_FALSE(applied.HasValue());
            EXPECT_NE(applied.Error().find("0x550990"), std::string::npos);

            const Armed& armed = byClass[static_cast<std::size_t>(characterClass & 7)];
            EXPECT_EQ(actor.hands[0].model, armed.right);
            EXPECT_EQ(actor.hands[1].model, armed.left);
            for (const ActorItemPart& hand : actor.hands)
            {
                EXPECT_EQ(hand.level, 0);
                EXPECT_EQ(hand.flags1b, 0);
                EXPECT_EQ(hand.flags1c, Untouched);
            }
            EXPECT_EQ(actor.wings.model, NoItemModel);
            EXPECT_EQ(actor.wings.level, 0);
            EXPECT_EQ(actor.wings.flags1b, Untouched);
            EXPECT_EQ(actor.pet.model, NoItemModel);
            EXPECT_EQ(actor.pet.level, 0);
            EXPECT_EQ(actor.pet.flags1b, Untouched);
            EXPECT_FALSE(actor.previousHad1c15);
            // La ropa de antes queda, y 0x512930 corre después.
            EXPECT_EQ(actor.body[0].model, m_own.equipment[2].id + 0x212);
            EXPECT_EQ(actor.head, NoItemModel);
        }
    }
}

// 0x512a30 escribe el word318 entero: lo que la selección guarda de él se pone al día.
TEST_F(ActorEquipmentTest, ThePetKeepsTheWord318HistoryInStep)
{
    ActorState actor = DressedInUntouched(0);
    actor.byte3e3 = 1;
    m_own.equipment[8].id = 0x1c15 - 0x212;
    ASSERT_TRUE(Apply(actor).HasValue());
    EXPECT_TRUE(actor.previousHad1c15);

    m_own.equipment[8].id = 0x1c16 - 0x212;
    ASSERT_TRUE(Apply(actor).HasValue());
    EXPECT_FALSE(actor.previousHad1c15);
}
} // namespace
