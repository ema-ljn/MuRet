#include "Scenes/ArrowEffects.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Skill.h"
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
#include "support/SameBytes.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
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
                                .worldClock = 0.0f,
                                .terrainHeight = terrainHeight,
                                .soundRequest = [this](std::int32_t sound) { sounds.push_back(sound); },
                                .send = [this](Client::Protocol::Outgoing packet)
                                { sent.push_back(std::move(packet)); }};
    }

    // Saca rand() hasta que el que sigue a los próximos `skipped` sea par (o impar con `odd`): el rand() & 1 de
    // 0x5201ed, después del de la luz y los del fuego del paso.
    void HitDrawIs(int skipped, bool odd)
    {
        const auto drawn = [this, skipped]()
        {
            CrtRandom probe = random;
            for (int draw = 0; draw < skipped; ++draw)
                static_cast<void>(probe.Next());
            return (probe.Next() & 1) != 0;
        };
        while (drawn() != odd)
            static_cast<void>(random.Next());
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
    std::vector<Client::Protocol::Outgoing> sent;
    std::function<double(float, float)> terrainHeight{};
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

// La lluvia de flechas (la skill 0x33, 0x53e741) sin blanco, en Lorencia y en Devias: un tiro de 0x53d9a0 (la 0xdb
// del arco corto, con el sonido 0x82 y la skill en [0x5767948]) y la cuenta de la tanda (+0xf04 uno más, con el
// registro propio abierto y cerrado), y sale sin el brillo de la 0xa25 aunque esté en +0x2b8. La flecha se llevó la
// cuenta de antes (0x51a1b6).
TEST(ArrowEffectsTest, TheArrowRainCountsTheVolley)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint16_t rightHand : {std::uint16_t{0xffff}, std::uint16_t{0xa25}})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", +0x2b8 en " << rightHand);
            ArrowWorld tested(map);
            tested.world.OwnCharacter().bytef04 = 9;
            ActorState& hero = tested.Archer(ShortBow, ShortBow.action, NoTarget);
            hero.byte38e = 0x33;
            hero.hands[0].model = rightHand;

            const auto struck = StepTheAttack(hero, HeroSlot, tested.Context());

            ASSERT_TRUE(struck.HasValue()) << struck.Error();
            EXPECT_EQ(DrawsSince(Seed, tested.random), CountingWithoutTarget + 2 * SkillRead + ShotReads + OwnRecord +
                                                           ArrowCreation + OwnRecord + SkillRead);
            EXPECT_EQ(tested.world.OwnCharacter().bytef04, 10);
            EXPECT_EQ(tested.world.Runtime().movement.lastShotSkill, 0x33u);
            EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x82});
            const auto arrows = tested.Arrows();
            ASSERT_EQ(arrows.size(), 1u);
            EXPECT_EQ(arrows.front()->type, 0xdb);
            EXPECT_EQ(arrows.front()->subtype, 0);
            EXPECT_EQ(arrows.front()->state, 0);
            EXPECT_EQ(arrows.front()->word8c, 9);
        }
    }
}

// La skill 0x2e (0x53df2a), con blanco y sin él, en Lorencia y en Devias: no sale flecha sino el efecto 0x13a con el
// blanco de dueño, +0x8a del que tira, el lugar y la bandera; vida 20, escala 1,8f, +0x64 en −2, la velocidad (0; −30;
// 0), la luz (0,3f; 0,8f; 1), la boca (−10; −80; 200) girada y la x de los ángulos en −30. Su joint 0x632/4 corta
// (0x51e01a, CL11), antes de la cuenta de la tanda.
TEST(ArrowEffectsTest, TheBlastSkillLeavesItsEffectAndCuts)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const bool withTarget : {true, false})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << (withTarget ? ", con blanco" : ", sin blanco"));
            ArrowWorld tested(map);
            tested.world.OwnCharacter().bytef04 = 9;
            ActorState& victim = tested.Target(VictimSlot, true, 340.0f);
            ActorState& hero = tested.Archer(ShortBow, ShortBow.action, withTarget ? VictimSlot : NoTarget);
            hero.byte38e = 0x2e;

            const auto struck = StepTheAttack(hero, HeroSlot, tested.Context());

            EXPECT_TRUE(HasError(struck, "0x51e01a")) << ErrorOf(struck);
            const int beforeTheShot =
                withTarget ? CountingWithTarget + SkillRead : CountingWithoutTarget + 2 * SkillRead;
            EXPECT_EQ(DrawsSince(Seed, tested.random), beforeTheShot + ShotReads + OwnRecord);
            EXPECT_EQ(tested.world.OwnCharacter().bytef04, 9);
            EXPECT_EQ(tested.world.Runtime().movement.lastShotSkill, 0x2eu);
            EXPECT_TRUE(tested.Arrows().empty());
            const auto effects = tested.world.Map().effects.Effects();
            const auto blast = std::ranges::find_if(effects, [](const ModelEffect& effect)
                                                    { return effect.alive && effect.type == 0x13a; });
            ASSERT_NE(blast, effects.end());
            EXPECT_EQ(blast->subtype, 0);
            EXPECT_EQ(blast->ownerContext, withTarget ? static_cast<void*>(&victim) : nullptr);
            EXPECT_EQ(blast->hasOwner, withTarget);
            EXPECT_EQ(blast->disturbanceState, hero.word8a);
            EXPECT_EQ(blast->state, 0);
            EXPECT_EQ(blast->life, 20);
            EXPECT_FLOAT_EQ(blast->speed, 1.0f);
            EXPECT_EQ(blast->transform.scale, std::bit_cast<float>(0x3fe66666u));
            EXPECT_EQ(blast->objectState.blendMesh, -2);
            EXPECT_EQ(blast->velocity, glm::vec3(0.0f, -30.0f, 0.0f));
            EXPECT_EQ(blast->light,
                      glm::vec3(std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3f4ccccdu), 1.0f));
            EXPECT_EQ(blast->transform.position, glm::vec3(990.0f, 920.0f, 200.0f));
            EXPECT_EQ(blast->transform.angles, glm::vec3(-30.0f, 0.0f, 0.0f));
        }
    }
}

// La skill 0x18 sin blanco (la bandera en 1, 0x53e2e0), en Lorencia y en Devias: tres flechas con +0x24 en a, a + 15 y
// a − 15, que vuelve a a; con 0xa24 o 0xa25 en +0x2b8, cuatro en a + 5, a + 15, a − 5 y a − 15, y +0x24 queda en a + 15
// (Q-C1218). Cada tiro con el registro propio de 0x53d9a0 y el de la salida del alta, las flechas con +0x88 en 1, y la
// cuenta de la tanda; con la 0xa25, el brillo corta después (0x53e642).
TEST(ArrowEffectsTest, TheFlaggedShotFansTheArrows)
{
    struct Case
    {
        std::uint16_t rightHand;
        std::vector<float> yaws;
        float lastYaw;
        bool cuts;
    };
    const std::array cases{Case{0xffff, {90.0f, 105.0f, 75.0f}, 90.0f, false},
                           Case{0xa24, {95.0f, 105.0f, 85.0f, 75.0f}, 105.0f, false},
                           Case{0xa25, {95.0f, 105.0f, 85.0f, 75.0f}, 105.0f, true}};
    for (const std::int32_t map : {0, 2})
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", +0x2b8 en " << probe.rightHand);
            ArrowWorld tested(map);
            tested.world.OwnCharacter().bytef04 = 9;
            tested.world.OwnCharacter().SetSkillAt(2, 0x18);
            ActorState& hero = tested.Archer(ShortBow, ShortBow.action, NoTarget);
            hero.byte38e = 0x18;
            hero.hands[0].model = probe.rightHand;
            hero.angles.z = 90.0f;

            const auto struck = StepTheAttack(hero, HeroSlot, tested.Context());

            const auto shots = static_cast<int>(probe.yaws.size());
            const int expected = CountingWithoutTarget + 2 * SkillRead + ShotReads +
                                 shots * (OwnRecord + ArrowCreation) + OwnRecord + (probe.cuts ? 0 : SkillRead);
            if (probe.cuts)
                EXPECT_TRUE(HasError(struck, "0x53e642")) << ErrorOf(struck);
            else
                EXPECT_TRUE(struck.HasValue()) << ErrorOf(struck);
            EXPECT_EQ(DrawsSince(Seed, tested.random), expected);
            EXPECT_EQ(hero.angles.z, probe.lastYaw);
            EXPECT_EQ(tested.world.OwnCharacter().bytef04, 10);
            const auto arrows = tested.Arrows();
            ASSERT_EQ(arrows.size(), probe.yaws.size());
            for (std::size_t shot = 0; shot < arrows.size(); ++shot)
            {
                SCOPED_TRACE(shot);
                EXPECT_EQ(arrows[shot]->type, 0xdb);
                EXPECT_EQ(arrows[shot]->transform.angles.z, probe.yaws[shot]);
                EXPECT_EQ(arrows[shot]->state, 1);
                EXPECT_EQ(arrows[shot]->subtype, 0);
                EXPECT_EQ(arrows[shot]->byte89, 2);
                EXPECT_EQ(arrows[shot]->word8c, 9);
            }
        }
    }
}

// La flecha de la bandera choca por 0x5201ed, en Lorencia y en Devias, con la ballesta (la 0xdc, que solo choca): con
// un rand() par, el dueño héroe y la skill 0x18 del lugar, 0x517a20 manda el C1 10 con el monstruo a 40 de la flecha
// (la celda 9; 9, la tanda +0x8c y su clave) y la flecha se apaga con el sonido rand() % 4 + 0x4b (CL10). Con un rand()
// impar, o con otro dueño, no hace nada más y la flecha sigue.
TEST(ArrowEffectsTest, TheFlaggedArrowSendsTheHits)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::string_view variant : {"par", "impar", "otro dueño"})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", " << variant);
            ArrowWorld tested(map);
            tested.world.OwnCharacter().bytef04 = 9;
            tested.world.OwnCharacter().SetSkillAt(2, 0x18);
            ActorState& hero = tested.Archer(Crossbow, Crossbow.action, NoTarget);
            hero.byte38e = 0x18;
            ASSERT_TRUE(StepTheAttack(hero, HeroSlot, tested.Context()).HasValue());
            const auto arrows = tested.Arrows();
            ASSERT_EQ(arrows.size(), 3u);
            ModelEffect& arrow = *arrows.front();
            ASSERT_EQ(arrow.type, 0xdc);
            ASSERT_EQ(arrow.transform.position, glm::vec3(990.0f, 940.0f, 135.0f));
            ActorState& monster = tested.Target(VictimSlot, true, 0.0f);
            monster.key = 0x0789;
            monster.position = {990.0f, 900.0f, 0.0f};
            if (variant == "otro dueño")
                arrow.ownerContext = &monster;
            tested.HitDrawIs(1, variant == "impar");
            tested.sounds.clear();
            const CrtRandom before = tested.random;

            const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            if (variant != "par")
            {
                EXPECT_EQ(DrawsSince(before.State(), tested.random), 1 + 1);
                EXPECT_TRUE(arrow.alive);
                EXPECT_TRUE(tested.sent.empty());
                EXPECT_TRUE(tested.sounds.empty());
                continue;
            }
            EXPECT_EQ(DrawsSince(before.State(), tested.random), 1 + 1 + OwnRecord + OwnRecord + 2 + 1);
            EXPECT_FALSE(arrow.alive);
            ASSERT_EQ(tested.sounds.size(), 1u);
            EXPECT_GE(tested.sounds[0], 0x4b);
            EXPECT_LE(tested.sounds[0], 0x4e);
            ASSERT_EQ(tested.sent.size(), 1u);
            Client::Protocol::Skill::SkillHitsSend expected{.skill = 0x18, .x = 9, .y = 9, .volley = 9, .hits = 1};
            expected.keys[0] = 0x0789;
            EXPECT_TRUE(Client::Tests::SameBytes(tested.sent[0].packet.Bytes(),
                                                 Client::Protocol::Skill::WriteSkillHits(expected).packet.Bytes()));
        }
    }
}

// La skill 0x34 (que el paso del ataque deja llegar desde +0x381 en 4: en 3 corta antes en el efecto 0x621, 0x4f9ecb;
// test_attack_frame_skills.cpp), por 0x53e2b0 directo, en Lorencia y en Devias: el sonido 0x55, la flecha 0xdb con el
// subtipo 2 y, en su alta, la estela 0x105 con la flecha de dueño (vida 100, escala 12, +0x64 en 0, la partida en la
// flecha), cuyo joint 0x610 corta (0x51a08a, CL11).
TEST(ArrowEffectsTest, ThePiercingShotLeavesItsTrail)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        ArrowWorld tested(map);
        ActorState& hero = tested.Archer(ShortBow, ShortBow.action, NoTarget);

        const auto fired = Client::Scenes::FireTheArrow(hero, nullptr, HeroSlot, 3, 0, 0x34, tested.Context());

        EXPECT_TRUE(HasError(fired, "0x51a08a")) << ErrorOf(fired);
        EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord);
        EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x55});
        EXPECT_EQ(tested.world.Runtime().movement.lastShotSkill, 0x34u);
        const auto arrows = tested.Arrows();
        ASSERT_EQ(arrows.size(), 1u);
        const ModelEffect& arrow = *arrows.front();
        EXPECT_EQ(arrow.subtype, 2);
        EXPECT_EQ(arrow.byte89, 3);
        const auto effects = tested.world.Map().effects.Effects();
        const auto trail = std::ranges::find_if(effects, [](const ModelEffect& effect)
                                                { return effect.alive && effect.type == 0x105; });
        ASSERT_NE(trail, effects.end());
        EXPECT_EQ(trail->ownerContext, static_cast<const void*>(&arrow));
        EXPECT_EQ(trail->subtype, 0);
        EXPECT_EQ(trail->disturbanceState, 0xffff);
        EXPECT_EQ(trail->life, 100);
        EXPECT_EQ(trail->transform.scale, 12.0f);
        EXPECT_EQ(trail->objectState.blendMesh, 0);
        EXPECT_EQ(trail->transform.position, arrow.transform.position);
        EXPECT_EQ(trail->start, arrow.transform.position);
    }
}

// Con un solo lugar libre en la tabla, la flecha de la 0x34 lo toma y la estela no nace: +0x100 en 0, +0x88 en 1, la
// salida del alta y la cuenta de la tanda. En su paso, con un rand() par y la 0x34 en el lugar, choca con el monstruo
// (el C1 10) y sigue viva con +0x100 en 2 hasta el joint 0x60f/6, que corta (0x52055f, CL11); en el paso siguiente
// gasta uno de +0x100 sin chocar. En Lorencia y en Devias.
TEST(ArrowEffectsTest, WithTheTableFullThePiercingArrowWaits)
{
    const int fireDraws = FireDraws();
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        ArrowWorld tested(map);
        tested.world.OwnCharacter().bytef04 = 9;
        tested.world.OwnCharacter().SetSkillAt(3, 0x34);
        auto effects = tested.world.Map().effects.Effects();
        for (std::size_t slot = 0; slot + 1 < effects.size(); ++slot)
        {
            effects[slot].alive = true;
            effects[slot].type = 0x61f;
        }
        ActorState& hero = tested.Archer(ShortBow, ShortBow.action, NoTarget);

        const auto fired = Client::Scenes::FireTheArrow(hero, nullptr, HeroSlot, 3, 0, 0x34, tested.Context());

        ASSERT_TRUE(fired.HasValue()) << fired.Error();
        EXPECT_EQ(DrawsSince(Seed, tested.random), 3 * OwnRecord);
        EXPECT_EQ(tested.world.OwnCharacter().bytef04, 10);
        ModelEffect& arrow = effects.back();
        ASSERT_TRUE(arrow.alive);
        ASSERT_EQ(arrow.type, 0xdb);
        EXPECT_EQ(arrow.subtype, 2);
        EXPECT_EQ(arrow.state, 1);
        EXPECT_EQ(arrow.dword100, 0);
        EXPECT_EQ(arrow.word8c, 9);
        ActorState& monster = tested.Target(VictimSlot, true, 0.0f);
        monster.key = 0x0789;
        monster.position = {990.0f, 900.0f, 0.0f};
        tested.HitDrawIs(1 + fireDraws, false);
        const CrtRandom before = tested.random;

        const auto hit = StepArrow(arrow, nullptr, tested.StepContext());

        EXPECT_TRUE(HasError(hit, "0x52055f")) << ErrorOf(hit);
        EXPECT_EQ(DrawsSince(before.State(), tested.random), 1 + fireDraws + 1 + OwnRecord + OwnRecord + 2);
        EXPECT_TRUE(arrow.alive);
        EXPECT_EQ(arrow.dword100, 2);
        ASSERT_EQ(tested.sent.size(), 1u);
        Client::Protocol::Skill::SkillHitsSend expected{.skill = 0x34, .x = 9, .y = 9, .volley = 9, .hits = 1};
        expected.keys[0] = 0x0789;
        EXPECT_TRUE(Client::Tests::SameBytes(tested.sent[0].packet.Bytes(),
                                             Client::Protocol::Skill::WriteSkillHits(expected).packet.Bytes()));

        tested.HitDrawIs(1 + fireDraws, false);
        const CrtRandom waiting = tested.random;
        const auto waited = StepArrow(arrow, nullptr, tested.StepContext());
        ASSERT_TRUE(waited.HasValue()) << waited.Error();
        EXPECT_EQ(DrawsSince(waiting.State(), tested.random), 1 + fireDraws + 1 + OwnRecord);
        EXPECT_EQ(arrow.dword100, 1);
        EXPECT_EQ(arrow.life, 29);
        EXPECT_EQ(tested.sent.size(), 1u);
    }
}

// La flecha 0xdb con el subtipo 3 o 4 (la de los objetos del asedio, CL9.5), por 0x518cf0 directo, en Lorencia, en
// Devias y en el 30: vida 40, escala 1,5, +0xe4 de (rand() % 100 + 50) · 0x3d888889, la partida en el héroe y la
// velocidad y la x de los ángulos de sus rand() (dos con el 3, uno con el 4), y la salida del alta.
TEST(ArrowEffectsTest, TheSiegeArrowsAreBornFromTheirRandoms)
{
    for (const std::int32_t map : {0, 2, 30})
    {
        for (const std::int32_t subtype : {3, 4})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", el subtipo " << subtype);
            ArrowWorld tested(map);
            tested.world.OwnCharacter().bytef04 = 9;
            const glm::vec3 from{2000.0f, 3000.0f, 400.0f};
            const Client::World::ModelEffectArguments arguments{
                .type = 0xdb, .position = from, .light = glm::vec3(1.0f), .subtype = subtype};

            const auto created = Client::Scenes::CreateTheArrowEffect(arguments, tested.world, tested.random);

            ASSERT_TRUE(created.HasValue()) << created.Error();
            EXPECT_EQ(DrawsSince(Seed, tested.random), (subtype == 3 ? 3 : 2) + OwnRecord);
            const auto arrows = tested.Arrows();
            ASSERT_EQ(arrows.size(), 1u);
            const ModelEffect& arrow = *arrows.front();
            CrtRandom probe(Seed);
            const int turn = (probe.Next() % 100) + 50;
            EXPECT_EQ(arrow.turnDirection,
                      static_cast<float>(turn * static_cast<double>(std::bit_cast<float>(0x3d888889u))));
            const float speed = subtype == 3 ? -static_cast<float>((probe.Next() % 30) + 50)
                                             : -static_cast<float>((probe.Next() % 15) + 55);
            const float pitch = subtype == 3 ? -static_cast<float>((probe.Next() % 20) + 45) : -10.0f;
            EXPECT_EQ(arrow.velocity, glm::vec3(0.0f, speed, 0.0f));
            EXPECT_EQ(arrow.transform.angles, glm::vec3(pitch, 0.0f, 0.0f));
            EXPECT_EQ(arrow.life, 40);
            EXPECT_EQ(arrow.transform.scale, 1.5f);
            EXPECT_EQ(arrow.objectState.blendMesh, 1);
            EXPECT_EQ(arrow.start, tested.Hero().position);
            EXPECT_EQ(arrow.transform.position, glm::vec3(1990.0f, 2940.0f, 535.0f));
            EXPECT_EQ(arrow.word8c, 9);
        }
    }
}

// El paso de la flecha 0xdb/3 y /4 (0x52413a), en Lorencia, en Devias y en el 30: con el 3, +0xe4 se suma a la x de los
// ángulos, que queda en 50 como mucho; con la celda sin el 0x08, el terreno más alto que la z la deja en la altura + 10
// y frena la flecha (más bajo, nada; con el 0x08, no se pide la altura). El fuego 0x5d9/5 corta (0x5241e9, CL11), sin
// rand() y sin el choque.
TEST(ArrowEffectsTest, TheSiegeArrowsFallOnTheGround)
{
    struct Case
    {
        std::string_view name;
        std::int32_t subtype;
        std::uint16_t attribute;
        double height;
        float pitch;
        float z;
        bool stopped;
    };
    const std::array cases{Case{"3, el suelo arriba", 3, 0x0000, 600.0, 50.0f, 610.0f, true},
                           Case{"3, el suelo abajo", 3, 0x0004, 100.0, 50.0f, 535.0f, false},
                           Case{"4, el 0x08", 4, 0x0108, 600.0, 50.0f, 535.0f, false},
                           Case{"4, el suelo arriba", 4, 0x0000, 600.0, 50.0f, 610.0f, true}};
    for (const std::int32_t map : {0, 2, 30})
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", " << probe.name);
            ArrowWorld tested(map);
            auto attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
            attributes->cells.fill(probe.attribute);
            tested.world.Map().terrain.attributes = std::move(attributes);
            int heights = 0;
            tested.terrainHeight = [&heights, &probe](float, float)
            {
                ++heights;
                return probe.height;
            };
            const Client::World::ModelEffectArguments arguments{.type = 0xdb,
                                                                .position = {2000.0f, 3000.0f, 400.0f},
                                                                .light = glm::vec3(1.0f),
                                                                .subtype = probe.subtype};
            ASSERT_TRUE(Client::Scenes::CreateTheArrowEffect(arguments, tested.world, tested.random).HasValue());
            ModelEffect& arrow = *tested.Arrows().front();
            arrow.transform.angles.x = probe.subtype == 3 ? 48.0f : 60.0f;
            arrow.turnDirection = 5.0f;
            const glm::vec3 velocity = arrow.velocity;
            const CrtRandom before = tested.random;

            const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());

            EXPECT_TRUE(HasError(stepped, "0x5241e9")) << ErrorOf(stepped);
            EXPECT_EQ(DrawsSince(before.State(), tested.random), 1);
            EXPECT_EQ(arrow.transform.angles.x, probe.pitch);
            EXPECT_EQ(arrow.transform.position.z, probe.z);
            EXPECT_EQ(arrow.velocity, probe.stopped ? glm::vec3(0.0f) : velocity);
            EXPECT_EQ(heights, (probe.attribute & 0x08) != 0 ? 0 : 1);
            EXPECT_TRUE(arrow.alive);
            EXPECT_EQ(arrow.life, 40);
        }
    }
}

// La flecha 0xe1/1 (la de la fortaleza y Crywolf; su alta corta en el joint 0x610/0xd, 0x51c713, con la flecha ya en
// la tabla), en Lorencia y en Devias: sus dos sprites 0x5d5, la luz dinámica y la y de los ángulos 60 más; con más de
// 25 de vida y par, los tres rand() del punto y la 0x60f/5 corta (0x526961, CL11); si no, el choque y la cola.
TEST(ArrowEffectsTest, TheFortressArrowSparklesOnEvenLives)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::int32_t life : {26, 27, 24})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", vida " << life);
            ArrowWorld tested(map);
            const Client::World::ModelEffectArguments arguments{
                .type = 0xe1, .position = {2000.0f, 3000.0f, 400.0f}, .light = glm::vec3(1.0f), .subtype = 1};
            EXPECT_TRUE(
                HasError(Client::Scenes::CreateTheArrowEffect(arguments, tested.world, tested.random), "0x51c713"));
            EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
            const auto arrows = tested.Arrows();
            ASSERT_EQ(arrows.size(), 1u);
            ModelEffect& arrow = *arrows.front();
            arrow.life = life;
            arrow.transform.angles.y = 10.0f;

            const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());

            const bool sparkles = life > 25 && life % 2 == 0;
            if (sparkles)
                EXPECT_TRUE(HasError(stepped, "0x526961")) << ErrorOf(stepped);
            else
                EXPECT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
            EXPECT_EQ(DrawsSince(Seed, tested.random), sparkles ? 1 + 3 : 1);
            EXPECT_EQ(arrow.transform.angles.y, 70.0f);
            EXPECT_EQ(arrow.life, sparkles ? life : life - 1);
            EXPECT_EQ(std::ranges::count_if(tested.sprites.Sprites(), [](const auto& sprite) { return sprite.alive; }),
                      2);
        }
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
