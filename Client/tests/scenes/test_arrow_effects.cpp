#include "Scenes/ArrowEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/AttackStep.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"

#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Client::Scenes::FireTheArrow y StepArrow: 0x53e2b0, 0x53d9a0, los casos de flecha de 0x518cf0 y de 0x5208a0, 0x520170
// y 0x517850, por el paso del ataque (0x50377d y 0x504060). 06, "La flecha".
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::ArrowStepContext;
using Client::Scenes::CharacterStepContext;
using Client::Scenes::StepArrow;
using Client::Scenes::StepTheAttack;
using Client::Scenes::WorldState;
using Client::World::ModelEffect;

constexpr std::uint32_t Seed = 0x2468ace1u;
constexpr std::size_t HeroSlot = 42;
constexpr std::size_t OtherSlot = 3;
constexpr std::size_t VictimSlot = 7;
constexpr std::uint16_t NoTarget = 0xffff;
// Una lectura de +0x38e: un rand() al cerrarse en 0.
constexpr int SkillRead = 1;
constexpr int OwnRecord = Client::Render::Models::OwnRecordCloseRandoms;
// El paso de +0x381 que corre la habilidad, antes del frame 5: la lectura de 0x4f97b0, el rand() de 0x4f5b97 y, con
// blanco en el pool, las dos lecturas de 0x4f811b y 0x4f8a2c.
constexpr int CountingWithTarget = SkillRead + 1 + 2 * SkillRead;
constexpr int CountingWithoutTarget = SkillRead + 1;
// La flecha en el paso del ataque: las dos lecturas de +0x38e, 0x59fb80 con el registro propio y, con el héroe, las
// armas de 0x53d9a0 con el registro propio.
constexpr int ShotReads = 2 * SkillRead + OwnRecord;
// El alta de cada flecha: la salida 0x51a1b6 con el registro propio.
constexpr int ArrowCreation = OwnRecord;
// Lo de después de la flecha con blanco sin daño: los dos switch, el sonido con el registro propio y la escritura.
constexpr int AfterTheArrow = 2 * SkillRead + 1 + OwnRecord + SkillRead;

// El arco 4/0 en la izquierda y la ballesta 4/8 en la derecha, con sus acciones a pie y sus flechas.
struct Weapon
{
    std::string_view name;
    std::uint16_t right;
    std::uint16_t left;
    std::uint8_t action;
    std::int16_t arrow;
};
constexpr Weapon ShortBow{"el arco corto", 0xffff, 0x800, 0x2e, 0xdb};
constexpr Weapon Crossbow{"la ballesta", 0x808, 0xffff, 0x2f, 0xdc};

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

// Los rand() del fuego 0x5d9/0 de la flecha 0xdb (0x53fe70), medidos en un pool aparte.
int FireDraws()
{
    Client::World::Particles scratch;
    CrtRandom random(Seed);
    static_cast<void>(scratch.CreateChecked(Client::World::FireParticle, glm::vec3(0.0f), glm::vec3(0.0f),
                                            glm::vec3(1.0f), 0, 1.0f, 0.0f, random));
    return DrawsSince(Seed, random);
}

struct ArrowWorld
{
    explicit ArrowWorld(std::int32_t map)
    {
        world.Runtime().map = map;
        world.Runtime().hero = HeroSlot;
        world.Runtime().heroSlot = static_cast<std::int32_t>(HeroSlot);
        world.OwnCharacter().ClearEquipmentForEntry();
        // Los tiempos de 0x4fc450 lejos de 0, como en el paso del ataque.
        constexpr std::uint16_t FarTimer = 1000;
        world.OwnCharacter().word3e = FarTimer;
        world.OwnCharacter().word40 = FarTimer;
        world.OwnCharacter().word42 = FarTimer;
        Hero() = Player(2);
        Hero().position = {1000.0f, 1000.0f, 0.0f};
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

    // El blanco en `slot`, a `distance` delante de quien tira (la flecha va hacia −y con los ángulos en 0), visto y
    // vivo: un jugador (+0x88 en 1) o un monstruo (en 2).
    ActorState& Target(std::size_t slot, bool monster, float distance)
    {
        ActorState& target = world.Actors()[slot];
        target = Player(0);
        if (monster)
        {
            target = ActorState{};
            target.alive = true;
            target.type = 0x2;
            target.byte88 = 2;
        }
        target.position = {990.0f, 940.0f - distance, 0.0f};
        target.seen = true;
        return target;
    }

    // El héroe con el arma en el registro propio, la acción y +0x381 en 14: el próximo paso tira.
    ActorState& Archer(const Weapon& weapon, std::uint8_t action, std::uint16_t target)
    {
        auto& equipment = world.OwnCharacter().equipment;
        equipment[0].id = weapon.right;
        equipment[1].id = weapon.left;
        ActorState& hero = Hero();
        hero.animation.action = action;
        hero.byte381 = 14;
        hero.attackTarget = target;
        return hero;
    }

    CharacterStepContext Context()
    {
        return CharacterStepContext{.world = world,
                                    .random = random,
                                    .playerSkeleton = &skeleton,
                                    .particles = &particles,
                                    .soundRequest = [this](std::int32_t sound) { sounds.push_back(sound); }};
    }

    ArrowStepContext StepContext()
    {
        return ArrowStepContext{.world = world,
                                .random = random,
                                .particles = &particles,
                                .sprites = &sprites,
                                .lighting = lighting.get(),
                                .worldClock = 0.0f};
    }

    std::vector<ModelEffect*> Arrows()
    {
        std::vector<ModelEffect*> found;
        for (ModelEffect& effect : world.Map().effects.Effects())
        {
            if (effect.alive && Client::Scenes::IsArrowEffect(effect.type))
                found.push_back(&effect);
        }
        return found;
    }

    // Pasos de la flecha hasta que se apaga (o `limit`); devuelve cuántos dio.
    int StepUntilItDies(ModelEffect& arrow, int limit)
    {
        int steps = 0;
        while (arrow.alive && steps < limit)
        {
            const auto stepped = StepArrow(arrow, nullptr, StepContext());
            EXPECT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
            if (!stepped.HasValue())
                break;
            ++steps;
        }
        return steps;
    }

    std::unique_ptr<WorldState> ownedWorld = std::make_unique<WorldState>();
    WorldState& world = *ownedWorld;
    CrtRandom random{Seed};
    Client::Render::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    Client::World::Particles particles;
    Client::World::FrameSprites sprites;
    std::unique_ptr<Client::World::TerrainLighting> lighting = std::make_unique<Client::World::TerrainLighting>();
    std::vector<std::int32_t> sounds;
};

// El elfo con el arco corto (la 0xdb, por la izquierda del registro propio) o la ballesta (la 0xdc, por la derecha)
// pega a un monstruo y a un jugador, en Lorencia y en Devias: el paso del ataque llega al final (+0x381 en 0, el
// sonido del golpe y la escritura de +0x38e) con la flecha creada (0x518cf0: la boca (−10; −60; 135) girada, vida 30,
// escala 0,8f, la velocidad (0; −70; 0), el dueño, +0x8a, +0x89 y +0x8c), con sus 3.848 rand() de más por el
// registro propio en 0x53d9a0 y otros tantos en la salida del alta. La flecha da cinco pasos (un rand() de luz cada
// uno, y la 0xdb su fuego 0x5d9/0 con la luz dinámica) y en el quinto, a 60 del blanco, choca: abre el registro propio
// para la skill del lugar +0x89 y se apaga.
TEST(ArrowEffectsTest, TheElfShootsAMonsterAndAPlayer)
{
    const int fireDraws = FireDraws();
    for (const std::int32_t map : {0, 2})
    {
        for (const Weapon& weapon : {ShortBow, Crossbow})
        {
            for (const bool monster : {true, false})
            {
                SCOPED_TRACE(::testing::Message()
                             << "mapa " << map << ", " << weapon.name << (monster ? ", al monstruo" : ", al jugador"));
                ArrowWorld tested(map);
                tested.world.OwnCharacter().bytef04 = 9;
                tested.Target(VictimSlot, monster, 340.0f);
                ActorState& hero = tested.Archer(weapon, weapon.action, VictimSlot);

                const auto struck = StepTheAttack(hero, HeroSlot, tested.Context());

                ASSERT_TRUE(struck.HasValue()) << struck.Error();
                EXPECT_EQ(hero.byte381, 0);
                EXPECT_EQ(DrawsSince(Seed, tested.random),
                          CountingWithTarget + SkillRead + ShotReads + OwnRecord + ArrowCreation + AfterTheArrow);
                EXPECT_EQ(tested.sounds.size(), 1u);
                EXPECT_EQ(tested.world.Runtime().movement.lastShotSkill, 0u);
                const auto arrows = tested.Arrows();
                ASSERT_EQ(arrows.size(), 1u);
                ModelEffect& arrow = *arrows.front();
                EXPECT_EQ(arrow.type, weapon.arrow);
                EXPECT_EQ(arrow.subtype, 0);
                EXPECT_EQ(arrow.ownerContext, &hero);
                EXPECT_EQ(arrow.disturbanceState, 0xffff);
                EXPECT_EQ(arrow.byte89, 0);
                EXPECT_EQ(arrow.state, 0);
                EXPECT_EQ(arrow.word8c, 9);
                EXPECT_EQ(arrow.life, 30);
                EXPECT_FLOAT_EQ(arrow.transform.scale, 0.8f);
                EXPECT_EQ(arrow.velocity, glm::vec3(0.0f, -70.0f, 0.0f));
                EXPECT_EQ(arrow.transform.position, glm::vec3(990.0f, 940.0f, 135.0f));
                EXPECT_EQ(arrow.objectState.blendMesh, weapon.arrow == 0xdb ? 1 : -1);

                const CrtRandom before = tested.random;
                const int steps = tested.StepUntilItDies(arrow, 40);

                EXPECT_EQ(steps, 5);
                EXPECT_FALSE(arrow.alive);
                const int perStep = 1 + (weapon.arrow == 0xdb ? fireDraws : 0);
                EXPECT_EQ(DrawsSince(before.State(), tested.random), 5 * perStep + OwnRecord);
                EXPECT_EQ(std::ranges::count(tested.particles.All(), Client::World::FireParticle,
                                             &Client::World::Particle::type),
                          weapon.arrow == 0xdb ? 5 : 0);
            }
        }
    }
}

// Sin blanco (+0x3a4 en −1), en Lorencia y en Devias: la flecha sale igual (0x50377d, con la bandera en 0) y, sin nadie
// a 100, vuela sus 30 pasos con un rand() cada uno y se apaga por la vida. El héroe que tira no la frena.
TEST(ArrowEffectsTest, WithoutATargetTheArrowFliesItsLife)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const Weapon& weapon : {ShortBow, Crossbow})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", " << weapon.name);
            ArrowWorld tested(map);
            tested.Hero().seen = true;
            ActorState& hero = tested.Archer(weapon, weapon.action, NoTarget);

            const auto struck = StepTheAttack(hero, HeroSlot, tested.Context());

            ASSERT_TRUE(struck.HasValue()) << struck.Error();
            EXPECT_EQ(hero.byte381, 0);
            EXPECT_TRUE(tested.sounds.empty());
            // La cabeza, la lectura de 0x502b12 (la bandera), la flecha y la escritura de +0x38e.
            EXPECT_EQ(DrawsSince(Seed, tested.random),
                      CountingWithoutTarget + 2 * SkillRead + ShotReads + OwnRecord + ArrowCreation + SkillRead);
            const auto arrows = tested.Arrows();
            ASSERT_EQ(arrows.size(), 1u);
            ModelEffect& arrow = *arrows.front();
            hero.position = arrow.transform.position;

            const int steps = tested.StepUntilItDies(arrow, 40);

            EXPECT_EQ(steps, 30);
            EXPECT_EQ(arrow.life, 0);
            EXPECT_FALSE(arrow.alive);
        }
    }
}

// Otro elfo, con el arco en su mano (+0x2d8 en 0xa12: sin el registro propio en 0x53d9a0), le tira al héroe: 0x517850
// con la clase 0 no choca con el héroe, que está en el camino, y la flecha vuela sus 30 pasos.
TEST(ArrowEffectsTest, AnotherElfsArrowPassesThroughTheHero)
{
    ArrowWorld tested(0);
    ActorState& elf = tested.world.Actors()[OtherSlot];
    elf = ArrowWorld::Player(2);
    elf.position = {1000.0f, 1000.0f, 0.0f};
    elf.hands[1].model = 0xa12;
    elf.animation.action = 0x2e;
    elf.byte381 = 14;
    elf.attackTarget = HeroSlot;
    tested.Hero().position = {990.0f, 700.0f, 0.0f};
    tested.Hero().seen = true;

    const auto struck = StepTheAttack(elf, OtherSlot, tested.Context());

    ASSERT_TRUE(struck.HasValue()) << struck.Error();
    EXPECT_EQ(DrawsSince(Seed, tested.random),
              CountingWithTarget + SkillRead + ShotReads + ArrowCreation + AfterTheArrow);
    const auto arrows = tested.Arrows();
    ASSERT_EQ(arrows.size(), 1u);
    EXPECT_EQ(arrows.front()->type, 0xdb);
    EXPECT_EQ(arrows.front()->ownerContext, &elf);
    EXPECT_EQ(tested.StepUntilItDies(*arrows.front(), 40), 30);
}

// Los arcos y las ballestas montados tiran igual que a pie (CL9 hito 3, fase 2e, la segunda pieza): las ocho acciones
// de tiro del 0x20a (a pie, con alas, 0x36/0x37, en Uniria o Dinorant la 0x5e y en Fenrir la 0x61), con y sin blanco,
// en Lorencia, en Devias y en un mapa de cada montura (Atlans para Uniria, Icarus para Dinorant y Tarkan para Fenrir).
TEST(ArrowEffectsTest, TheMountedBowsShootLikeOnFoot)
{
    struct Case
    {
        std::uint8_t action;
        std::int32_t map;
    };
    std::vector<Case> cases;
    constexpr std::array<std::uint8_t, 8> RangedActions{0x2e, 0x2f, 0x30, 0x31, 0x36, 0x37, 0x5e, 0x61};
    for (const std::uint8_t action : RangedActions)
    {
        cases.push_back({action, 0});
        cases.push_back({action, 2});
    }
    cases.push_back({0x5e, 7});
    cases.push_back({0x5e, 10});
    cases.push_back({0x61, 8});
    for (const Case& probe : cases)
    {
        for (const Weapon& weapon : {ShortBow, Crossbow})
        {
            for (const bool withTarget : {true, false})
            {
                SCOPED_TRACE(::testing::Message()
                             << "acción " << static_cast<int>(probe.action) << ", mapa " << probe.map << ", "
                             << weapon.name << (withTarget ? ", con blanco" : ", sin blanco"));
                ArrowWorld tested(probe.map);
                tested.Target(VictimSlot, true, 340.0f);
                ActorState& hero = tested.Archer(weapon, probe.action, withTarget ? VictimSlot : NoTarget);

                const auto struck = StepTheAttack(hero, HeroSlot, tested.Context());

                ASSERT_TRUE(struck.HasValue()) << struck.Error();
                EXPECT_EQ(hero.byte381, 0);
                const int expected =
                    withTarget
                        ? CountingWithTarget + SkillRead + ShotReads + OwnRecord + ArrowCreation + AfterTheArrow
                        : CountingWithoutTarget + 2 * SkillRead + ShotReads + OwnRecord + ArrowCreation + SkillRead;
                EXPECT_EQ(DrawsSince(Seed, tested.random), expected);
                const auto arrows = tested.Arrows();
                ASSERT_EQ(arrows.size(), 1u);
                EXPECT_EQ(arrows.front()->type, weapon.arrow);
                EXPECT_EQ(tested.StepUntilItDies(*arrows.front(), 40), 5);
            }
        }
    }
}

// Las armas cuyas flechas piden algo sin portar cortan en su dirección, con lo de antes: en el alta, los joints de
// la 0xf7 (4/16), la 0xf8 (4/17), la 0x10e (4/18), la 0x111 (4/19), la 0x11c (4/20, después de su rand() % 360) y la
// 0xf9 (4/21); en el paso, la 0xe1 (4/6) en sus partículas 0x608..0x60a, la 0xe2 (4/14) y la 0xe3 (4/13) en las
// 0x607/1, después de sus dos sprites y de los rand() del punto.
TEST(ArrowEffectsTest, TheArrowsOfUnportedEffectsCut)
{
    struct Case
    {
        std::string_view name;
        std::uint16_t right;
        std::uint16_t left;
        std::string_view creationCut;
        int creationDraws;
        std::string_view stepCut;
        int stepDraws;
    };
    const std::array cases{
        Case{"4/16", 0x810, 0xffff, "0x51a24a", 0, "", 0},    Case{"4/17", 0xffff, 0x811, "0x51a405", 0, "", 0},
        Case{"4/18", 0x812, 0xffff, "0x51a173", 0, "", 0},    Case{"4/19", 0x813, 0xffff, "0x51bf35", 0, "", 0},
        Case{"4/20", 0xffff, 0x814, "0x51c713", 1, "", 0},    Case{"4/21", 0xffff, 0x815, "0x51c56c", 0, "", 0},
        Case{"4/6", 0xffff, 0x806, "", 0, "0x526a8b", 1 + 4}, Case{"4/14", 0x80e, 0xffff, "", 0, "0x52434e", 1 + 3},
        Case{"4/13", 0x80d, 0xffff, "", 0, "0x526d5f", 1 + 3}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        ArrowWorld tested(0);
        const Weapon weapon{probe.name, probe.right, probe.left, 0x2e, 0};
        ActorState& hero = tested.Archer(weapon, weapon.action, NoTarget);

        const auto struck = StepTheAttack(hero, HeroSlot, tested.Context());

        const int beforeTheArrow = CountingWithoutTarget + 2 * SkillRead + ShotReads + OwnRecord;
        if (!probe.creationCut.empty())
        {
            EXPECT_TRUE(HasError(struck, probe.creationCut)) << ErrorOf(struck);
            EXPECT_EQ(DrawsSince(Seed, tested.random), beforeTheArrow + probe.creationDraws);
            continue;
        }
        ASSERT_TRUE(struck.HasValue()) << struck.Error();
        const auto arrows = tested.Arrows();
        ASSERT_EQ(arrows.size(), 1u);
        const CrtRandom before = tested.random;

        const auto stepped = StepArrow(*arrows.front(), nullptr, tested.StepContext());

        EXPECT_TRUE(HasError(stepped, probe.stepCut)) << ErrorOf(stepped);
        EXPECT_EQ(DrawsSince(before.State(), tested.random), probe.stepDraws);
        EXPECT_EQ(std::ranges::count_if(tested.sprites.Sprites(), [](const auto& sprite) { return sprite.alive; }), 2);
    }
}

// Las skills de tiro (0x34, 0x33 y 0x2e, 0x53e741) y la bandera de la 0x18 sin blanco (0x53e2e0) son del hito 4: cortan
// en 0x53e2b0 antes de crear nada (la 0x34 no llega: corta antes en 0x4f9899). Con la 0xa25 en +0x2b8 del héroe y otra
// arma en el registro propio, el brillo de la mano corta en 0x53e642 después de la flecha.
TEST(ArrowEffectsTest, TheSkillShotsAndTheReignSparkleCut)
{
    struct Case
    {
        std::string_view name;
        std::uint8_t skill;
        std::uint16_t target;
        std::uint16_t rightHand;
        std::string_view cut;
        std::size_t arrows;
    };
    const std::array cases{Case{"la 0x2e", 0x2e, VictimSlot, 0xffff, "0x53e2be", 0},
                           Case{"la 0x33", 0x33, NoTarget, 0xffff, "0x53e2be", 0},
                           Case{"la 0x18 sin blanco", 0x18, NoTarget, 0xffff, "0x53e2da", 0},
                           Case{"la 0xa25 en la mano", 0, VictimSlot, 0xa25, "0x53e642", 1}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        ArrowWorld tested(2);
        tested.Target(VictimSlot, true, 400.0f);
        ActorState& hero = tested.Archer(ShortBow, ShortBow.action, probe.target);
        hero.byte38e = probe.skill;
        hero.hands[0].model = probe.rightHand;

        const auto struck = StepTheAttack(hero, HeroSlot, tested.Context());

        EXPECT_TRUE(HasError(struck, probe.cut)) << ErrorOf(struck);
        EXPECT_EQ(tested.Arrows().size(), probe.arrows);
    }
}

// El choque lee la skill del lugar +0x89 en el registro propio (0x5205cd..0x5207d1): con la 0x33 la flecha 0xdb se
// apaga sin más; con la 0x34 se apaga y 0x51fe10 suelta sus chispas 0x5c5/2, que cortan en 0x51fe9e (CL11) después de
// los dos rand() de la primera. En Lorencia y en Devias, con el blanco a 50.
TEST(ArrowEffectsTest, TheSkillOfThePlaceDecidesTheHit)
{
    const int fireDraws = FireDraws();
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t skill : {std::uint8_t{0x33}, std::uint8_t{0x34}})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", la skill " << static_cast<int>(skill));
            ArrowWorld tested(map);
            tested.Target(VictimSlot, true, 50.0f);
            ActorState& hero = tested.Archer(ShortBow, ShortBow.action, VictimSlot);
            ASSERT_TRUE(StepTheAttack(hero, HeroSlot, tested.Context()).HasValue());
            const auto arrows = tested.Arrows();
            ASSERT_EQ(arrows.size(), 1u);
            ModelEffect& arrow = *arrows.front();
            arrow.byte89 = 3;
            tested.world.OwnCharacter().words6c[0] = static_cast<std::uint32_t>(skill) << 24;
            const CrtRandom before = tested.random;

            const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());

            EXPECT_FALSE(arrow.alive);
            constexpr int FirstSpark = 2;
            if (skill == 0x33)
            {
                ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
                EXPECT_EQ(DrawsSince(before.State(), tested.random), 1 + fireDraws + OwnRecord);
                EXPECT_EQ(arrow.life, 29);
            }
            else
            {
                EXPECT_TRUE(HasError(stepped, "0x51fe9e")) << ErrorOf(stepped);
                EXPECT_EQ(DrawsSince(before.State(), tested.random), 1 + fireDraws + OwnRecord + FirstSpark);
            }
        }
    }
}

// La flecha 0xdb con el subtipo 5 (la crea el hito 4) no corta en el paso: su fuego 0x5d9/0 lleva (luz · 0,1f;
// luz · 0,6f; luz · 0,3f) en vez del color de la luz dinámica (0x5240be..0x5240fa).
TEST(ArrowEffectsTest, TheFifthSubtypeBurnsBlue)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        ArrowWorld tested(map);
        ActorState& hero = tested.Archer(ShortBow, ShortBow.action, NoTarget);
        ASSERT_TRUE(StepTheAttack(hero, HeroSlot, tested.Context()).HasValue());
        const auto arrows = tested.Arrows();
        ASSERT_EQ(arrows.size(), 1u);
        ModelEffect& arrow = *arrows.front();
        arrow.subtype = 5;
        CrtRandom probe = tested.random;
        const float luminosity = Client::World::StepLuminosity(probe.Next(), arrow.life);

        const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const auto particles = tested.particles.All();
        const auto fire = std::ranges::find(particles, Client::World::FireParticle, &Client::World::Particle::type);
        ASSERT_NE(fire, particles.end());
        const auto times = [luminosity](float factor)
        { return static_cast<float>(static_cast<double>(luminosity) * static_cast<double>(factor)); };
        EXPECT_EQ(fire->light, glm::vec3(times(0.1f), times(0.6f), times(0.3f)));
        EXPECT_TRUE(arrow.alive);
    }
}

// Los misiles de los monstruos (0x5037d0 sin blanco y 0x5040a9 con blanco, este sin el 0x180): 0x53e2b0(c, o, 0, 0, 0,
// 0) va a 0x53d9a0 con las manos de +0x2b8/+0x2d8, sin el registro propio; con el arco 0xa12 en la izquierda sale la
// 0xdb del monstruo, con los 3.848 rand() de la salida del alta como única diferencia con las manos vacías. En
// Lorencia y en Devias.
TEST(ArrowEffectsTest, TheMonsterMissilesShootWithTheirHands)
{
    constexpr std::array<std::uint16_t, 4> Missiles{0x168, 0x175, 0x17a, 0x180};
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint16_t type : Missiles)
        {
            for (const bool withTarget : {true, false})
            {
                SCOPED_TRACE(::testing::Message() << "mapa " << map << ", el tipo " << type
                                                  << (withTarget ? ", con blanco" : ", sin blanco"));
                const auto shoot = [&](bool armed)
                {
                    auto tested = std::make_unique<ArrowWorld>(map);
                    ActorState& monster = tested->world.Actors()[OtherSlot];
                    monster = ActorState{};
                    monster.alive = true;
                    monster.type = type;
                    monster.byte88 = 2;
                    monster.word376 = 0xffff;
                    monster.position = {1000.0f, 1000.0f, 0.0f};
                    monster.animation.action = 1;
                    monster.byte381 = 14;
                    monster.attackTarget = withTarget ? static_cast<std::uint16_t>(HeroSlot) : NoTarget;
                    monster.hands[0].model = 0xffff;
                    monster.hands[1].model = armed ? std::uint16_t{0xa12} : std::uint16_t{0xffff};
                    const auto struck = StepTheAttack(monster, OtherSlot, tested->Context());
                    EXPECT_TRUE(struck.HasValue()) << ErrorOf(struck);
                    return tested;
                };
                const auto bare = shoot(false);
                const auto armed = shoot(true);

                const bool shoots = !withTarget || type != 0x180;
                EXPECT_EQ(DrawsSince(Seed, armed->random) - DrawsSince(Seed, bare->random), shoots ? ArrowCreation : 0);
                const auto arrows = armed->Arrows();
                ASSERT_EQ(arrows.size(), shoots ? 1u : 0u);
                if (!shoots)
                    continue;
                EXPECT_EQ(arrows.front()->type, 0xdb);
                EXPECT_EQ(arrows.front()->ownerContext, &armed->world.Actors()[OtherSlot]);
                EXPECT_EQ(arrows.front()->byte89, 0);
            }
        }
    }
}
} // namespace
