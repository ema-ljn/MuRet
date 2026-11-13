#include "Scenes/AttackStep.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/HeroAttack.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/TerrainLighting.h"

#include "support/PlayerActors.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Scenes::StepTheAttack: 0x4fdb9a..0x50539e con 0x4f97b0 y 0x4f5b70, y ChosenKeyOf (0x558eb0). 06, "El paso
// del ataque".
namespace
{
using Client::Render::Models::ActionInfo;
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::CharacterStepContext;
using Client::Scenes::ChosenKeyOf;
using Client::Scenes::JointState;
using Client::Scenes::StepCharacters;
using Client::Scenes::StepTheAttack;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 0x13579bdfu;
constexpr std::size_t HeroSlot = 42;
constexpr std::size_t OtherSlot = 3;
constexpr std::size_t VictimSlot = 7;
// Una lectura de +0x38e (0x517630 y sus copias): un rand() al cerrarse en 0.
constexpr int SkillRead = 1;
// El registro propio cerrado en 0: los 3.848 de 0x40b4d0.
constexpr int OwnRecord = Client::Render::Models::OwnRecordCloseRandoms;
// Un paso de +0x381 antes del frame 5 con blanco: la lectura de 0x4f97b0, el rand() de 0x4f5b97 y las dos lecturas
// de 0x4f811b y 0x4f8a2c.
constexpr int CountingStep = SkillRead + 1 + 2 * SkillRead;
// El paso que pega antes de la habilidad: lo mismo con el registro propio de 0x4f97b0 (el frame 5).
constexpr int StrikingStep = CountingStep + OwnRecord;
// La habilidad con un blanco sin daño ni +0x380 en 2: la cabeza, los dos switch, el sonido con el registro propio y la
// escritura de +0x38e.
constexpr int SkillWithTarget = SkillRead + SkillRead + SkillRead + 1 + OwnRecord + SkillRead;
// Sin blanco: la cabeza, la lectura de 0x502b12 y la escritura.
constexpr int SkillWithoutTarget = 3 * SkillRead;

int DrawsSince(std::uint32_t seed, const CrtRandom& random)
{
    constexpr int Limit = 1'000'000;
    CrtRandom probe(seed);
    int draws = 0;
    while (probe.State() != random.State() && draws < Limit)
    {
        static_cast<void>(probe.Next());
        ++draws;
    }
    return draws;
}

bool HasError(const Client::Core::Expected<void, std::string>& stepped, std::string_view address)
{
    return !stepped.HasValue() && stepped.Error().find(address) != std::string::npos;
}

std::string ErrorOf(const Client::Core::Expected<void, std::string>& stepped)
{
    return stepped.HasValue() ? std::string() : stepped.Error();
}

struct AttackWorld
{
    AttackWorld()
    {
        world.Runtime().map = 0;
        world.Runtime().hero = HeroSlot;
        world.Runtime().heroSlot = static_cast<std::int32_t>(HeroSlot);
        world.OwnCharacter().ClearEquipmentForEntry();
        // Los tiempos de 0x4fc450 lejos de 0: sus helpers leerían Item.bmd con un arma en el registro propio.
        constexpr std::uint16_t FarTimer = 1000;
        world.OwnCharacter().word3e = FarTimer;
        world.OwnCharacter().word40 = FarTimer;
        world.OwnCharacter().word42 = FarTimer;
        world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        Hero() = Player(1);
        Victim() = Player(2);
        Victim().alive = false;
    }

    static ActorState Player(std::uint8_t characterClass)
    {
        ActorState actor = Client::Tests::UnarmedPlayer(characterClass);
        actor.alive = true;
        actor.byte88 = 1;
        actor.pk = 3;
        actor.animation.action = 1;
        return actor;
    }

    ActorState& Hero()
    {
        return world.Actors()[HeroSlot];
    }
    ActorState& Victim()
    {
        return world.Actors()[VictimSlot];
    }

    // El que pega, en `slot`, con la acción `action` y +0x381 en 1, como lo deja el ataque.
    ActorState& Striker(std::size_t slot, std::uint8_t action, std::size_t target)
    {
        ActorState& striker = world.Actors()[slot];
        if (slot != HeroSlot)
            striker = Player(0);
        striker.animation.action = action;
        striker.byte381 = 1;
        striker.attackTarget = static_cast<std::uint16_t>(target);
        return striker;
    }

    // Un monstruo del alta (byte88 en 2) en `slot` con el tipo y el +0x376, en la 3 y con +0x381 en 1.
    ActorState& Monster(std::size_t slot, std::uint16_t type, std::uint16_t kind, std::size_t target)
    {
        ActorState& monster = world.Actors()[slot];
        monster = ActorState{};
        monster.alive = true;
        monster.type = type;
        monster.word376 = kind;
        monster.byte88 = 2;
        monster.animation.action = 3;
        monster.byte381 = 1;
        monster.attackTarget = static_cast<std::uint16_t>(target);
        return monster;
    }

    CharacterStepContext Context()
    {
        return CharacterStepContext{.world = world,
                                    .random = random,
                                    .playerSkeleton = &skeleton,
                                    .particles = withParticles ? &particles : nullptr,
                                    .joints = withJoints ? std::span<JointState>(joints) : std::span<JointState>{},
                                    .soundRequest = [this](std::int32_t sound) { sounds.push_back(sound); },
                                    .controlDown = controlDown};
    }

    Client::Core::Expected<void, std::string> Attack(ActorState& striker, std::size_t slot)
    {
        return StepTheAttack(striker, slot, Context());
    }

    std::unique_ptr<WorldState> ownedWorld = std::make_unique<WorldState>();
    WorldState& world = *ownedWorld;
    CrtRandom random{Seed};
    Client::Render::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    std::vector<std::int32_t> sounds;
    bool controlDown = false;
    Client::World::Particles particles;
    bool withParticles = true;
    // Un pool corto: los dos 0x618 del que mató son los únicos joints de estas pruebas.
    std::vector<JointState> joints = std::vector<JointState>(16);
    bool withJoints = true;
};

// Las partículas vivas de `type`.
std::vector<const Client::World::Particle*> AliveOf(const Client::World::Particles& particles, std::int32_t type)
{
    std::vector<const Client::World::Particle*> found;
    for (const Client::World::Particle& particle : particles.All())
    {
        if (particle.alive && particle.type == type)
            found.push_back(&particle);
    }
    return found;
}

// Antes del frame 5, cada paso lee +0x38e en 0x4f97b0, saca el rand() de 0x4f5b70 y lee +0x38e dos veces (el blanco
// está en el pool) y sube +0x381; el que lo deja en 15 corre la habilidad: o+0x8a en −1 (sin elegido), la cabeza, los
// dos switch, el sonido 0x46 + rand() & 3 con el registro propio abierto y +0x38e, +0x382 y +0x380 en 0. Después,
// nada.
TEST(AttackStepTest, TheCounterRunsFourteenStepsAndThenTheSkill)
{
    AttackWorld tested;
    ActorState& hero = tested.Striker(HeroSlot, 0x23, VictimSlot);
    hero.word382 = 18;
    hero.word8a = 5;
    CrtRandom expected(Seed);
    for (int step = 1; step < 14; ++step)
    {
        ASSERT_TRUE(tested.Attack(hero, HeroSlot).HasValue());
        EXPECT_EQ(hero.byte381, step + 1);
        expected.Discard(CountingStep);
        EXPECT_EQ(tested.random.State(), expected.State());
    }

    const auto struck = tested.Attack(hero, HeroSlot);

    ASSERT_TRUE(struck.HasValue()) << struck.Error();
    EXPECT_EQ(hero.byte381, 0);
    EXPECT_EQ(hero.word8a, 0xffff);
    EXPECT_EQ(hero.word382, 0);
    EXPECT_EQ(hero.byte380, 0);
    EXPECT_EQ(tested.world.Runtime().skillThreshold, 15);
    expected.Discard(CountingStep + SkillRead + SkillRead + SkillRead);
    const int drawn = expected.Next() & 3;
    expected.Discard(OwnRecord + SkillRead);
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x46 + drawn}));

    ASSERT_TRUE(tested.Attack(hero, HeroSlot).HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
}

// Pasado el frame 5 (0x6b87f8), 0x4f97b0 lee la izquierda del registro propio y pone +0x381 en 15: la habilidad corre
// en el mismo paso. La 0x3e del 0x20a termina desde el frame 1, sin el registro. Antes del 5, o con NaN, sigue.
TEST(AttackStepTest, TheStrikeFrameEndsTheCount)
{
    struct Case
    {
        std::string_view name;
        std::uint8_t action;
        float frame;
        int draws;
        bool struck;
    };
    for (const Case& probe : {Case{"0x23 en el frame 5", 0x23, 5.0f, StrikingStep + SkillWithTarget, true},
                              Case{"0x97 en el frame 7", 0x97, 7.0f, StrikingStep + SkillWithTarget, true},
                              Case{"0x23 en el frame 4,9", 0x23, 4.9f, CountingStep, false},
                              Case{"0x98 en el frame 5", 0x98, 5.0f, CountingStep, false},
                              Case{"0x3e en el frame 1", 0x3e, 1.0f, CountingStep + SkillWithTarget, true},
                              Case{"0x3e en el frame 0,9", 0x3e, 0.9f, CountingStep, false}})
    {
        SCOPED_TRACE(probe.name);
        AttackWorld tested;
        ActorState& hero = tested.Striker(HeroSlot, probe.action, VictimSlot);
        hero.animation.frame = probe.frame;

        const auto stepped = tested.Attack(hero, HeroSlot);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(hero.byte381, probe.struck ? 0 : 2);
        EXPECT_EQ(DrawsSince(Seed, tested.random), probe.draws);
        EXPECT_EQ(tested.sounds.size(), probe.struck ? 1u : 0u);
    }
}

// Un monstruo sin caso en 0x4f5b70 (el +0x376 3 en Lorencia) cuenta y pega como un jugador: los ayudantes de los
// mapas no lo miran, su +0x376 va a 0x4f80e0 y 0x4f97b0 lo deja pegar en la 3 o la 4 desde el frame 5. El sonido es el
// de las manos del registro propio (Q-C1168).
TEST(AttackStepTest, AnOrdinaryMonsterCountsAndStrikesLikeAPlayer)
{
    AttackWorld tested;
    ActorState& monster = tested.Monster(OtherSlot, 0x155, 3, HeroSlot);
    for (int step = 1; step < 4; ++step)
    {
        const auto counted = tested.Attack(monster, OtherSlot);
        ASSERT_TRUE(counted.HasValue()) << counted.Error();
        EXPECT_EQ(monster.byte381, step + 1);
    }
    EXPECT_EQ(DrawsSince(Seed, tested.random), 3 * CountingStep);

    monster.animation.frame = 5.0f;
    const auto struck = tested.Attack(monster, OtherSlot);

    ASSERT_TRUE(struck.HasValue()) << struck.Error();
    EXPECT_EQ(monster.byte381, 0);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 3 * CountingStep + StrikingStep + SkillWithTarget);
    EXPECT_EQ(tested.sounds.size(), 1u);

    // En la 5 no es un ataque de monstruo para 0x4f97b0: sigue contando.
    ActorState& other = tested.Monster(VictimSlot + 1, 0x155, 3, HeroSlot);
    other.animation.action = 5;
    other.animation.frame = 5.0f;
    ASSERT_TRUE(tested.Attack(other, VictimSlot + 1).HasValue());
    EXPECT_EQ(other.byte381, 2);
}

// Los ayudantes de 0x4f5b70, cada uno en sus mapas, después del rand() de 0x4f5b97: los que devuelven 1 salen sin la
// cola (0x4436f0, los de Kalima y los del 33 y el 54 fuera de su efecto); 0x42fce0 y 0x4324c0 miran el tipo y +0x381 y
// siguen; los efectos cortan. Los casos uno por uno, en test_monster_attack_cases.cpp.
TEST(AttackStepTest, TheMapHelpersOfTheMonstersExitOrCut)
{
    struct Case
    {
        std::string_view name;
        std::int32_t map;
        std::uint16_t type;
        std::uint16_t kind;
        std::uint8_t counter;
        std::string_view cut;
        int draws;
    };
    // La lectura de 0x4f97b0 y el rand() de 0x4f5b97.
    constexpr int Helper = SkillRead + 1;
    const std::array cases{Case{"Kalima 1", 0x18, 0x160, 0x91, 1, "", Helper + SkillRead},
                           Case{"Kalima 7", 0x24, 0x160, 0x113, 1, "", Helper + SkillRead},
                           Case{"Kalima fuera de Kalima", 0, 0x160, 0x91, 1, "", CountingStep},
                           Case{"el 31", 0x1f, 0x160, 0x123, 1, "", Helper},
                           Case{"el 31 con otro", 0x1f, 0x160, 0x126, 1, "", CountingStep},
                           Case{"el asedio", 0x1e, 0x160, 0x120, 1, "0x42dd48", Helper},
                           Case{"el 54", 0x36, 0x160, 0x134, 1, "", Helper},
                           Case{"el 35 en 14", 0x23, 0x1b2, 3, 14, "0x42fd37", Helper},
                           Case{"el 35 en 2", 0x23, 0x1b2, 3, 2, "", CountingStep},
                           Case{"Crywolf 0x17c", 0x22, 0x17c, 3, 1, "0x43259e", Helper},
                           Case{"Crywolf 0x1ad en 2", 0x22, 0x1ad, 3, 2, "", CountingStep},
                           Case{"un caso de +0x376", 0, 0x160, 0x4d, 1, "", CountingStep + SkillRead}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        AttackWorld tested;
        tested.world.Runtime().map = probe.map;
        ActorState& monster = tested.Monster(OtherSlot, probe.type, probe.kind, HeroSlot);
        monster.byte381 = probe.counter;

        const auto stepped = tested.Attack(monster, OtherSlot);

        if (probe.cut.empty())
        {
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(monster.byte381, probe.counter + 1);
        }
        else
        {
            EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        }
        EXPECT_EQ(DrawsSince(Seed, tested.random), probe.draws);
    }
}

// Sin blanco (+0x3a4 en −1), 0x4f5b70 sale después de su rand() (0x4f80ea) y la habilidad no suena.
TEST(AttackStepTest, WithoutATargetThereIsNoSound)
{
    AttackWorld tested;
    ActorState& hero = tested.Striker(HeroSlot, 0x22, 0xffff);
    hero.animation.frame = 6.0f;

    const auto stepped = tested.Attack(hero, HeroSlot);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawsSince(Seed, tested.random), SkillRead + OwnRecord + 1 + SkillWithoutTarget);
    EXPECT_TRUE(tested.sounds.empty());
    EXPECT_EQ(hero.byte381, 0);
}

// El sonido sale de las manos del registro propio, también cuando pega otro: 0x4b con las dos de 0x800 a 0x9ff y 0x46
// si no; el ítem del cursor, sacado de la grilla del inventario, toma el lugar de [0x78a6758].
TEST(AttackStepTest, TheStrikeSoundFollowsTheOwnHands)
{
    struct Case
    {
        std::string_view name;
        std::uint16_t right;
        std::uint16_t left;
        std::optional<std::uint16_t> cursor;
        std::int32_t slot;
        std::int32_t base;
    };
    for (const Case& probe : {Case{"sin armas", 0xffff, 0xffff, std::nullopt, 0, 0x46},
                              Case{"la ballesta y el arco", 0x808, 0x800, std::nullopt, 0, 0x4b},
                              Case{"la espada y el arco", 0x000, 0x800, std::nullopt, 0, 0x46},
                              Case{"el cursor en la derecha", 0x000, 0x800, std::uint16_t{0x808}, 0, 0x4b},
                              Case{"el cursor en la izquierda", 0x808, 0x000, std::uint16_t{0x9ff}, 1, 0x4b},
                              Case{"el cursor en otro lugar", 0x808, 0x000, std::uint16_t{0x9ff}, 2, 0x46}})
    {
        SCOPED_TRACE(probe.name);
        AttackWorld tested;
        auto& equipment = tested.world.OwnCharacter().equipment;
        equipment[0].id = probe.right;
        equipment[1].id = probe.left;
        if (probe.cursor)
        {
            auto& runtime = tested.world.Runtime();
            runtime.cursorItemCount = 1;
            runtime.cursorGrid = Client::Scenes::CursorGrid::Inventory;
            runtime.slot78a6758 = probe.slot;
            runtime.cursorItem.id = *probe.cursor;
        }
        ActorState& other = tested.Striker(OtherSlot, 0x23, HeroSlot);
        other.byte381 = 14;

        const auto stepped = tested.Attack(other, OtherSlot);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        CrtRandom expected(Seed);
        expected.Discard(CountingStep + 3 * SkillRead);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{probe.base + (expected.Next() & 3)}));
    }
}

// Lo que corta, en su orden y con los rand() de antes: las habilidades del hito 4 (0x4f9899, 0x4f8a39, 0x4fdeeb,
// 0x504372), los efectos de los casos de 0x4f5b70 (0x4f7050), 0x52e740 (0x5042ce) y, sin el pool de joints, los de
// +0x380 en 2 (0x505169). La flecha (0x53e2b0) ya no corta: sus cortes están en test_arrow_effects.cpp.
TEST(AttackStepTest, TheCutsAfterTheirDraws)
{
    struct Case
    {
        std::string_view name;
        void (*prepare)(AttackWorld&, ActorState&);
        std::string_view cut;
        int draws;
    };
    const std::array cases{
        Case{"la habilidad 0x28", [](AttackWorld&, ActorState& hero) { hero.byte38e = 0x28; }, "0x4f9899", SkillRead},
        Case{"la habilidad 3", [](AttackWorld&, ActorState& hero) { hero.byte38e = 3; }, "0x4f8a39", CountingStep},
        Case{"la habilidad 5",
             [](AttackWorld&, ActorState& hero)
             {
                 hero.byte38e = 5;
                 hero.byte381 = 14;
             },
             "0x4fdeeb", CountingStep + SkillRead},
        Case{"la habilidad 0x18",
             [](AttackWorld&, ActorState& hero)
             {
                 hero.byte38e = 0x18;
                 hero.byte381 = 14;
             },
             "0x504372", CountingStep + 2 * SkillRead},
        Case{"el caso 0x23 de +0x376",
             [](AttackWorld&, ActorState& hero)
             {
                 hero.word376 = 0x23;
                 hero.byte38e = 0x32;
             },
             "0x4f7050", SkillRead + 1 + SkillRead},
        Case{"la 0x39",
             [](AttackWorld&, ActorState& hero)
             {
                 hero.animation.action = 0x39;
                 hero.byte381 = 14;
             },
             "0x5042ce", CountingStep + SkillRead},
        Case{"mató, sin los joints",
             [](AttackWorld& tested, ActorState& hero)
             {
                 tested.withJoints = false;
                 hero.byte380 = 2;
                 hero.byte381 = 14;
             },
             "0x505169", CountingStep + 2 * SkillRead},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        AttackWorld tested;
        ActorState& hero = tested.Striker(HeroSlot, 0x23, VictimSlot);
        probe.prepare(tested, hero);

        const auto stepped = tested.Attack(hero, HeroSlot);

        EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        EXPECT_EQ(DrawsSince(Seed, tested.random), probe.draws);
    }
}

// 0x558eb0: la clave del elegido solo con el héroe que pega. −1 con otro, con un ítem en el cursor o sin elegido; el
// PK de 6 o más, Ctrl con otro y +0x370 en 2 dan la clave; Chaos Castle, también la del héroe; el duelo, solo la del
// rival; la guerra de gremios con un PK de 6 corta.
TEST(AttackStepTest, TheChosenKeyIsTheHerosOnly)
{
    struct Case
    {
        std::string_view name;
        void (*prepare)(AttackWorld&);
        std::size_t attacker;
        std::int32_t key;
    };
    constexpr std::int32_t Key = 0x1234;
    const std::array cases{
        Case{"sin elegido", [](AttackWorld&) {}, HeroSlot, -1},
        Case{"un PK de 6", [](AttackWorld& tested) { tested.Victim().pk = 6; }, HeroSlot, Key},
        Case{"un PK de 6 con otro que pega", [](AttackWorld& tested) { tested.Victim().pk = 6; }, OtherSlot, -1},
        Case{"un PK de 3", [](AttackWorld&) {}, HeroSlot, -1},
        Case{"Ctrl", [](AttackWorld& tested) { tested.controlDown = true; }, HeroSlot, Key},
        Case{"+0x370 en 2", [](AttackWorld& tested) { tested.Victim().byte370 = 2; }, HeroSlot, Key},
        Case{"el cursor",
             [](AttackWorld& tested)
             {
                 tested.Victim().pk = 6;
                 tested.world.Runtime().cursorItemCount = 1;
             },
             HeroSlot, -1},
        Case{"Chaos Castle", [](AttackWorld& tested) { tested.world.Runtime().map = 18; }, HeroSlot, Key},
        Case{"el duelo con otro",
             [](AttackWorld& tested)
             {
                 tested.Victim().pk = 6;
                 tested.world.Runtime().attackRules.duelActive = true;
             },
             HeroSlot, -1},
        Case{"el duelo con el rival",
             [](AttackWorld& tested)
             {
                 tested.world.Runtime().attackRules.duelActive = true;
                 tested.world.Runtime().attackRules.duelKey = 0x1234;
             },
             HeroSlot, Key},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        AttackWorld tested;
        tested.Victim().key = Key;
        if (probe.name != "sin elegido")
            tested.world.Runtime().selectedTargets[2] = static_cast<std::int32_t>(VictimSlot);
        probe.prepare(tested);

        const auto key = ChosenKeyOf(tested.world, probe.attacker, tested.controlDown);

        ASSERT_TRUE(key.HasValue()) << key.Error();
        EXPECT_EQ(key.Value(), probe.key);
    }

    AttackWorld guild;
    guild.world.Runtime().selectedTargets[2] = static_cast<std::int32_t>(VictimSlot);
    guild.world.Runtime().attackRules.byte576796c = 1;
    guild.Victim().pk = 6;
    guild.Victim().word1f2 = 2;
    const auto cut = ChosenKeyOf(guild.world, HeroSlot, false);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x558f29"), std::string::npos);

    // En el paso de la habilidad, la clave va a o+0x8a.
    AttackWorld stepped;
    stepped.world.Runtime().selectedTargets[2] = static_cast<std::int32_t>(VictimSlot);
    stepped.Victim().key = Key;
    stepped.Victim().pk = 6;
    ActorState& hero = stepped.Striker(HeroSlot, 0x23, VictimSlot);
    hero.byte381 = 14;
    ASSERT_TRUE(stepped.Attack(hero, HeroSlot).HasValue());
    EXPECT_EQ(hero.word8a, Key);
}

// 0x5040b9..0x5041a4: con el daño del último 15 en el blanco (+0x384) y otro tipo que el 0x159, diez veces tres
// rand() & 63 (el punto: la x y la y del blanco − 32, su z + 0x5a) y la sangre 0x5e4 (0x53fe70, con cuatro rand() y los
// ángulos del que pega); después sigue la habilidad hasta el sonido. Sin el pool, corta después del primer punto; con
// el pool lleno, solo los tres rand() de cada punto. El 0x159 no sangra.
TEST(AttackStepTest, TheTargetWithDamageBleedsTenTimes)
{
    constexpr int Bleeding = 10 * (3 + 4);
    const glm::vec3 victimAt{12800.0f, 6400.0f, 150.0f};
    const auto prepare = [&victimAt](AttackWorld& tested)
    {
        tested.Victim().alive = true;
        tested.Victim().damage = 12;
        tested.Victim().position = victimAt;
        ActorState& hero = tested.Striker(HeroSlot, 0x23, VictimSlot);
        hero.byte381 = 14;
        hero.angles = {0.0f, 0.0f, 135.0f};
        return &hero;
    };
    {
        AttackWorld tested;
        ActorState* hero = prepare(tested);
        const auto stepped = tested.Attack(*hero, HeroSlot);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(DrawsSince(Seed, tested.random), CountingStep + SkillWithTarget + Bleeding);
        EXPECT_EQ(tested.sounds.size(), 1u);
        const auto blood = AliveOf(tested.particles, Client::World::BloodParticle);
        ASSERT_EQ(blood.size(), 10u);
        CrtRandom expected(Seed);
        for (int draw = 0; draw < CountingStep + SkillRead; ++draw)
            static_cast<void>(expected.Next());
        for (const Client::World::Particle* drop : blood)
        {
            const int x = (expected.Next() & 63) - 32;
            const int y = (expected.Next() & 63) - 32;
            const int z = (expected.Next() & 63) + 0x5a;
            EXPECT_EQ(drop->start, (glm::vec3{victimAt.x + static_cast<float>(x), victimAt.y + static_cast<float>(y),
                                              victimAt.z + static_cast<float>(z)}));
            EXPECT_EQ(drop->angles.y, 0.0f);
            EXPECT_EQ(drop->angles.z, 135.0f);
            EXPECT_EQ(drop->life, 12);
            for (int draw = 0; draw < 4; ++draw)
                static_cast<void>(expected.Next());
        }
    }
    {
        AttackWorld tested;
        tested.withParticles = false;
        ActorState* hero = prepare(tested);
        const auto stepped = tested.Attack(*hero, HeroSlot);
        EXPECT_TRUE(HasError(stepped, "0x50419c")) << ErrorOf(stepped);
        EXPECT_EQ(DrawsSince(Seed, tested.random), CountingStep + SkillRead + 3);
    }
    {
        AttackWorld tested;
        for (Client::World::Particle& particle : tested.particles.All())
            particle.alive = true;
        ActorState* hero = prepare(tested);
        ASSERT_TRUE(tested.Attack(*hero, HeroSlot).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), CountingStep + SkillWithTarget + 10 * 3);
    }
    {
        AttackWorld tested;
        ActorState* hero = prepare(tested);
        tested.Victim().type = 0x159;
        ASSERT_TRUE(tested.Attack(*hero, HeroSlot).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), CountingStep + SkillWithTarget);
        EXPECT_TRUE(AliveOf(tested.particles, Client::World::BloodParticle).empty());
    }
}

// 0x5041bf..0x50426f: el blanco 0x18e (las estatuas de Blood Castle) con daño, después de la sangre, tira cinco veces
// rand() & 1; con 0, rand() % 30 para la z (z + 50 + el rand()) y la piedra 0x10b (0x51bafb, nueve rand(): con el
// subtipo 0 pasa al 1), con los ángulos y la luz del que pega y sin dueño. Después, el sonido 0x88 y el resto. En
// Lorencia, en Devias y en Blood Castle 1 (el 11). Cada piedra da pasos hasta morir o hasta el corte de 0x5252e6.
TEST(AttackStepTest, TheStatueWithDamageThrowsStones)
{
    constexpr int Bleeding = 10 * (3 + 4);
    constexpr int Flips = 5;
    constexpr int StoneDraws = 1 + 9;
    const glm::vec3 victimAt{12800.0f, 6400.0f, 150.0f};
    int cuts = 0;
    int steps = 0;
    for (const std::int32_t map : {0, 2, 11})
    {
        SCOPED_TRACE(map);
        AttackWorld tested;
        tested.world.Runtime().map = map;
        tested.Victim().alive = true;
        tested.Victim().type = 0x18e;
        tested.Victim().damage = 12;
        tested.Victim().position = victimAt;
        ActorState& hero = tested.Striker(HeroSlot, 0x23, VictimSlot);
        hero.byte381 = 14;
        hero.angles = {0.0f, 0.0f, 135.0f};
        hero.light = {0.25f, 0.5f, 0.75f};

        const auto stepped = tested.Attack(hero, HeroSlot);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        CrtRandom expected(Seed);
        expected.Discard(CountingStep + SkillRead + Bleeding);
        std::vector<int> rises;
        for (int flip = 0; flip < Flips; ++flip)
        {
            if ((expected.Next() & 1) != 0)
                continue;
            rises.push_back(expected.Next() % 30);
            expected.Discard(9);
        }
        ASSERT_FALSE(rises.empty());
        EXPECT_EQ(DrawsSince(Seed, tested.random),
                  CountingStep + SkillWithTarget + Bleeding + Flips + static_cast<int>(rises.size()) * StoneDraws);
        EXPECT_NE(std::ranges::find(tested.sounds, 0x88), tested.sounds.end());
        std::vector<Client::World::ModelEffect*> stones;
        for (Client::World::ModelEffect& effect : tested.world.Map().effects.Effects())
        {
            if (effect.alive && effect.type == Client::World::DebrisEffect)
                stones.push_back(&effect);
        }
        ASSERT_EQ(stones.size(), rises.size());
        for (std::size_t index = 0; index < stones.size(); ++index)
        {
            const Client::World::ModelEffect& stone = *stones[index];
            const float lifted = static_cast<float>(static_cast<double>(victimAt.z) + 50.0);
            const float z = static_cast<float>(static_cast<double>(rises[index]) + static_cast<double>(lifted));
            EXPECT_EQ(stone.transform.position, (glm::vec3{victimAt.x, victimAt.y, z + 50.0f}));
            EXPECT_EQ(stone.subtype, 1);
            EXPECT_EQ(stone.light, hero.light);
            EXPECT_EQ(stone.ownerContext, nullptr);
            EXPECT_EQ(stone.disturbanceState, 0xffff);
            EXPECT_GE(stone.life, 32);
            EXPECT_LE(stone.life, 47);
        }
        // Los pasos de cada piedra (0x5208a0 → 0x52531d), dos rand() cada uno: la luz local y el de 0x5252e6. Con
        // rand() % 10 en 0 corta ahí (el polvo en un local sin escribir, Q-C1176); si no, la cola resta la vida.
        // Con la misma semilla las tres piedras darían el mismo primer paso: cada mapa corre la CRT `map` lugares para
        // pasar por las dos ramas.
        Client::World::ModelEffectStepServices services;
        services.terrainHeight = [](float, float) { return 0.0; };
        tested.random.Discard(static_cast<std::uint32_t>(map));
        for (Client::World::ModelEffect* stone : stones)
        {
            while (stone->alive)
            {
                CrtRandom probe = tested.random;
                static_cast<void>(probe.Next());
                const bool dusty = probe.Next() % 10 == 0;
                const std::int32_t life = stone->life;
                const CrtRandom before = tested.random;
                const auto moved = Client::World::StepGameModelEffect(*stone, nullptr, tested.particles, 0.0f,
                                                                      tested.random, nullptr, services);
                EXPECT_EQ(DrawsSince(before.State(), tested.random), 2);
                if (dusty)
                {
                    EXPECT_TRUE(HasError(moved, "0x5252e6")) << ErrorOf(moved);
                    ++cuts;
                    break;
                }
                ASSERT_TRUE(moved.HasValue()) << moved.Error();
                EXPECT_LT(stone->life, life);
                ++steps;
            }
        }
    }
    EXPECT_GT(cuts, 0);
    EXPECT_GT(steps, 0);
}

// 0x4fb08c..0x4fb0da: con el 0x815 en la izquierda del registro propio, el frame 5 de cualquiera que pegue pide veinte
// chispas 0x5c6/12 sobre él, con su luz (+0xf4), escala 2 y seis rand() cada una, y el ataque sigue hasta el sonido.
// Sin el pool corta después del registro; con el pool lleno no saca nada más.
TEST(AttackStepTest, TheBowInTheOwnLeftHandSparksTwentyTimes)
{
    constexpr int Sparks = 20;
    constexpr int SparkDraws = 6;
    // Después de 0x4f97b0, los tres de 0x4f5b70 y, con +0x381 en 16, la habilidad con blanco.
    constexpr int AfterTheFrames = CountingStep - SkillRead + SkillWithTarget;
    const auto prepare = [](AttackWorld& tested)
    {
        tested.world.OwnCharacter().equipment[1].id = 0x815;
        ActorState& hero = tested.Striker(HeroSlot, 0x23, VictimSlot);
        hero.animation.frame = 5.0f;
        hero.position = {1000.0f, 2000.0f, 50.0f};
        hero.angles = {0.0f, 0.0f, 90.0f};
        hero.light = {0.5f, 0.25f, 1.0f};
        return &hero;
    };
    {
        AttackWorld tested;
        ActorState* hero = prepare(tested);
        ASSERT_TRUE(tested.Attack(*hero, HeroSlot).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), SkillRead + OwnRecord + Sparks * SparkDraws + AfterTheFrames);
        EXPECT_EQ(hero->byte381, 0);
        int found = 0;
        for (const Client::World::Particle& spark : tested.particles.All())
        {
            if (!spark.alive)
                continue;
            ++found;
            EXPECT_EQ(spark.type, Client::World::SparkParticle);
            EXPECT_EQ(spark.subtype, Client::World::BowSparkSubtype);
            EXPECT_EQ(spark.texture, 0x5fc);
            EXPECT_EQ(spark.life, 40);
            EXPECT_EQ(spark.angles, hero->angles);
            EXPECT_EQ(spark.light, hero->light);
        }
        EXPECT_EQ(found, Sparks);
    }
    {
        AttackWorld tested;
        tested.withParticles = false;
        ActorState* hero = prepare(tested);
        EXPECT_TRUE(HasError(tested.Attack(*hero, HeroSlot), "0x4fb0d1"));
        EXPECT_EQ(DrawsSince(Seed, tested.random), SkillRead + OwnRecord);
    }
    {
        AttackWorld tested;
        for (Client::World::Particle& particle : tested.particles.All())
            particle.alive = true;
        ActorState* hero = prepare(tested);
        ASSERT_TRUE(tested.Attack(*hero, HeroSlot).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), SkillRead + OwnRecord + AfterTheFrames);
    }
}

// 0x505069..0x5051a4: con +0x380 en 2 (el 16 del héroe: mató), la habilidad lanza el 0x618/0 y el 0x618/1 desde el
// blanco, con el que pega de dueño, el ancho 20 y los ángulos del que pega, sin rand(), y sigue hasta el sonido.
TEST(AttackStepTest, TheKillerLaunchesTwoBeamsFromTheTarget)
{
    AttackWorld tested;
    tested.Victim().alive = true;
    tested.Victim().position = {3000.0f, 4000.0f, 100.0f};
    ActorState& hero = tested.Striker(HeroSlot, 0x23, VictimSlot);
    hero.byte380 = 2;
    hero.byte381 = 14;
    hero.angles = {0.0f, 0.0f, 45.0f};

    ASSERT_TRUE(tested.Attack(hero, HeroSlot).HasValue());

    EXPECT_EQ(DrawsSince(Seed, tested.random), CountingStep + SkillWithTarget);
    EXPECT_EQ(hero.byte380, 0);
    EXPECT_EQ(tested.sounds.size(), 1u);
    for (const std::int32_t subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        const JointState& beam = tested.joints[static_cast<std::size_t>(subtype)];
        EXPECT_TRUE(beam.alive);
        EXPECT_EQ(beam.type, Client::Scenes::KillBeamJoint);
        EXPECT_EQ(beam.subtype, subtype);
        EXPECT_EQ(beam.ownerContext, &hero);
        EXPECT_EQ(beam.position, tested.Victim().position);
        EXPECT_EQ(beam.angles, hero.angles);
        EXPECT_EQ(beam.width, 20.0f);
        EXPECT_EQ(beam.speed, 3.0f);
        EXPECT_EQ(beam.life, 120);
        EXPECT_EQ(beam.maximumCount, 8);
        EXPECT_EQ(beam.key, -1);
    }
    EXPECT_FALSE(tested.joints[2].alive);
}

// El ataque cuerpo a cuerpo de cada clase, del héroe (a un blanco del pool) y de otro jugador (al héroe, como lo deja
// el 18), por el paso entero de los personajes (0x508f00) y el de las partículas (0x5446e0): veintidós pasos sin
// cortar, con la acción a 0,5 keys por paso (el frame 5 en el décimo). Lo que saca de más contra el mismo mundo sin
// +0x381: 4 rand() en cada uno de los nueve primeros pasos y, en el décimo, el registro propio de 0x4f97b0, las
// lecturas de 0x4f5b70 y la habilidad con el sonido. Después, +0x381 en 0. Con el daño del último 15 en el blanco,
// además las diez sangres 0x5e4 con sus rand(), que viven sus 12 pasos. Los dos rand() del giro de la cabeza (0x5068fd,
// uno en 32 por paso en los que no son el héroe) dependen de la semilla: se descuentan los de cada blanco que cambió.
TEST(AttackStepTest, AMeleeAttackOfEachClassRunsItsSteps)
{
    struct Case
    {
        std::string_view name;
        std::uint8_t characterClass;
        std::uint8_t action;
        std::uint16_t right;
        std::uint16_t left;
    };
    constexpr int Steps = 22;
    constexpr int AttackDraws = 9 * CountingStep + StrikingStep + SkillWithTarget;
    constexpr int HeadSortDraws = 2;
    constexpr int Bleeding = 10 * (3 + 4);
    const std::array cases{Case{"el mago con el bastón", 0, 0x2b, 0xa00, 0xffff},
                           Case{"el caballero con la espada", 1, 0x23, 0x000, 0xffff},
                           Case{"el elfo sin armas", 2, 0x22, 0xffff, 0xffff},
                           Case{"el gladiador con dos espadas", 3, 0x25, 0x000, 0x001},
                           Case{"el Dark Lord con el cetro", 4, 0x24, 0x400, 0xffff},
                           Case{"el Dark Lord en Fenrir", 4, 0x59, 0x400, 0xffff}};
    for (const auto& [heroStrikes, hurt] :
         {std::pair{true, false}, std::pair{false, false}, std::pair{true, true}, std::pair{false, true}})
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message()
                         << probe.name << (heroStrikes ? ", el héroe" : ", otro") << (hurt ? ", con daño" : ""));
            const auto run = [&](bool attacking, int& draws)
            {
                AttackWorld tested;
                tested.skeleton.actions[probe.action] =
                    ActionInfo{.repeats = false, .speed = 0.5f, .keyCount = 30, .lockPositions = false};
                auto& equipment = tested.world.OwnCharacter().equipment;
                equipment[0].id = probe.right;
                equipment[1].id = probe.left;
                tested.Hero().characterClass = probe.characterClass;
                const std::size_t slot = heroStrikes ? HeroSlot : OtherSlot;
                ActorState& striker = tested.Striker(slot, probe.action, heroStrikes ? VictimSlot : HeroSlot);
                striker.characterClass = probe.characterClass;
                if (!attacking)
                    striker.byte381 = 0;
                if (hurt)
                    tested.world.Actors()[heroStrikes ? VictimSlot : HeroSlot].damage = 12;
                Client::World::PortableParticleTrigonometry trigonometry;
                const float clock = 0.0f;
                const std::int32_t map = 0;
                const auto heads = [&tested]
                {
                    std::vector<glm::vec2> targets;
                    for (const ActorState& actor : tested.world.Actors())
                        targets.push_back(actor.headTurn.target);
                    return targets;
                };
                int headDraws = 0;
                for (int step = 0; step < Steps; ++step)
                {
                    const std::vector<glm::vec2> before = heads();
                    const auto stepped = StepCharacters(tested.Context());
                    if (!stepped.HasValue())
                        return stepped;
                    const std::vector<glm::vec2> after = heads();
                    for (std::size_t k = 0; k < before.size(); ++k)
                    {
                        if (before[k] != after[k])
                            headDraws += HeadSortDraws;
                    }
                    if (!Client::World::StepParticlesChecked(tested.particles, tested.random, std::cref(clock), &map,
                                                             trigonometry)
                             .HasValue())
                        return Client::Core::Expected<void, std::string>(
                            Client::Core::Unexpected(std::string("0x5446e0: el paso de las partículas")));
                }
                EXPECT_EQ(striker.byte381, 0);
                EXPECT_EQ(tested.sounds.size(), attacking ? 1u : 0u);
                const auto created = std::ranges::count(tested.particles.All(), Client::World::BloodParticle,
                                                        &Client::World::Particle::type);
                EXPECT_EQ(created, attacking && hurt ? 10 : 0);
                EXPECT_TRUE(AliveOf(tested.particles, Client::World::BloodParticle).empty());
                draws = DrawsSince(Seed, tested.random) - headDraws;
                return Client::Core::Expected<void, std::string>{};
            };
            int attackDraws = 0;
            int quietDraws = 0;
            const auto attacked = run(true, attackDraws);
            const auto quiet = run(false, quietDraws);

            ASSERT_TRUE(attacked.HasValue()) << attacked.Error();
            ASSERT_TRUE(quiet.HasValue()) << quiet.Error();
            EXPECT_EQ(attackDraws - quietDraws, AttackDraws + (hurt ? Bleeding : 0));
        }
    }
}

// Una muerte de cada clase, en Lorencia y en Devias: el héroe con +0x380 en 2 pega a un blanco con daño y el paso
// entero de los personajes (0x508f00), el de los joints (0x535840) y el de las partículas (0x5446e0) corren sin cortar
// hasta que los dos 0x618 vuelven al héroe con su sonido 0x23 (además del del golpe) y se apagan, con sus 0x5d5 y las
// sangres.
TEST(AttackStepTest, AKillOfEachClassSendsTheBeamsBackWithoutACut)
{
    constexpr int Steps = 120;
    constexpr std::int32_t HitSound = 0x23;
    const std::array cases{std::pair{0, 0x2b}, std::pair{1, 0x23}, std::pair{2, 0x22}, std::pair{3, 0x25},
                           std::pair{4, 0x24}};
    for (const std::int32_t map : {0, 2})
    {
        for (const auto& [characterClass, action] : cases)
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", clase " << characterClass);
            AttackWorld tested;
            tested.world.Runtime().map = map;
            tested.skeleton.actions[static_cast<std::size_t>(action)] =
                ActionInfo{.repeats = false, .speed = 0.5f, .keyCount = 30, .lockPositions = false};
            tested.Hero().characterClass = static_cast<std::uint8_t>(characterClass);
            ActorState& hero = tested.Striker(HeroSlot, static_cast<std::uint8_t>(action), VictimSlot);
            hero.byte380 = 2;
            tested.Victim().alive = true;
            tested.Victim().position = {hero.position.x + 300.0f, hero.position.y, hero.position.z};
            tested.Victim().damage = 12;
            Client::World::PortableParticleTrigonometry trigonometry;
            auto lighting = std::make_unique<Client::World::TerrainLighting>();
            const float clock = 0.0f;
            const auto sound = [&tested](std::int32_t played) { tested.sounds.push_back(played); };
            int step = 0;
            for (; step < Steps; ++step)
            {
                SCOPED_TRACE(step);
                const auto stepped = StepCharacters(tested.Context());
                ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
                ASSERT_TRUE(Client::Scenes::StepJoints(tested.joints, tested.random, nullptr, &tested.particles, clock,
                                                       nullptr, lighting.get(), sound)
                                .HasValue());
                ASSERT_TRUE(Client::World::StepParticlesChecked(tested.particles, tested.random, std::cref(clock), &map,
                                                                trigonometry, {}, {}, {}, {}, lighting.get())
                                .HasValue());
                if (step > 10 && !tested.particles.Any() && std::ranges::none_of(tested.joints, &JointState::alive))
                    break;
            }
            EXPECT_LT(step, Steps);
            EXPECT_EQ(std::ranges::count(tested.sounds, HitSound), 2);
            EXPECT_EQ(hero.byte380, 0);
        }
    }
}

// El elfo con el arco (0x2e, el 4/0 en la izquierda del registro propio) o la ballesta (0x2f, el 4/8 en la derecha) y
// la skill 0x18 en el lugar 3 de +0x6c tira en el paso del frame 5 (0x504060): 0x59fb80 da el lugar, 0x53e2b0 crea la
// flecha con el lugar en +0x89 y deja la skill en [0x5767948]; después corta en el switch de las skills con blanco
// (0x504372, hito 4), con la flecha viva.
TEST(AttackStepTest, TheElfBowShootsAndStopsAtTheSkill)
{
    struct Case
    {
        std::uint8_t action;
        std::uint16_t right;
        std::uint16_t left;
        std::int16_t arrow;
    };
    for (const Case& probe : {Case{0x2e, 0xffff, 0x800, 0xdb}, Case{0x2f, 0x808, 0xffff, 0xdc}})
    {
        SCOPED_TRACE(probe.action);
        AttackWorld tested;
        tested.skeleton.actions[probe.action] =
            ActionInfo{.repeats = false, .speed = 0.5f, .keyCount = 30, .lockPositions = false};
        tested.Hero().characterClass = 2;
        tested.world.OwnCharacter().words6c[0] = 0x18000000u; // el byte 3 de +0x6c
        tested.world.OwnCharacter().equipment[0].id = probe.right;
        tested.world.OwnCharacter().equipment[1].id = probe.left;
        ActorState& hero = tested.Striker(HeroSlot, probe.action, VictimSlot);
        hero.byte38e = 0x18;
        Client::Core::Expected<void, std::string> stepped;
        int step = 0;
        for (; step < 20 && stepped.HasValue(); ++step)
            stepped = StepCharacters(tested.Context());

        EXPECT_TRUE(HasError(stepped, "0x504372")) << ErrorOf(stepped);
        EXPECT_EQ(step, 10);
        EXPECT_EQ(tested.world.Runtime().lastShotSkill, 0x18u);
        const auto& effects = tested.world.Map().effects.Effects();
        const auto arrow = std::ranges::find_if(effects, [](const auto& effect) { return effect.alive; });
        ASSERT_NE(arrow, effects.end());
        EXPECT_EQ(arrow->type, probe.arrow);
        EXPECT_EQ(arrow->byte89, 3);
        EXPECT_EQ(arrow->ownerContext, &hero);
    }
}
} // namespace
