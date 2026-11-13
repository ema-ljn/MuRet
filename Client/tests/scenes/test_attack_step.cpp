#include "Scenes/AttackStep.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/HeroAttack.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

    CharacterStepContext Context()
    {
        return CharacterStepContext{.world = world,
                                    .random = random,
                                    .playerSkeleton = &skeleton,
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
};

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
// 0x504372), los monstruos de la fase 2c (0x4f5c0f), el arco 0x815 del registro propio (0x4fb0d1), la flecha con y sin
// blanco (0x504048, 0x50376a), las partículas del blanco con daño (0x50419c), 0x52e740 (0x5042ce) y los joints de
// +0x380 en 2 (0x505169).
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
        Case{"un monstruo", [](AttackWorld&, ActorState& hero) { hero.word376 = 0x10; }, "0x4f5c0f", SkillRead + 1},
        Case{"el 0x815",
             [](AttackWorld& tested, ActorState& hero)
             {
                 tested.world.OwnCharacter().equipment[1].id = 0x815;
                 hero.animation.frame = 5.0f;
             },
             "0x4fb0d1", SkillRead + OwnRecord},
        Case{"el arco",
             [](AttackWorld&, ActorState& hero)
             {
                 hero.animation.action = 0x2e;
                 hero.byte381 = 14;
             },
             "0x504048", CountingStep + 3 * SkillRead},
        Case{"Fenrir sin blanco",
             [](AttackWorld&, ActorState& hero)
             {
                 hero.animation.action = 0x61;
                 hero.attackTarget = 0xffff;
                 hero.byte381 = 14;
             },
             "0x50376a", SkillRead + 1 + 4 * SkillRead},
        Case{"el blanco con daño",
             [](AttackWorld& tested, ActorState& hero)
             {
                 tested.Victim().damage = 12;
                 hero.byte381 = 14;
             },
             "0x50419c", CountingStep + SkillRead + 3},
        Case{"la 0x39",
             [](AttackWorld&, ActorState& hero)
             {
                 hero.animation.action = 0x39;
                 hero.byte381 = 14;
             },
             "0x5042ce", CountingStep + SkillRead},
        Case{"mató",
             [](AttackWorld&, ActorState& hero)
             {
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

// El ataque cuerpo a cuerpo de cada clase, del héroe (a un blanco del pool) y de otro jugador (al héroe, como lo deja
// el 18), por el paso entero de los personajes (0x508f00): veinte pasos sin cortar, con la acción a 0,5 keys por paso
// (el frame 5 en el décimo). Lo que saca de más contra el mismo mundo sin +0x381: 4 rand() en cada uno de los nueve
// primeros pasos y, en el décimo, el registro propio de 0x4f97b0, las lecturas de 0x4f5b70 y la habilidad con el
// sonido. Después, +0x381 en 0. El arco del elfo corta en la flecha (0x504048) en el décimo paso.
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
    constexpr int Steps = 20;
    constexpr int AttackDraws = 9 * CountingStep + StrikingStep + SkillWithTarget;
    const std::array cases{Case{"el mago con el bastón", 0, 0x2b, 0xa00, 0xffff},
                           Case{"el caballero con la espada", 1, 0x23, 0x000, 0xffff},
                           Case{"el elfo sin armas", 2, 0x22, 0xffff, 0xffff},
                           Case{"el gladiador con dos espadas", 3, 0x25, 0x000, 0x001},
                           Case{"el Dark Lord con el cetro", 4, 0x24, 0x400, 0xffff},
                           Case{"el Dark Lord en Fenrir", 4, 0x59, 0x400, 0xffff}};
    for (const bool heroStrikes : {true, false})
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message() << probe.name << (heroStrikes ? ", el héroe" : ", otro"));
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
                for (int step = 0; step < Steps; ++step)
                {
                    const auto stepped = StepCharacters(tested.Context());
                    if (!stepped.HasValue())
                        return stepped;
                }
                EXPECT_EQ(striker.byte381, 0);
                EXPECT_EQ(tested.sounds.size(), attacking ? 1u : 0u);
                draws = DrawsSince(Seed, tested.random);
                return Client::Core::Expected<void, std::string>{};
            };
            int attackDraws = 0;
            int quietDraws = 0;
            const auto attacked = run(true, attackDraws);
            const auto quiet = run(false, quietDraws);

            ASSERT_TRUE(attacked.HasValue()) << attacked.Error();
            ASSERT_TRUE(quiet.HasValue()) << quiet.Error();
            EXPECT_EQ(attackDraws - quietDraws, AttackDraws);
        }
    }
}

// El elfo con el arco (0x2e) o la ballesta (0x2f) llega a la flecha en el paso del frame 5.
TEST(AttackStepTest, TheElfBowStopsAtTheArrow)
{
    for (const std::uint8_t action : {std::uint8_t{0x2e}, std::uint8_t{0x2f}})
    {
        SCOPED_TRACE(action);
        AttackWorld tested;
        tested.skeleton.actions[action] =
            ActionInfo{.repeats = false, .speed = 0.5f, .keyCount = 30, .lockPositions = false};
        tested.Hero().characterClass = 2;
        tested.Striker(HeroSlot, action, VictimSlot);
        Client::Core::Expected<void, std::string> stepped;
        int step = 0;
        for (; step < 20 && stepped.HasValue(); ++step)
            stepped = StepCharacters(tested.Context());

        EXPECT_TRUE(HasError(stepped, "0x504048")) << ErrorOf(stepped);
        EXPECT_EQ(step, 10);
    }
}
} // namespace
