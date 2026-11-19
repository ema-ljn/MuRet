#include "Scenes/ArrowEffects.h"
#include "Network/Crypto/Xor32.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Skill.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Scenes/GameDrawTail.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/AttackStep.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/FaunaJointCreation.h"
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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
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
                                    .soundRequest = [this](std::int32_t sound) { sounds.push_back(sound); },
                                    .soundAt =
                                        [this](std::int32_t sound, const glm::vec3& origin)
                                    {
                                        sounds.push_back(sound);
                                        soundOrigins.push_back(origin);
                                    }};
    }

    ArrowStepContext StepContext()
    {
        return ArrowStepContext{
            .world = world,
            .random = random,
            .particles = &particles,
            .sprites = &sprites,
            .lighting = lighting.get(),
            .worldClock = 0.0f,
            .terrainHeight = terrainHeight        ? terrainHeight
                             : ground.has_value() ? std::function<double(float, float)>(
                                                        [this](float, float) { return ground.value_or(0.0); })
                                                  : std::function<double(float, float)>{},
            .soundRequest = [this](std::int32_t sound) { sounds.push_back(sound); },
            .send = [this](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); }};
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

    // Llena los 3.000 lugares del pool con fuegos 0x5d9/0 (rand() aparte): las partículas que siguen no se crean ni
    // sacan rand(), como en 0x53fe70 con el pool lleno.
    void FillTheParticles()
    {
        CrtRandom scratch(Seed);
        for (std::size_t made = 0; made <= Client::World::ParticleCount; ++made)
        {
            const auto created = particles.CreateChecked(Client::World::FireParticle, glm::vec3(0.0f), glm::vec3(0.0f),
                                                         glm::vec3(1.0f), 0, 1.0f, 0.0f, scratch);
            if (!created.HasValue() || !created.Value().has_value())
                return;
        }
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
    std::vector<glm::vec3> soundOrigins;
    std::vector<Client::Protocol::Outgoing> sent;
    std::function<double(float, float)> terrainHeight{};
    // La altura del terreno de 0x5f93c0 en cualquier punto; sin valor, el contexto va sin ella.
    std::optional<double> ground = 0.0;
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
                EXPECT_EQ(tested.world.Runtime().lastShotSkill, 0u);
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
// la 0x11c (4/20, después de su rand() % 360) y la 0xf9 (4/21); en el paso, la 0xe1 (4/6) en sus partículas
// 0x608..0x60a, después de sus dos sprites y de los rand() del punto.

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
        Case{"4/20", 0xffff, 0x814, "0x51c713", 1, "", 0}, Case{"4/21", 0xffff, 0x815, "0x51c56c", 0, "", 0},
        Case{"4/6", 0xffff, 0x806, "", 0, "0x526a8b", 1 + 4}};
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

// 0x524300..0x52434e (la 0xe2, 4/14) y 0x526cd0..0x526d5f (la 0xe3, 4/13): después del rand() de la luz, cuatro
// veces el punto al azar (tres rand()) y la burbuja 0x607/1 de 1 con los ángulos de la flecha (la luz 1 de su alta,
// 0x542d96, y dos rand()). La 0xe3 sigue con su humo 0x5f2/0 en la posición de la flecha.
TEST(ArrowEffectsTest, TheWaterArrowsLeaveFourBubbles)
{
    for (const Weapon& weapon : {Weapon{"4/14", 0x80e, 0xffff, 0x2e, 0xe2}, Weapon{"4/13", 0x80d, 0xffff, 0x2e, 0xe3}})
    {
        SCOPED_TRACE(weapon.name);
        ArrowWorld tested(0);
        ActorState& hero = tested.Archer(weapon, weapon.action, NoTarget);
        ASSERT_TRUE(StepTheAttack(hero, HeroSlot, tested.Context()).HasValue());
        const auto arrows = tested.Arrows();
        ASSERT_EQ(arrows.size(), 1u);
        ModelEffect& arrow = *arrows.front();
        arrow.life = 40;
        tested.ground = 0.0;
        for (auto& particle : tested.particles.All())
            particle.alive = false;
        const glm::vec3 position = arrow.transform.position;
        const glm::vec3 angles = arrow.transform.angles;
        const CrtRandom before = tested.random;

        const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        auto twin = std::make_unique<Client::World::Particles>();
        CrtRandom expected = before;
        static_cast<void>(expected.Next());
        for (std::size_t index = 0; index < 4; ++index)
        {
            SCOPED_TRACE(index);
            expected.Discard(3);
            const Client::World::Particle& bubble = tested.particles.All()[index];
            const auto replayed =
                twin->CreateChecked(0x607, bubble.start, angles, glm::vec3(0.25f), 1, 1.0f, 0.0f, expected);
            ASSERT_TRUE(replayed.HasValue() && replayed.Value() == std::optional<std::size_t>{index});
            const Client::World::Particle& reference = twin->All()[index];
            EXPECT_TRUE(bubble.alive);
            EXPECT_EQ(bubble.type, 0x607);
            EXPECT_EQ(bubble.subtype, 1);
            EXPECT_EQ(bubble.angles, angles);
            EXPECT_EQ(bubble.light, glm::vec3(1.0f));
            EXPECT_EQ(bubble.life, reference.life);
            EXPECT_EQ(bubble.scale, reference.scale);
            for (int axis = 0; axis < 3; ++axis)
            {
                EXPECT_GE(bubble.start[axis], position[axis] - 8.0f);
                EXPECT_LE(bubble.start[axis], position[axis] + 7.0f);
            }
        }
        if (weapon.arrow == 0xe2)
        {
            EXPECT_FALSE(tested.particles.All()[4].alive);
            EXPECT_EQ(tested.random.State(), expected.State());
            continue;
        }
        const Client::World::Particle& smoke = tested.particles.All()[4];
        EXPECT_TRUE(smoke.alive);
        EXPECT_EQ(smoke.type, 0x5f2);
        EXPECT_EQ(smoke.subtype, 0);
        EXPECT_EQ(smoke.start, position);
    }
}

// La caída de la flecha 0xe2 (4/14, 0x52435d..0x52443f), con el pool de partículas lleno para que sus burbujas 0x607/1
// (CL11) no se creen: el paso saca el rand() de la luz y los tres del punto de cada una de las cuatro burbujas. La z
// suma +0xe4 (−10 en el alta) y +0xe4 pierde 1; con el terreno (0x5f93c0) por encima de la z, la z al terreno, +0xe4
// por −0,4f y la y de la velocidad por 0,4f; con el terreno igual a la z no rebota. En la última vida suelta las
// chispas de 0x51fe10 (40 rand() con el pool lleno) y, con el héroe de dueño y sin el subtipo 0x63, corta en 0x517a20
// (0x524405, hito 4); sin la altura del terreno corta en 0x524382. En Lorencia, en Devias y en Icarus (10).

// 0x51a173, 0x51a24a y 0x51bf35: la flecha de la 4/16, la 4/18 y la 4/19 deja un 0x618/5 con ella de dueño, desde su
// posición (la z + 130, que también queda en su +0x40), con sus ángulos y ancho 100, sin rand(): velocidad 0, vida
// 999.999.999, 10 puntos y la luz (0,5; 0,1; 1) (0x532238..0x53230e). En el paso sigue el +0x40 de la flecha y, con
// la flecha apagada, se apaga sin la cola ni la vida (0x53820e).
TEST(ArrowEffectsTest, TheAdvancedArrowsLeaveTheirBeam)
{
    for (const auto& [name, right] : {std::pair{"4/16", std::uint16_t{0x810}}, std::pair{"4/18", std::uint16_t{0x812}},
                                      std::pair{"4/19", std::uint16_t{0x813}}})
    {
        SCOPED_TRACE(name);
        ArrowWorld tested(0);
        const Weapon weapon{name, right, 0xffff, 0x2e, 0};
        ActorState& hero = tested.Archer(weapon, weapon.action, NoTarget);

        const auto struck = StepTheAttack(hero, HeroSlot, tested.Context());

        ASSERT_TRUE(struck.HasValue()) << struck.Error();
        const auto arrows = tested.Arrows();
        ASSERT_EQ(arrows.size(), 1u);
        ModelEffect& arrow = *arrows.front();
        auto& joints = tested.world.DrawTail().joints;
        const auto beam = std::ranges::find_if(joints, [](const auto& joint) { return joint.alive; });
        ASSERT_NE(beam, joints.end());
        EXPECT_EQ(beam->type, 0x618);
        EXPECT_EQ(beam->subtype, 5);
        EXPECT_EQ(beam->ownerContext, &arrow);
        EXPECT_EQ(beam->ownerKind, Client::Scenes::JointOwnerKind::ModelEffect);
        EXPECT_EQ(beam->position, arrow.point40);
        EXPECT_EQ(beam->angles, arrow.transform.angles);
        EXPECT_EQ(beam->width, 100.0f);
        EXPECT_EQ(beam->speed, 0.0f);
        EXPECT_EQ(beam->life, 999999999);
        EXPECT_EQ(beam->maximumCount, 10);
        EXPECT_EQ(beam->light, glm::vec3(0.5f, std::bit_cast<float>(0x3dcccccdu), 1.0f));

        arrow.point40 = {1.0f, 2.0f, 3.0f};
        ASSERT_TRUE(Client::Scenes::StepJoints(joints, tested.random).HasValue());
        EXPECT_EQ(beam->position, glm::vec3(1.0f, 2.0f, 3.0f));
        EXPECT_EQ(beam->life, 999999998);
        EXPECT_EQ(beam->count, 1);

        arrow.alive = false;
        ASSERT_TRUE(Client::Scenes::StepJoints(joints, tested.random).HasValue());
        EXPECT_FALSE(beam->alive);
        EXPECT_EQ(beam->life, 999999998);
        EXPECT_EQ(beam->count, 1);
    }
}

// 0x51a3d0..0x51a417: la flecha 0xf8 de la 4/17 deja cuatro 0x60f/0xb con ella de dueño, desde su partida (+0x17c),
// con los ángulos (0, 0, z), ancho 50 de pedido (el alta pone 30) y el skill 1 en el segundo; cada uno saca su
// rand() % 3000 (vx). Con el pool lleno, ninguno y sin rand().
TEST(ArrowEffectsTest, TheCelestialArrowLeavesFourWanderingJoints)
{
    const Weapon weapon{"4/17", 0xffff, 0x811, 0x2e, 0};
    auto full = std::make_unique<ArrowWorld>(0);
    for (auto& joint : full->world.DrawTail().joints)
        joint.alive = true;
    ActorState& fullHero = full->Archer(weapon, weapon.action, NoTarget);
    ASSERT_TRUE(StepTheAttack(fullHero, HeroSlot, full->Context()).HasValue());

    auto tested = std::make_unique<ArrowWorld>(0);
    ActorState& hero = tested->Archer(weapon, weapon.action, NoTarget);

    const auto struck = StepTheAttack(hero, HeroSlot, tested->Context());

    ASSERT_TRUE(struck.HasValue()) << struck.Error();
    EXPECT_EQ(DrawsSince(Seed, tested->random), DrawsSince(Seed, full->random) + 4);
    const auto arrows = tested->Arrows();
    ASSERT_EQ(arrows.size(), 1u);
    const ModelEffect& arrow = *arrows.front();
    const auto& joints = tested->world.DrawTail().joints;
    for (std::size_t slot = 0; slot < 4; ++slot)
    {
        SCOPED_TRACE(slot);
        const auto& joint = joints[slot];
        ASSERT_TRUE(joint.alive);
        EXPECT_EQ(joint.type, 0x60f);
        EXPECT_EQ(joint.subtype, 0xb);
        EXPECT_EQ(joint.ownerContext, &arrow);
        EXPECT_EQ(joint.ownerKind, Client::Scenes::JointOwnerKind::ModelEffect);
        EXPECT_EQ(joint.position, arrow.start);
        EXPECT_EQ(joint.targetPosition, arrow.start);
        EXPECT_EQ(joint.angles, glm::vec3(0.0f, 0.0f, arrow.transform.angles.z));
        EXPECT_EQ(joint.skill, slot == 1 ? 1 : 0);
        EXPECT_EQ(joint.width, 30.0f);
        EXPECT_EQ(joint.life, 30);
        EXPECT_EQ(joint.maximumCount, 15);
    }
    EXPECT_FALSE(joints[4].alive);

    // El alta sola (0x51a2bd..0x51a417, sin el ataque): los cuatro rand() % 3000 de vx (0x533c06), en orden, antes de
    // los de 0x51a1b0.
    auto alone = std::make_unique<ArrowWorld>(0);
    glm::vec3 angles{15.0f, 25.0f, 35.0f};
    const Client::World::ModelEffectArguments args{
        .type = 0xf8, .position = {10.0f, 20.0f, 30.0f}, .angles = angles, .light = {1.0f, 1.0f, 1.0f}, .subtype = 0};
    const auto made = Client::Scenes::CreateTheArrowEffect(args, alone->world, alone->random, &angles);
    ASSERT_TRUE(made.HasValue()) << ErrorOf(made);
    EXPECT_EQ(DrawsSince(Seed, alone->random), 4 + OwnRecord);
    CrtRandom expected(Seed);
    for (std::size_t slot = 0; slot < 4; ++slot)
    {
        SCOPED_TRACE(slot);
        const auto& joint = alone->world.DrawTail().joints[slot];
        EXPECT_EQ(joint.subtype, 0xb);
        EXPECT_EQ(joint.velocity, glm::vec3(static_cast<float>(expected.Next() % 3000), 0.0f, 0.0f));
    }
}

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
            EXPECT_EQ(tested.world.Runtime().lastShotSkill, 0x33u);
            EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x82});
            ASSERT_EQ(tested.soundOrigins.size(), 1u);
            EXPECT_EQ(tested.soundOrigins.front(), hero.position);
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
            EXPECT_EQ(tested.world.Runtime().lastShotSkill, 0x2eu);
            EXPECT_TRUE(tested.Arrows().empty());
            const auto effects = tested.world.Map().effects.Effects();
            const auto blast = std::ranges::find_if(effects, [](const ModelEffect& effect)
                                                    { return effect.alive && effect.type == 0x13a; });
            ASSERT_NE(blast, effects.end());
            EXPECT_EQ(blast->subtype, 0);
            EXPECT_EQ(blast->ownerContext, withTarget ? static_cast<void*>(&victim) : nullptr);
            EXPECT_EQ(blast->hasOwner, withTarget);
            EXPECT_EQ(blast->disturbanceState, static_cast<std::uint16_t>(hero.word8a));
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
// cuenta de la tanda; con la 0xa25, sigue el brillo de H3 (0x53e642).

TEST(ArrowEffectsTest, TheFlaggedShotFansTheArrows)
{
    struct Case
    {
        std::uint16_t rightHand;
        std::vector<float> yaws;
        float lastYaw;
        bool shines;
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
                                 shots * (OwnRecord + ArrowCreation) + OwnRecord + SkillRead +
                                 (probe.shines ? 15 * (2 + 5) : 0);
            // El fixture no ofrece joints: los quince intentos del brillo conservan los dos rand() del caller y
            // los cinco de cada partícula; la prueba de capacidad inferior cubre también los joints disponibles.
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

// 0x53e2b0 con la skill 0x34: dispara 0xdb/2, crea su estela 0x105/0 y conserva la tanda de cuatro
// intentos de joint 0x610/0..3 y el sonido 0x7a con cualquier capacidad (0x51a08a..0x51a101).

TEST(ArrowEffectsTest, ThePiercingShotLeavesItsTrail)
{
    for (const std::size_t freeJoints : std::array<std::size_t, 3>{0, 2, 4})
        for (const std::int32_t map : {0, 2})
        {
            SCOPED_TRACE(map);
            ArrowWorld tested(map);
            ActorState& hero = tested.Archer(ShortBow, ShortBow.action, NoTarget);
            auto& joints = tested.world.DrawTail().joints;
            for (std::size_t slot = 0; slot < joints.size(); ++slot)
                joints[slot].alive = slot >= freeJoints;

            const auto fired = Client::Scenes::FireTheArrow(hero, nullptr, HeroSlot, 3, 0, 0x34, tested.Context());

            ASSERT_TRUE(fired.HasValue()) << ErrorOf(fired);
            EXPECT_EQ(DrawsSince(Seed, tested.random), 3 * OwnRecord);
            EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x55});
            ASSERT_EQ(tested.soundOrigins.size(), 1u);
            EXPECT_EQ(tested.soundOrigins.front(), hero.position);
            EXPECT_EQ(tested.world.Runtime().lastShotSkill, 0x34u);
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
            EXPECT_EQ(trail->transform.scale, 1.0f);
            EXPECT_EQ(trail->objectState.blendMesh, 0);
            EXPECT_EQ(trail->transform.position, arrow.transform.position);
            EXPECT_EQ(trail->start, arrow.transform.position);
            EXPECT_EQ(trail->owner.effect, &arrow);
            EXPECT_EQ(tested.world.Runtime().lastSoundId, 0x7a);
            EXPECT_EQ(arrow.dword100, 0);
            EXPECT_EQ(arrow.state, 1);
            EXPECT_EQ(arrow.word8c, 0);
            EXPECT_EQ(tested.world.OwnCharacter().bytef04, 1);
            EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
            for (std::size_t slot = 0; slot < freeJoints; ++slot)
            {
                EXPECT_TRUE(joints[slot].alive);
                EXPECT_EQ(joints[slot].type, 0x610);
                EXPECT_EQ(joints[slot].subtype, static_cast<std::int32_t>(slot));
                EXPECT_EQ(joints[slot].ownerKind, Client::Scenes::JointOwnerKind::ModelEffect);
                EXPECT_EQ(joints[slot].ownerContext, &*trail);
                EXPECT_EQ(joints[slot].width, 20.0f);
                EXPECT_EQ(joints[slot].life, 30);
                EXPECT_EQ(joints[slot].key, 0);
                EXPECT_EQ(joints[slot].skill, 0);
                EXPECT_EQ(joints[slot].maximumCount, 50);
                EXPECT_EQ(joints[slot].velocity, (glm::vec3{18, 0, 0}));
                EXPECT_EQ(joints[slot].light, glm::vec3(0.5f));
            }
        }
}

// 0x52055f: la flecha 0x34 choca con el monstruo, manda C1 10 e intenta 0x60f/6.
// La pausa de dos pasos y el impacto son los mismos con la estela activa o sin hueco para crearla.

TEST(ArrowEffectsTest, ThePiercingArrowHitsAndWaitsWithOrWithoutATrailSlot)
{
    const int fireDraws = FireDraws();
    struct Case
    {
        std::int32_t map;
        bool full;
    };
    constexpr std::array cases{Case{0, false}, Case{0, true}, Case{2, false}, Case{2, true}};
    for (const auto [map, full] : cases)
    {
        SCOPED_TRACE(testing::Message() << map << "/" << full);
        ArrowWorld tested(map);
        tested.world.OwnCharacter().bytef04 = 9;
        tested.world.OwnCharacter().SetSkillAt(3, 0x34);
        auto effects = tested.world.Map().effects.Effects();
        for (std::size_t slot = 0; full && slot + 1 < effects.size(); ++slot)
        {
            effects[slot].alive = true;
            effects[slot].type = 0x61f;
        }
        ActorState& hero = tested.Archer(ShortBow, ShortBow.action, NoTarget);

        const auto fired = Client::Scenes::FireTheArrow(hero, nullptr, HeroSlot, 3, 0, 0x34, tested.Context());

        ASSERT_TRUE(fired.HasValue()) << fired.Error();
        EXPECT_EQ(DrawsSince(Seed, tested.random), 3 * OwnRecord);
        EXPECT_EQ(tested.world.OwnCharacter().bytef04, 10);
        const auto arrows = tested.Arrows();
        ASSERT_EQ(arrows.size(), 1u);
        ModelEffect& arrow = *arrows.front();
        if (!full)
        {
            ASSERT_EQ(effects[1].type, 0x105);
            ASSERT_EQ(effects[1].owner.effect, &arrow);
        }
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

        ASSERT_TRUE(hit.HasValue()) << hit.Error();
        EXPECT_EQ(DrawsSince(before.State(), tested.random), 1 + fireDraws + 1 + OwnRecord + OwnRecord + 3);
        EXPECT_TRUE(arrow.alive);
        EXPECT_EQ(arrow.dword100, 2);
        const auto& impact = tested.world.DrawTail().joints[full ? 0 : 4];
        EXPECT_TRUE(impact.alive);
        EXPECT_EQ(impact.type, 0x60f);
        EXPECT_EQ(impact.subtype, 6);
        EXPECT_EQ(impact.ownerContext, &arrow);
        EXPECT_EQ(impact.width, 30);
        EXPECT_EQ(impact.life, 20);
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
        EXPECT_EQ(arrow.life, 28);
        EXPECT_EQ(tested.sent.size(), 1u);
    }
}

TEST(ArrowEffectsTest, BothPiercingImpactSitesUseTheFirstFreeJointAndSkipBirthRandomnessWhenFull)
{
    for (const bool marked : {false, true})
    {
        std::uint32_t fullState = 0;
        for (const bool full : {true, false})
        {
            SCOPED_TRACE(testing::Message() << marked << "/" << full);
            ArrowWorld tested(0);
            auto& arrow = tested.world.Map().effects.Effects()[0];
            arrow.alive = true;
            arrow.type = 0xdc;
            arrow.life = 30;
            arrow.subtype = 2;
            arrow.state = marked ? 1 : 0;
            arrow.ownerContext = &tested.Hero();
            arrow.byte89 = 3;
            arrow.transform.position = {1000, 1000, 0};
            arrow.transform.angles = {10, 20, 30};
            tested.world.OwnCharacter().SetSkillAt(3, 0x34);
            auto& victim = tested.Target(VictimSlot, true, 0);
            victim.position = {1000, 950, 0};
            victim.key = 0x1234;
            auto& joints = tested.world.DrawTail().joints;
            for (auto& joint : joints)
                joint.alive = true;
            if (!full)
                joints[13].alive = false;
            if (marked)
                tested.HitDrawIs(1, false);
            const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_TRUE(arrow.alive);
            EXPECT_EQ(arrow.life, 29);
            EXPECT_EQ(arrow.dword100, marked ? 2 : 0);
            EXPECT_EQ(tested.sent.size(), marked ? 1u : 0u);
            if (full)
                fullState = tested.random.State();
            else
            {
                CrtRandom expected(fullState);
                const int phase = expected.Next() % 360;
                EXPECT_EQ(tested.random.State(), expected.State());
                const auto& impact = joints[13];
                EXPECT_EQ(impact.type, 0x60f);
                EXPECT_EQ(impact.subtype, 6);
                EXPECT_EQ(impact.ownerContext, &arrow);
                EXPECT_EQ(impact.ownerKind, Client::Scenes::JointOwnerKind::ModelEffect);
                EXPECT_EQ(impact.position, (glm::vec3{1000, 1000, 0}));
                EXPECT_EQ(impact.targetPosition, impact.position);
                EXPECT_EQ(impact.angles, arrow.transform.angles);
                EXPECT_EQ(impact.velocity.x, static_cast<float>(phase));
                EXPECT_EQ(impact.key, -1);
                EXPECT_EQ(impact.width, 30);
                EXPECT_EQ(impact.maximumCount, 30);
                EXPECT_EQ(impact.life, 20);
            }
        }
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
// y frena la flecha (más bajo, nada; con el 0x08, no se pide la altura). El fuego 0x5d9/5 nace antes de la cola
// común: tres rand() además del de la luz, sin choque. Después la flecha se mueve y pierde una vida (0x52cb98).

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
            // CRT1: luz41, velocidad18467, escala6334 y giro26500, en ese orden.
            tested.random = CrtRandom(1);
            const CrtRandom before = tested.random;

            const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());

            ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
            EXPECT_EQ(DrawsSince(before.State(), tested.random), 4);
            EXPECT_EQ(arrow.transform.angles.x, probe.pitch);
            EXPECT_EQ(arrow.velocity, probe.stopped ? glm::vec3(0.0f) : velocity);
            EXPECT_EQ(heights, (probe.attribute & 0x08) != 0 ? 0 : 1);
            EXPECT_TRUE(arrow.alive);
            EXPECT_EQ(arrow.life, 39);
            const glm::vec3 emittedAt(1990, 2940, probe.z);
            EXPECT_EQ(arrow.previousPosition, emittedAt);
            if (probe.stopped)
                EXPECT_EQ(arrow.transform.position, emittedAt);
            else
                EXPECT_LT(arrow.transform.position.z, emittedAt.z);
            ASSERT_EQ(
                std::ranges::count_if(tested.particles.All(), [](const auto& particle) { return particle.alive; }), 1);
            const auto& fire = tested.particles.All()[0];
            EXPECT_EQ(fire.type, Client::World::FireParticle);
            EXPECT_EQ(fire.subtype, 5);
            EXPECT_EQ(fire.position, emittedAt);
            EXPECT_EQ(fire.angles, arrow.transform.angles);
            EXPECT_EQ(fire.owner, 0u);
            EXPECT_EQ(fire.life, 24);
            EXPECT_FLOAT_EQ(fire.scale, 1.9f);
            EXPECT_EQ(fire.rotation, 220.0f);
            EXPECT_EQ(fire.velocity, glm::vec3(0, -3.5f, 0));
            EXPECT_FLOAT_EQ(fire.light.x, .64f);
            EXPECT_FLOAT_EQ(fire.light.y, .4f);
            EXPECT_FLOAT_EQ(fire.light.z, .16f);
        }
    }
}

// E1/1 de Crywolf:610/13 al nacer ydos60f/5 en las vidas pares mayores25, con CRT intercalado.
// También conserva el recorrido de la fortaleza, que usa el mismo constructor.

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
            ASSERT_TRUE(Client::Scenes::CreateTheArrowEffect(arguments, tested.world, tested.random).HasValue());
            EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord);
            const auto beforeStep = tested.random.State();
            const auto arrows = tested.Arrows();
            ASSERT_EQ(arrows.size(), 1u);
            ModelEffect& arrow = *arrows.front();
            arrow.life = life;
            arrow.transform.angles.y = 10.0f;

            const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());

            const bool sparkles = life > 25 && life % 2 == 0;
            EXPECT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
            EXPECT_EQ(DrawsSince(beforeStep, tested.random), sparkles ? 15 : 1);
            EXPECT_EQ(std::ranges::count_if(tested.particles.All(),
                                            [](const auto& p) { return p.alive && p.type == 0x60f && p.subtype == 5; }),
                      sparkles ? 2 : 0);
            EXPECT_EQ(arrow.transform.angles.y, 70.0f);
            EXPECT_EQ(arrow.life, life - 1);
            EXPECT_EQ(std::ranges::count_if(tested.sprites.Sprites(), [](const auto& sprite) { return sprite.alive; }),
                      2);
        }
    }
}

TEST(ArrowEffectsTest, TheCrywolfArrowKeepsCallerDrawsWhenParticleCapacityIsPartial)
{
    for (int room = 0; room <= 2; ++room)
    {
        ArrowWorld tested(34);
        ASSERT_TRUE(Client::Scenes::CreateTheArrowEffect({.type = 0xe1, .position = {2000, 3000, 400}, .subtype = 1},
                                                         tested.world, tested.random)
                        .HasValue());
        auto arrows = tested.Arrows();
        ASSERT_EQ(arrows.size(), 1u);
        auto& arrow = *arrows[0];
        arrow.life = 26;
        const auto& joints = tested.world.DrawTail().joints;
        ASSERT_TRUE(joints[0].alive);
        EXPECT_EQ(joints[0].ownerContext, &arrow);
        EXPECT_EQ(joints[0].ownerKind, Client::Scenes::JointOwnerKind::ModelEffect);
        for (auto& p : tested.particles.All())
            p.alive = true;
        for (int i = 0; i < room; ++i)
            tested.particles.All()[static_cast<std::size_t>(i)].alive = false;
        const auto before = tested.random.State();
        ASSERT_TRUE(StepArrow(arrow, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(DrawsSince(before, tested.random), 7 + room * 4);
        EXPECT_EQ(arrow.life, 25);
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
            if (skill == 0x33)
            {
                ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
                EXPECT_EQ(DrawsSince(before.State(), tested.random), 1 + fireDraws + OwnRecord);
                EXPECT_EQ(arrow.life, 29);
            }
            else
            {
                ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
                constexpr int FullBurst = 20 * (2 + 5);
                EXPECT_EQ(tested.world.Runtime().lastSoundId, 0x40);
                EXPECT_EQ(DrawsSince(before.State(), tested.random), 1 + fireDraws + OwnRecord + FullBurst);
                EXPECT_EQ(arrow.life, 29);
            }
        }
    }
}

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

// 0x53e642: pose propia retenida y escala/origen compartidos; los sorteos de los pools van intercalados.

TEST(ArrowEffectsTest, TheAquagoldArrowFallsAndBounces)
{
    constexpr int Bubbles = 4;
    constexpr int StepDraws = 1 + Bubbles * 3;
    constexpr int BurstDraws = 20 * 2;
    constexpr float FirstZ = 135.0f;
    constexpr float FallenZ = 125.0f;
    struct Case
    {
        std::string_view name;
        std::optional<double> ground;
        std::int32_t life;
        std::int32_t subtype;
        bool ownedByTheHero;
        std::string_view cut;
    };
    const std::array cases{Case{"el terreno abajo", 0.0, 40, 0, true, ""},
                           Case{"el terreno arriba", 200.0, 40, 0, true, ""},
                           Case{"el terreno en la z", 125.0, 40, 0, true, ""},
                           Case{"la última vida del héroe", 0.0, 1, 0, true, ""},
                           Case{"la última vida con el subtipo 0x63", 0.0, 1, 0x63, true, ""},
                           Case{"la última vida de otro", 0.0, 1, 0, false, ""},
                           Case{"sin el terreno", std::nullopt, 40, 0, true, "0x524382"}};
    const Weapon aquagold{"4/14", 0x80e, 0xffff, 0x2e, 0xe2};
    const auto times = [](float value, std::uint32_t factor)
    { return static_cast<float>(static_cast<double>(value) * static_cast<double>(std::bit_cast<float>(factor))); };
    for (const std::int32_t map : {0, 2, 10})
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", " << probe.name);
            ArrowWorld tested(map);
            ActorState& hero = tested.Archer(aquagold, aquagold.action, NoTarget);
            ASSERT_TRUE(StepTheAttack(hero, HeroSlot, tested.Context()).HasValue());
            const auto arrows = tested.Arrows();
            ASSERT_EQ(arrows.size(), 1u);
            ModelEffect& arrow = *arrows.front();
            ASSERT_EQ(arrow.type, aquagold.arrow);
            EXPECT_EQ(arrow.transform.position.z, FirstZ);
            EXPECT_EQ(arrow.turnDirection, -10.0f);
            EXPECT_EQ(arrow.velocity.y, -30.0f);
            arrow.life = probe.life;
            arrow.subtype = probe.subtype;
            if (!probe.ownedByTheHero)
                arrow.ownerContext = &tested.world.Actors()[OtherSlot];
            tested.ground = probe.ground;
            tested.FillTheParticles();
            const CrtRandom before = tested.random;

            const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());

            const bool last = probe.life == 1;
            const bool fell = probe.ground.has_value();
            const int terminal = last && fell && probe.ownedByTheHero && probe.subtype != 0x63 ? OwnRecord : 0;
            EXPECT_EQ(DrawsSince(before.State(), tested.random),
                      StepDraws + (last && fell ? BurstDraws : 0) + terminal);
            if (!probe.cut.empty())
            {
                EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
                continue;
            }
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            const bool bounces = probe.ground.value_or(0.0) > static_cast<double>(FallenZ);
            EXPECT_EQ(arrow.transform.position.z, bounces ? 200.0f : FallenZ);
            EXPECT_EQ(arrow.turnDirection, bounces ? times(-11.0f, 0xbecccccdu) : -11.0f);
            EXPECT_EQ(arrow.velocity.y, bounces ? times(-30.0f, 0x3ecccccdu) : -30.0f);
            EXPECT_EQ(arrow.life, probe.life - 1);
            EXPECT_EQ(arrow.alive, !last);
        }
    }
}

// Las skills de tiro (0x34, 0x33 y 0x2e) van a 0x53e741: el tiro de 0x53d9a0 (el registro propio del héroe, la skill en
// [0x5767948], el sonido 0x82 de la 0x33 y una flecha con su alta, que abre y cierra el registro propio otra vez) y
// cortan en el registro propio de 0x53e75b (hito 4); la 0x2e corta antes, en el efecto 0x13a de 0x53df2a (hito 4), y la
// 0x34 no llega: corta en 0x4f9899. La bandera de la 0x18 sin blanco tira el abanico de 0x53e2e0: tres flechas con la z
// de los ángulos del héroe en 0, 15 y −15, que vuelve a 0, y corta en 0x53e365; con la 0xa24 o la 0xa25 en +0x2b8,
// cuatro en 5, 15, −5 y −15, que queda en 15 (Q-C1181), y corta en 0x53e4dd. Cada tiro saca los 3.848 rand() del
// registro propio de 0x53d9a0 y otros tantos en la salida del alta. El brillo 0x53e642 de la 0xa25 se cubre con el
// cuerpo, los pools y el RNG intercalado abajo. En Lorencia, en Devias y en Icarus (10).

TEST(ArrowEffectsTest, TheSkillShotsAndTheFanContinueAfterShooting)
{
    struct Case
    {
        std::string_view name;
        std::uint8_t skill;
        std::uint16_t target;
        std::uint16_t rightHand;
        std::string_view cut;
        std::vector<float> yaws;
        float yawAfter;
    };
    // Antes de la flecha: con blanco, la cabeza, la lectura de la habilidad y los dos de 0x59fb80; sin blanco, la
    // cabeza, la lectura de 0x502b12 (la bandera) y los mismos.
    const int withTarget = CountingWithTarget + SkillRead + ShotReads;
    const int withoutTarget = CountingWithoutTarget + 2 * SkillRead + ShotReads;
    const std::array cases{
        Case{"la 0x2e", 0x2e, VictimSlot, 0xffff, "0x51e01a", {}, 0.0f},
        Case{"la 0x33", 0x33, NoTarget, 0xffff, "", {0.0f}, 0.0f},
        Case{"la 0x18 sin blanco", 0x18, NoTarget, 0xffff, "", {0.0f, 15.0f, -15.0f}, 0.0f},
        Case{"la 0x18 con la 0xa24", 0x18, NoTarget, 0xa24, "", {5.0f, 15.0f, -5.0f, -15.0f}, 15.0f},
        Case{"la 0x18 con la 0xa25", 0x18, NoTarget, 0xa25, "", {5.0f, 15.0f, -5.0f, -15.0f}, 15.0f}};
    for (const std::int32_t map : {0, 2, 10})
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", " << probe.name);
            ArrowWorld tested(map);
            tested.Target(VictimSlot, true, 400.0f);
            ActorState& hero = tested.Archer(ShortBow, ShortBow.action, probe.target);
            hero.byte38e = probe.skill;
            hero.hands[0].model = probe.rightHand;

            const auto struck = StepTheAttack(hero, HeroSlot, tested.Context());

            if (probe.cut.empty())
                EXPECT_TRUE(struck.HasValue()) << ErrorOf(struck);
            else
                EXPECT_TRUE(HasError(struck, probe.cut)) << ErrorOf(struck);
            const int before = probe.target == NoTarget ? withoutTarget : withTarget;
            const auto shots = static_cast<int>(std::max<std::size_t>(probe.yaws.size(), 1));
            const int arrows = static_cast<int>(probe.yaws.size());
            const int continuation = probe.cut.empty() ? OwnRecord + SkillRead : 0;
            const int shine = probe.rightHand == 0xa25 ? 15 * (2 + 5) : 0;
            EXPECT_EQ(DrawsSince(Seed, tested.random),
                      before + shots * OwnRecord + arrows * ArrowCreation + continuation + shine);
            EXPECT_EQ(tested.world.OwnCharacter().bytef04, probe.cut.empty() ? 1 : 0);
            const auto found = tested.Arrows();
            ASSERT_EQ(found.size(), probe.yaws.size());
            for (std::size_t arrow = 0; arrow < found.size(); ++arrow)
            {
                EXPECT_EQ(found[arrow]->type, 0xdb);
                EXPECT_EQ(found[arrow]->transform.angles.z, probe.yaws[arrow]) << arrow;
                EXPECT_EQ(found[arrow]->state, probe.skill == 0x18 ? 1 : 0) << arrow;
            }
            EXPECT_EQ(hero.angles.z, probe.yawAfter);
            EXPECT_EQ(tested.world.Runtime().lastShotSkill, probe.skill);
            EXPECT_EQ(tested.sounds,
                      probe.skill == 0x33 ? std::vector<std::int32_t>{0x82} : std::vector<std::int32_t>{});
        }
    }
}

// El abanico de la 0x18 con la z de los ángulos del héroe en 100,3f: cada corrimiento se suma en la x87 y se guarda en
// float (0x53e31b..0x53e362), así que la z de cada flecha es la suma redondeada a float de la anterior.

TEST(ArrowEffectsTest, TheFanTurnsTheYawInFloatSteps)
{
    ArrowWorld tested(0);
    ActorState& hero = tested.Archer(ShortBow, ShortBow.action, NoTarget);
    hero.byte38e = 0x18;
    constexpr float Start = 100.3f;
    hero.angles.z = Start;

    const auto struck = StepTheAttack(hero, HeroSlot, tested.Context());

    EXPECT_TRUE(struck.HasValue()) << ErrorOf(struck);
    const auto step = [](float yaw, double offset) { return static_cast<float>(static_cast<double>(yaw) + offset); };
    const float second = step(Start, 15.0);
    const float third = step(second, -30.0);
    const auto found = tested.Arrows();
    ASSERT_EQ(found.size(), 3u);
    EXPECT_EQ(found[0]->transform.angles.z, Start);
    EXPECT_EQ(found[1]->transform.angles.z, second);
    EXPECT_EQ(found[2]->transform.angles.z, third);
    EXPECT_EQ(hero.angles.z, step(third, 15.0));
}

// El choque lee la skill del lugar +0x89 en el registro propio (0x5205cd..0x5207d1): con la 0x33 la flecha 0xdb se
// apaga sin más; con la 0x34 se apaga y 0x51fe10 suelta sus chispas 0x5c5/2, que cortan en 0x51fe9e (CL11) después de
// los dos rand() de la primera. En Lorencia y en Devias, con el blanco a 50.

TEST(ArrowEffectsTest, MissingBurstParticlesKeepsItsTwoCallerDrawsBeforeTheDiagnostic)
{
    ArrowWorld tested(0);
    tested.Target(VictimSlot, true, 50.0f);
    ActorState& hero = tested.Archer(Crossbow, Crossbow.action, VictimSlot);
    ASSERT_TRUE(StepTheAttack(hero, HeroSlot, tested.Context()).HasValue());
    const auto arrows = tested.Arrows();
    ASSERT_EQ(arrows.size(), 1u);
    ModelEffect& arrow = *arrows.front();
    arrow.byte89 = 3;
    tested.world.OwnCharacter().words6c[0] = static_cast<std::uint32_t>(0x34) << 24;
    const CrtRandom before = tested.random;
    ArrowStepContext context = tested.StepContext();
    context.particles = nullptr;

    const auto stepped = StepArrow(arrow, nullptr, context);

    EXPECT_TRUE(HasError(stepped, "0x51fe9e")) << ErrorOf(stepped);
    EXPECT_EQ(DrawsSince(before.State(), tested.random), 1 + OwnRecord + 2);
}

// La flecha 0xdb con el subtipo 5 (la crea el hito 4) no corta en el paso: su fuego 0x5d9/0 lleva (luz · 0,1f;
// luz · 0,6f; luz · 0,3f) en vez del color de la luz dinámica (0x5240be..0x5240fa).

TEST(ArrowEffectsTest, TheReignHandShineUsesTheSharedPlacementAndRespectsPoolCapacity)
{
    for (const bool particlesFull : {false, true})
    {
        for (const bool jointsFull : {false, true})
        {
            SCOPED_TRACE(testing::Message() << "partículas " << particlesFull << ", joints " << jointsFull);
            auto tested = std::make_unique<ArrowWorld>(0);
            ActorState& actor = tested->world.Actors()[OtherSlot];
            actor = ArrowWorld::Player(2);
            actor.hands[0].model = 0xa25;
            actor.hands[0].bone = 0;
            actor.position = {9000.0f, 8000.0f, 7000.0f};
            actor.scale = 7.0f;
            actor.bodyPose.resize(1, Client::Render::Models::BoneTransform(1.0f));
            actor.bodyPose[0][3] = {3.0f, 4.0f, 5.0f};
            tested->world.PlayerModel() = {.scale = 2.0f, .origin = {100.0f, 200.0f, 300.0f}};
            // El tiro sigue al brillo aunque no encuentre lugar para la flecha 0x111.
            for (ModelEffect& effect : tested->world.Map().effects.Effects())
                effect.alive = true;
            if (particlesFull)
                for (Client::World::Particle& particle : tested->particles.All())
                    particle.alive = true;
            std::vector<Client::Scenes::JointState> joints(100);
            if (jointsFull)
                for (Client::Scenes::JointState& joint : joints)
                    joint.alive = true;
            auto context = tested->Context();
            context.joints = joints;
            CrtRandom expected = tested->random;
            int expectedJoints = 0;
            for (int index = 0; index < 15; ++index)
            {
                static_cast<void>(expected.Next());
                if (expected.Next() % 2 == 0 && !jointsFull)
                {
                    ++expectedJoints;
                    expected.Discard(2);
                }
                if (!particlesFull)
                    expected.Discard(5);
            }
            const auto shot = Client::Scenes::FireTheArrow(actor, nullptr, OtherSlot, 0, 0, 0, context);
            ASSERT_TRUE(shot.HasValue()) << shot.Error();
            EXPECT_EQ(tested->random.State(), expected.State());
            if (!particlesFull)
            {
                int created = 0;
                for (const auto& particle : tested->particles.All())
                {
                    if (!particle.alive)
                        continue;
                    ++created;
                    EXPECT_EQ(particle.type, Client::World::FairySparkParticle);
                    EXPECT_EQ(particle.subtype, 0);
                    EXPECT_EQ(particle.position, glm::vec3(106.0f, 228.0f, 50.0f));
                }
                EXPECT_EQ(created, 15);
            }
            if (!jointsFull)
            {
                int created = 0;
                for (const auto& joint : joints)
                {
                    if (!joint.alive)
                        continue;
                    ++created;
                    EXPECT_EQ(joint.type, 0x617);
                    EXPECT_EQ(joint.subtype, 3);
                }
                EXPECT_EQ(created, expectedJoints);
            }
        }
    }
}

// 0x517850 recorre los lugares 0..399: incluye al 399 y deja afuera al registro fantasma 400. En el plano, el borde
// de 100 cuenta; nextafter fuera no, y el unordered de la comparación nativa cuenta.

TEST(ArrowEffectsTest, ArrowHitSearchUsesTheNativePoolBoundAndDistancePredicate)
{
    struct Case
    {
        std::string_view name;
        std::optional<std::size_t> targetSlot;
        float distance;
        bool seen;
        std::uint8_t dying;
        bool owner;
        bool heroNear;
        bool expectedHit;
    };
    constexpr float Radius = 100.0f;
    const float OutsideRadius = std::nextafter(Radius, 200.0f);
    const float Unordered = std::bit_cast<float>(0x7fc00000u);
    const std::array cases{
        Case{"lugar 399", 399, 50.0f, true, 0, false, false, true},
        Case{"lugar 400", 400, 50.0f, true, 0, false, false, false},
        Case{"borde", 399, Radius, true, 0, false, false, true},
        Case{"apenas afuera", 399, OutsideRadius, true, 0, false, false, false},
        Case{"NaN", 399, Unordered, true, 0, false, false, true},
        Case{"dueño", 399, 50.0f, true, 0, true, false, false},
        Case{"héroe", std::nullopt, 50.0f, true, 0, false, true, false},
        Case{"no visto", 399, 50.0f, false, 0, false, false, false},
        Case{"muriéndose", 399, 50.0f, true, 1, false, false, false},
    };
    for (const Case& test : cases)
    {
        SCOPED_TRACE(test.name);
        ArrowWorld tested(0);
        ModelEffect& arrow = tested.world.Map().effects.Effects().front();
        arrow.alive = true;
        arrow.type = 0xdc;
        arrow.life = 30;
        arrow.transform.position = {};
        if (test.targetSlot)
        {
            ActorState& target = tested.world.Actors()[*test.targetSlot];
            target.alive = true;
            target.byte88 = 2;
            target.seen = test.seen;
            target.dying = test.dying;
            target.position = {0.0f, -test.distance, 0.0f};
            if (test.owner)
                arrow.ownerContext = &target;
        }
        if (test.heroNear)
        {
            tested.Hero().seen = true;
            tested.Hero().position = {0.0f, -test.distance, 0.0f};
        }

        const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());

        ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
        EXPECT_EQ(!arrow.alive, test.expectedHit);
    }
}
// 524405: el envío terminal sucede antes de la luz/choque, aun cuando las partículas no tengan hueco.
TEST(ArrowEffectsTest, TheLastAquagoldStepSendsTheNearbySkillHits)
{
    ArrowWorld tested(0);
    auto& hero = tested.Archer(Crossbow, Crossbow.action, NoTarget);
    auto& target = tested.Target(VictimSlot, true, 50.0f);
    target.position = {1050.0f, 1000.0f, 0.0f};
    target.key = 0x1234;
    tested.world.OwnCharacter().SetSkillAt(2, 0x18);
    auto& arrow = tested.world.Map().effects.Effects().front();
    arrow.alive = true;
    arrow.type = 0xe2;
    arrow.life = 1;
    arrow.transform.position = {1000.0f, 1000.0f, 135.0f};
    arrow.turnDirection = -10.0f;
    arrow.ownerContext = &hero;
    arrow.byte89 = 2;
    arrow.word8c = 7;
    tested.FillTheParticles();
    const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
    ASSERT_EQ(tested.sent.size(), 1u);
    const auto clear = Client::Network::Crypto::Xor32Decode(tested.sent[0].packet);
    const std::vector<std::byte> expected{std::byte{0xc1}, std::byte{11},   std::byte{0x10}, std::byte{0x18},
                                          std::byte{9},    std::byte{9},    std::byte{7},    std::byte{1},
                                          std::byte{0x12}, std::byte{0x34}, std::byte{0}};
    EXPECT_TRUE(std::ranges::equal(clear.Bytes(), expected));
    EXPECT_FALSE(arrow.alive);
    EXPECT_EQ(arrow.life, 0);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 1 + 4 * 3 + 2 * 20 * 2 + 2 * OwnRecord + 2);
}

TEST(ArrowEffectsTest, FullJointsPreserveTheArrowConstructorTails)
{
    for (const auto type : {0xf7, 0xf8, 0xf9, 0x10e, 0x111, 0x11c, 0xe1, 0xdb})
    {
        SCOPED_TRACE(type);
        ArrowWorld tested(0);
        tested.world.OwnCharacter().bytef04 = 9;
        for (auto& joint : tested.world.DrawTail().joints)
            joint.alive = true;
        const std::int32_t subtype = type == 0xe1 ? 1 : 2;
        const Client::World::ModelEffectArguments args{.type = static_cast<std::int16_t>(type),
                                                       .position = {100.0f, 200.0f, 300.0f},
                                                       .angles = {},
                                                       .light = {1.0f, 1.0f, 1.0f},
                                                       .subtype = subtype};
        const auto made = Client::Scenes::CreateTheArrowEffect(args, tested.world, tested.random);
        ASSERT_TRUE(made.HasValue()) << made.Error();
        const auto& arrow = tested.world.Map().effects.Effects().front();
        EXPECT_EQ(arrow.life, 30);
        EXPECT_EQ(arrow.word8c, 9);
        EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord + (type == 0x11c ? 1 : 0));
        if (type == 0xf7 || type == 0x10e || type == 0x111 || type == 0xf9 || type == 0xdb)
        {
            EXPECT_EQ(arrow.state, 1);
            EXPECT_EQ(tested.world.Map().effects.Effects()[1].transform.scale, 1.0f);
        }
    }
}

TEST(ArrowEffectsTest, FullPoolsPreserveTheNewlyReachableArrowSteps)
{
    for (const auto type : {0xf7, 0xf8, 0xf9, 0x10e, 0x111, 0x11c})
    {
        SCOPED_TRACE(type);
        ArrowWorld tested(0);
        tested.FillTheParticles();
        for (auto& effect : tested.world.Map().effects.Effects())
            effect.alive = true;
        auto& arrow = tested.world.Map().effects.Effects().front();
        arrow.type = static_cast<std::int16_t>(type);
        arrow.life = 20;
        arrow.transform.position = {1000.0f, 1000.0f, 1000.0f};
        arrow.velocity = {0.0f, -70.0f, 0.0f};
        const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(arrow.life, 19);
        // 0x52ccbe: la 0xf8 no se mueve; la velocidad corre su partida.
        EXPECT_EQ(arrow.transform.position.y, type == 0xf8 ? 1000.0f : 930.0f);
        EXPECT_EQ(DrawsSince(Seed, tested.random), type == 0x10e ? 2 : type == 0x11c ? 10 : 1);
    }
}

// 0x52594c..0x5259a4: después del rand() de la luz (0x5208c5), el paso de la flecha 0x111 gira la y +30 y suelta tres
// humos 0x5f2/13 en su posición, con esos ángulos, la luz 1 (0x520921) y escala 1, tres rand() cada uno; después se
// mueve.
TEST(ArrowEffectsTest, TheReignArrowLeavesThreeEmbers)
{
    ArrowWorld tested(0);
    for (auto& effect : tested.world.Map().effects.Effects())
        effect.alive = true;
    auto& arrow = tested.world.Map().effects.Effects().front();
    arrow.type = 0x111;
    arrow.subtype = 2;
    arrow.life = 20;
    arrow.transform.position = {1000.0f, 1000.0f, 1000.0f};
    arrow.transform.angles = {0.0f, 15.0f, 0.0f};
    arrow.velocity = {0.0f, -70.0f, 0.0f};
    const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(arrow.transform.angles.y, 45.0f);
    auto twin = std::make_unique<Client::World::Particles>();
    CrtRandom expected(Seed);
    static_cast<void>(expected.Next());
    for (std::size_t index = 0; index < 3; ++index)
    {
        SCOPED_TRACE(index);
        const auto replayed = twin->CreateChecked(0x5f2, {1000.0f, 1000.0f, 1000.0f}, {0.0f, 45.0f, 0.0f},
                                                  glm::vec3(1.0f), 13, 1.0f, 0.0f, expected);
        ASSERT_TRUE(replayed.HasValue() && replayed.Value() == std::optional<std::size_t>{index});
        const Client::World::Particle& ember = tested.particles.All()[index];
        const Client::World::Particle& reference = twin->All()[index];
        EXPECT_TRUE(ember.alive);
        EXPECT_EQ(ember.type, 0x5f2);
        EXPECT_EQ(ember.subtype, 13);
        EXPECT_EQ(ember.position, glm::vec3(1000.0f));
        EXPECT_EQ(ember.angles, reference.angles);
        EXPECT_EQ(ember.light, glm::vec3(1.0f));
        EXPECT_EQ(ember.life, 20);
        EXPECT_EQ(ember.scale, reference.scale);
        EXPECT_EQ(ember.rotation, reference.rotation);
        EXPECT_EQ(ember.verticalSpeed, reference.verticalSpeed);
    }
    EXPECT_FALSE(tested.particles.All()[3].alive);
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(DrawsSince(Seed, tested.random), 1 + 3 * 3);
    EXPECT_EQ(arrow.transform.position.y, 930.0f);
}

// 0x51a347..0x51a3b7: la 0xf8/1 escribe los ángulos del caller (0, 0, z) antes de sus tres joints 0x60f/0x19 (alta
// 0x533db8, un rand() % 3000 cada uno, con la flecha de dueño) y sigue a 0x51a1b0; con la tabla llena, no hay joints.
TEST(ArrowEffectsTest, CelestialWritesTheCallerAnglesBeforeItsJoints)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        ArrowWorld tested(0);
        tested.world.OwnCharacter().bytef04 = 9;
        if (full)
            for (auto& joint : tested.world.DrawTail().joints)
                joint.alive = true;
        glm::vec3 angles{15.0f, 25.0f, 35.0f};
        const Client::World::ModelEffectArguments args{.type = 0xf8,
                                                       .position = {10.0f, 20.0f, 30.0f},
                                                       .angles = angles,
                                                       .light = {1.0f, 1.0f, 1.0f},
                                                       .subtype = 1};
        const auto made = Client::Scenes::CreateTheArrowEffect(args, tested.world, tested.random, &angles);
        const auto& effect = tested.world.Map().effects.Effects().front();
        EXPECT_EQ(angles, (glm::vec3{0.0f, 0.0f, 35.0f}));
        EXPECT_EQ(effect.transform.angles, (glm::vec3{15.0f, 25.0f, 35.0f}));
        EXPECT_EQ(effect.transform.position, (glm::vec3{10.0f, 20.0f, 160.0f}));
        EXPECT_EQ(effect.velocity, (glm::vec3{0.0f, -60.0f, 0.0f}));
        ASSERT_TRUE(made.HasValue()) << ErrorOf(made);
        EXPECT_EQ(effect.word8c, 9);
        EXPECT_EQ(DrawsSince(Seed, tested.random), (full ? 0 : 3) + OwnRecord);
        if (full)
            continue;
        CrtRandom expected(Seed);
        const auto& joints = tested.world.DrawTail().joints;
        for (std::size_t slot = 0; slot < 3; ++slot)
        {
            SCOPED_TRACE(slot);
            const auto& joint = joints[slot];
            ASSERT_TRUE(joint.alive);
            EXPECT_EQ(joint.type, 0x60f);
            EXPECT_EQ(joint.subtype, 0x19);
            EXPECT_EQ(joint.ownerKind, Client::Scenes::JointOwnerKind::ModelEffect);
            EXPECT_EQ(joint.ownerContext, &effect);
            EXPECT_EQ(joint.life, 30);
            EXPECT_EQ(joint.maximumCount, 15);
            EXPECT_EQ(joint.width, 30.0f);
            EXPECT_EQ(joint.velocity.x, static_cast<float>(expected.Next() % 3000));
            EXPECT_EQ(joint.position, effect.start);
        }
        EXPECT_FALSE(joints[3].alive);
    }
}

// 0x526c05: con las partículas llenas, la flecha 0x11c deja la onda 0x114/3 (0x51c31f) con la clave en 0, sin rand(),
// y sigue con su luz y su choque.
TEST(ArrowEffectsTest, TheTwentiethArrowLeavesTheThirdWave)
{
    ArrowWorld tested(0);
    tested.FillTheParticles();
    auto& arrow = tested.world.Map().effects.Effects().front();
    arrow.alive = true;
    arrow.type = 0x11c;
    arrow.life = 20;
    arrow.transform.position = {100.0f, 200.0f, 300.0f};
    arrow.transform.angles = {10.0f, 20.0f, 30.0f};
    arrow.light = {0.5f, 0.25f, 1.0f};
    const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(arrow.point40, (glm::vec3{100.0f, 200.0f, 300.0f}));
    EXPECT_EQ(arrow.life, 19);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 10);
    const auto& wave = tested.world.Map().effects.Effects()[1];
    ASSERT_TRUE(wave.alive);
    EXPECT_EQ(wave.type, 0x114);
    EXPECT_EQ(wave.subtype, 3);
    EXPECT_EQ(wave.life, 15);
    EXPECT_EQ(wave.transform.scale, 0.0f);
    EXPECT_EQ(wave.disturbanceState, 0xffff);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(wave.turnDirection), 0x3c23d70au);
    EXPECT_EQ(wave.transform.angles, (glm::vec3{90.0f, 0.0f, 30.0f}));
    EXPECT_EQ(wave.transform.position, (glm::vec3{100.0f, 200.0f, 300.0f}));
    EXPECT_EQ(wave.start, (glm::vec3{100.0f, 200.0f, 300.0f}));
    EXPECT_EQ(wave.light, (glm::vec3{0.5f, 0.25f, 1.0f}));
}

TEST(ArrowEffectsTest, TheLastFreeEffectSlotStillFinishesAnArrowWithFullJoints)
{
    ArrowWorld tested(0);
    for (auto& joint : tested.world.DrawTail().joints)
        joint.alive = true;
    for (auto& effect : tested.world.Map().effects.Effects())
        effect.alive = true;
    auto& arrow = tested.world.Map().effects.Effects().front();
    arrow.alive = false;
    tested.world.OwnCharacter().bytef04 = 11;
    const Client::World::ModelEffectArguments args{
        .type = 0x10e, .position = {10.0f, 20.0f, 30.0f}, .angles = {}, .light = {1.0f, 1.0f, 1.0f}, .subtype = 2};
    const auto made = Client::Scenes::CreateTheArrowEffect(args, tested.world, tested.random);
    ASSERT_TRUE(made.HasValue()) << ErrorOf(made);
    EXPECT_EQ(arrow.point40, (glm::vec3{10.0f, 20.0f, 160.0f}));
    EXPECT_EQ(arrow.state, 1);
    EXPECT_EQ(arrow.dword100, 0);
    EXPECT_EQ(arrow.word8c, 11);
    EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord);
}

// 0x4039a0 usa 0xed/1, el mismo prefijo de las flechas pero con pitch−90, vy−42 y vz4; el paso0x524210 ilumina de
// rojo y continúa por 0x52cb98 sin el choque de 0x520170.
TEST(ArrowEffectsTest, TheTourProjectileCreatesAndStepsWithoutAttackingActors)
{
    for (const std::uint8_t depth : {std::uint8_t{0}, std::uint8_t{1}})
    {
        auto tested = std::make_unique<ArrowWorld>(34);
        tested->world.Runtime().ownRecordDepth = depth;
        tested->world.OwnCharacter().bytef04 = 9;
        const Client::World::ModelEffectArguments arguments{.type = 0xed,
                                                            .position = {1000, 1000, 0},
                                                            .angles = {},
                                                            .light = {2, 3, 4},
                                                            .subtype = 1,
                                                            .owner = &tested->Hero()};
        const auto created = Client::Scenes::CreateTheArrowEffect(arguments, tested->world, tested->random);
        ASSERT_TRUE(created.HasValue()) << ErrorOf(created);
        auto& effect = tested->world.Map().effects.Effects()[0];
        EXPECT_TRUE(effect.alive);
        EXPECT_EQ(effect.type, 0xed);
        EXPECT_EQ(effect.life, 30);
        EXPECT_EQ(effect.transform.position, (glm::vec3{990, 940, 135}));
        EXPECT_EQ(effect.transform.angles, (glm::vec3{-90, 0, 0}));
        EXPECT_EQ(effect.transform.scale, 1);
        EXPECT_EQ(effect.velocity, (glm::vec3{0, -42, 4}));
        EXPECT_EQ(effect.objectState.blendMesh, -1);
        EXPECT_EQ(effect.word8c, 9);
        EXPECT_EQ(DrawsSince(Seed, tested->random), depth == 0 ? OwnRecord : 0);
        const auto stepped = StepArrow(effect, nullptr, tested->StepContext());
        ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
        EXPECT_EQ(effect.life, 29);
        EXPECT_EQ(effect.light, (glm::vec3{1, 0, 0}));
        EXPECT_NEAR(effect.transform.position.y, 944, 0.001);
        EXPECT_NEAR(effect.transform.position.z, 177, 0.001);
        EXPECT_EQ(DrawsSince(Seed, tested->random), depth == 0 ? OwnRecord + 1 : 1);
        EXPECT_TRUE(tested->sent.empty());
    }
}
} // namespace

TEST(ArrowEffectsTest, Attribute100StopsTheArrowAndHalvesLifeBeforeTheCommonEpilogue)
{
    ArrowWorld tested(0);
    tested.world.Runtime().siegeActive = true;
    tested.world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    tested.world.Map().terrain.attributes->cells[513] = 0x100;
    ModelEffect arrow;
    arrow.alive = true;
    arrow.type = 0xdc;
    arrow.life = 9;
    arrow.speed = 7;
    arrow.velocity = {1, 2, 3};
    arrow.transform.position = {150, 250, 50};
    const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
    EXPECT_EQ(arrow.speed, 0);
    EXPECT_EQ(arrow.velocity, glm::vec3(0));
    EXPECT_EQ(arrow.life, 3);
    EXPECT_TRUE(arrow.alive);
    EXPECT_EQ(arrow.transform.position, (glm::vec3{150, 250, 50}));
    EXPECT_EQ(DrawsSince(Seed, tested.random), 1);
}

// 0x523f33..0x523f72: la 0xf8/1 gira 60 su y y, en la vida 13, crea el pulso 0x105/3 con ella de dueño (0x51a01b: la
// escala 10 y la luz (0,9; 0,4; 0,6) para sus cuatro joints 0x610 con el noveno argumento en 3, el sonido 0x7a y la
// escala en 1); la cola (0x52ccbe) corre la partida y no la posición. Con la tabla llena, ni el pulso ni el sonido.
TEST(ArrowEffectsTest, CelestialPulseAtLifeThirteenCreatesTheTrailPulseAndMovesOnlyTheStart)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(::testing::Message() << "pool lleno " << full);
        ArrowWorld tested(34);
        if (full)
            for (auto& effect : tested.world.Map().effects.Effects())
                effect.alive = true;
        auto& arrow = tested.world.Map().effects.Effects().front();
        arrow.alive = true;
        arrow.type = 0xf8;
        arrow.subtype = 1;
        arrow.life = 13;
        arrow.transform.position = {1000, 1000, 300};
        arrow.transform.angles = {0, 0, 0};
        arrow.velocity = {0, -60, 0};
        arrow.light = {0.2f, 0.3f, 0.4f};
        arrow.start = {1000, 900, 300};
        const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
        EXPECT_TRUE(arrow.alive);
        EXPECT_EQ(arrow.life, 12);
        EXPECT_EQ(arrow.previousPosition, (glm::vec3{1000, 1000, 300}));
        EXPECT_EQ(arrow.transform.position, (glm::vec3{1000, 1000, 300}));
        EXPECT_EQ(arrow.transform.angles.y, 60);
        EXPECT_NEAR(arrow.start.x, 1000, 1e-3);
        EXPECT_EQ(arrow.start.y, 840);
        EXPECT_NEAR(arrow.start.z, 300, 1e-3);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 1);
        EXPECT_TRUE(tested.sounds.empty());
        const auto joints = std::ranges::count_if(tested.world.DrawTail().joints,
                                                  [](const auto& joint) { return joint.alive && joint.type == 0x610; });
        if (full)
        {
            EXPECT_EQ(joints, 0);
            EXPECT_NE(tested.world.Runtime().lastSoundId, 0x7a);
            continue;
        }
        EXPECT_EQ(joints, 4);
        EXPECT_EQ(tested.world.Runtime().lastSoundId, 0x7a);
        const auto& pulse = tested.world.Map().effects.Effects()[1];
        ASSERT_TRUE(pulse.alive);
        EXPECT_EQ(pulse.type, 0x105);
        EXPECT_EQ(pulse.subtype, 3);
        EXPECT_EQ(pulse.ownerContext, &arrow);
        EXPECT_EQ(pulse.owner.effect, &arrow);
        EXPECT_EQ(pulse.life, 100);
        EXPECT_EQ(pulse.transform.scale, 1.0f);
        EXPECT_EQ(pulse.transform.position, (glm::vec3{1000, 1000, 300}));
        EXPECT_EQ(pulse.start, (glm::vec3{1000, 1000, 300}));
        EXPECT_EQ(pulse.transform.angles, arrow.transform.angles);
        EXPECT_EQ(pulse.light, (glm::vec3{std::bit_cast<float>(0x3f666666u), std::bit_cast<float>(0x3ecccccdu),
                                          std::bit_cast<float>(0x3f19999au)}));
        EXPECT_EQ(pulse.objectState.blendMesh, 0);
        EXPECT_EQ(pulse.objectState.light, 1.0f);
    }
}

// Con los joints llenos, la 0xf8/1 nace sin cortar: no se mueve, su partida avanza 60 por paso y en la vida 13 suelta
// el pulso 0x105/3 en su posición, hasta que se le acaban los 30 pasos.
TEST(ArrowEffectsTest, CelestialArrowStaysPutWhileItsStartTravelsForThirtySteps)
{
    ArrowWorld tested(34);
    for (auto& joint : tested.world.DrawTail().joints)
        joint.alive = true;
    const Client::World::ModelEffectArguments args{
        .type = 0xf8, .position = {1000, 3000, 0}, .angles = {}, .light = {1, 1, 1}, .subtype = 1};
    const auto created = Client::Scenes::CreateTheArrowEffect(args, tested.world, tested.random);
    ASSERT_TRUE(created.HasValue()) << ErrorOf(created);
    auto& arrow = tested.world.Map().effects.Effects().front();
    EXPECT_EQ(arrow.start, (glm::vec3{990, 2900, 145}));
    for (int step = 1; step <= 30; ++step)
    {
        const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
        EXPECT_EQ(arrow.transform.position, (glm::vec3{1000, 3000, 130}));
        EXPECT_EQ(arrow.start.y, static_cast<float>(2900 - step * 60));
        EXPECT_EQ(arrow.life, 30 - step);
        EXPECT_EQ(arrow.alive, step < 30);
    }
    EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord + 30);
    EXPECT_EQ(tested.world.Runtime().lastSoundId, 0x7a);
    const auto& pulse = tested.world.Map().effects.Effects()[1];
    EXPECT_EQ(pulse.type, 0x105);
    EXPECT_EQ(pulse.transform.position, (glm::vec3{1000, 3000, 130}));
}

TEST(ArrowEffectsTest, CelestialPulsePrecedesTheHitAndDoesNotSkipItsLightOrLifetime)
{
    ArrowWorld tested(34);
    auto& target = tested.Target(VictimSlot, true, 0);
    target.position = {1000, 1000, 0};
    auto& arrow = tested.world.Map().effects.Effects().front();
    arrow.alive = true;
    arrow.type = 0xf8;
    arrow.subtype = 1;
    arrow.life = 13;
    arrow.transform.position = {1000, 1000, 130};
    arrow.velocity = {0, -60, 0};
    const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
    EXPECT_FALSE(arrow.alive);
    EXPECT_EQ(arrow.life, 12);
    EXPECT_EQ(arrow.transform.position, (glm::vec3{1000, 1000, 130}));
    EXPECT_TRUE(tested.world.Map().effects.Effects()[1].alive);
    EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord + 1);
    EXPECT_EQ(tested.world.Runtime().lastSoundId, 0x7a);
    EXPECT_TRUE(std::ranges::any_of(tested.lighting->cells, [](const auto& light) { return light[0] > 0; }));
}
