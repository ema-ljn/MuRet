#include "Network/Xor32Packet.h"
#include "Effects/ArrowEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Kernel/JointRequest.h"
#include "Kernel/ModelEffectArguments.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Skills/Skill.h"
#include "Math/HalfLifeMath.h"
#include "Kernel/DrawTailState.h"
#include "Models/LogicalAccess.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Skills/AttackStep.h"
#include "Actors/CharacterStep.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

#include "support/LentWorld.h"
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

// Client::Effects::FireTheArrow and StepArrow: 0x53e2b0, 0x53d9a0, the arrow cases of 0x518cf0 and of 0x5208a0,
// 0x520170 and 0x517850, through the attack step (0x50377d and 0x504060). 06, "La flecha".
namespace
{
using Client::Actors::CharacterStepContext;
using Client::Crt::CrtRandom;
using Client::Effects::ArrowStepContext;
using Client::Effects::ModelEffect;
using Client::Effects::StepArrow;
using Client::Kernel::ActorState;
using Client::Kernel::WorldState;
using Client::Skills::StepTheAttack;

constexpr std::uint32_t Seed = 0x2468ace1u;
constexpr std::size_t HeroSlot = 42;
constexpr std::size_t OtherSlot = 3;
constexpr std::size_t VictimSlot = 7;
constexpr std::uint16_t NoTarget = 0xffff;
// One read of +0x38e: one rand() when it closes at 0.
constexpr int SkillRead = 1;
constexpr int OwnRecord = Client::Models::OwnRecordCloseRandoms;
// The +0x381 step that runs the skill, before frame 5: the read of 0x4f97b0, the rand() of 0x4f5b97 and, with a target
// in the pool, the two reads of 0x4f811b and 0x4f8a2c.
constexpr int CountingWithTarget = SkillRead + 1 + 2 * SkillRead;
constexpr int CountingWithoutTarget = SkillRead + 1;
// The arrow in the attack step: the two reads of +0x38e, 0x59fb80 with the own record and, with the hero, the weapons
// of 0x53d9a0 with the own record.
constexpr int ShotReads = 2 * SkillRead + OwnRecord;
// Each arrow's creation: the exit 0x51a1b6 with the own record.
constexpr int ArrowCreation = OwnRecord;
// After the arrow with a target and no damage: the two switches, the sound with the own record and the write.
constexpr int AfterTheArrow = 2 * SkillRead + 1 + OwnRecord + SkillRead;

// Bow 4/0 in the left hand and crossbow 4/8 in the right, with their on-foot actions and their arrows.
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

// The rand() of arrow 0xdb's 0x5d9/0 fire (0x53fe70), measured in a separate pool.
int FireDraws()
{
    Client::Particles::Particles scratch;
    CrtRandom random(Seed);
    static_cast<void>(scratch.CreateChecked(Client::Particles::FireParticle, glm::vec3(0.0f), glm::vec3(0.0f),
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
        // 0x4fc450's timers away from 0, as in the attack step.
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

    // The target in `slot`, `distance` ahead of the shooter (the arrow goes toward −y with zero angles), seen and
    // alive: a player (+0x88 at 1) or a monster (at 2).
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

    // The hero with the weapon in the own record, the action and +0x381 at 14: the next step shoots.
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
                                    .terrain = &terrain,
                                    .particles = &particles,
                                    .soundRequest = [this](Client::Audio::SoundId sound)
                                    { sounds.push_back(sound.Value()); },
                                    .soundAt =
                                        [this](Client::Audio::SoundId sound, const glm::vec3& origin)
                                    {
                                        sounds.push_back(sound.Value());
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
            .terrain = &terrain,
            .terrainHeight = terrainHeight        ? terrainHeight
                             : ground.has_value() ? std::function<double(float, float)>(
                                                        [this](float, float) { return ground.value_or(0.0); })
                                                  : std::function<double(float, float)>{},
            .soundRequest = [this](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); },
            .send = [this](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); }};
    }

    // Draws rand() until the one after the next `skipped` is even (odd with `odd`): the rand() & 1 of 0x5201ed, after
    // the light's and the step's fire draws.
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

    // Fills the pool's 3000 slots with 0x5d9/0 fires (separate rand()): the following particles are neither created nor
    // draw rand(), as in 0x53fe70 with the pool full.
    void FillTheParticles()
    {
        CrtRandom scratch(Seed);
        for (std::size_t made = 0; made <= Client::Particles::ParticleCount; ++made)
        {
            const auto created = particles.CreateChecked(Client::Particles::FireParticle, glm::vec3(0.0f),
                                                         glm::vec3(0.0f), glm::vec3(1.0f), 0, 1.0f, 0.0f, scratch);
            if (!created.HasValue() || !created.Value().has_value())
                return;
        }
    }

    std::vector<ModelEffect*> Arrows()
    {
        std::vector<ModelEffect*> found;
        for (ModelEffect& effect : world.Map().effects.Effects())
        {
            if (effect.alive && Client::Effects::IsArrowEffect(effect.type))
                found.push_back(&effect);
        }
        return found;
    }

    // Steps the arrow until it dies (or `limit`); returns how many steps it took.
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

    std::unique_ptr<WorldState> ownedWorld = Client::Tests::LentWorld();
    WorldState& world = *ownedWorld;
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random{Seed};
    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    Client::Particles::Particles particles;
    Client::Particles::FrameSprites sprites;
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    std::vector<std::int32_t> sounds;
    std::vector<glm::vec3> soundOrigins;
    std::vector<Client::Protocol::Outgoing> sent;
    std::function<double(float, float)> terrainHeight{};
    // 0x5f93c0's terrain height at any point; without a value, the context goes without it.
    std::optional<double> ground = 0.0;
};

// The elf with the short bow (0xdb, through the own record's left) or the crossbow (0xdc, through the right) hits a
// monster and a player, in Lorencia and Devias: the attack step reaches the end (+0x381 at 0, the hit sound and the
// write of +0x38e) with the arrow created (0x518cf0: the mouth (−10; −60; 135) rotated, life 30, scale 0.8f, speed (0;
// −70; 0), the owner, +0x8a, +0x89 and +0x8c), with its 3848 extra rand() for the own record at 0x53d9a0 and as many at
// the creation's exit. The arrow takes five steps (one light rand() each, and the 0xdb its 0x5d9/0 fire with the
// dynamic light) and on the fifth, 60 from the target, it hits: it opens the own record for the skill of slot +0x89
// and dies.
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
                EXPECT_EQ(std::ranges::count(tested.particles.All(), Client::Particles::FireParticle,
                                             &Client::Particles::Particle::type),
                          weapon.arrow == 0xdb ? 5 : 0);
            }
        }
    }
}

// Without a target (+0x3a4 at −1), in Lorencia and Devias: the arrow leaves the same way (0x50377d, with the flag at 0)
// and, with nobody within 100, flies its 30 steps with one rand() each and dies of life. The shooting hero does not
// stop it.

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
            // The head, the read of 0x502b12 (the flag), the arrow and the write of +0x38e.
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

// Another elf, with the bow in hand (+0x2d8 at 0xa12: no own record at 0x53d9a0), shoots the hero: 0x517850 with class
// 0 does not hit the hero, who is in the way, and the arrow flies its 30 steps.

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

// Mounted bows and crossbows shoot like on foot: the 0x20a's eight shooting actions (on foot, with wings, 0x36/0x37,
// on Uniria or Dinorant 0x5e and on Fenrir 0x61), with and without a target, in Lorencia, Devias and one map per mount
// (Atlans for Uniria, Icarus for Dinorant and Tarkan for Fenrir).

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

// 0x5269ed..0x526a9a: weapon 4/6 shoots the 0xe1, which each step, after the light's rand() and its two sprites, draws
// the random point four times ((& 31) − 16; (& 63) − 32; (& 31) − 16) and the flower 0x608 + rand() % 3 at scale 1
// with its angles and light (0x526a8b; the flower is born with five more rand(), 0x542c4b: life rand() % 10 + 15,
// scale ((rand() & 7) + 4) · 0.03f, light 1 and speed ((& 31) − 16; (& 31) − 16; (& 31) − 32) · 0.1f). With the
// particle table full, the same rand() for the point and the type. The step starts with the CrtRandom at 0x1234; the
// expected values come from arrowflowers.py (0x5269ed..0x526a9a and 0x542c4b in a separate x87), not from
// CreateChecked.
TEST(ArrowEffectsTest, TheNatureArrowScattersFourFlowers)
{
    struct Flower
    {
        std::int32_t type;
        glm::vec3 point;
        std::int32_t life;
        std::uint32_t scale;
        std::array<std::uint32_t, 3> velocity;
    };
    constexpr std::array<Flower, 4> Flowers{{
        {0x608, {1013, 1021, 291}, 19, 0x3e570a3du, {0x0u, 0x3fa66667u, 0xc019999au}},
        {0x60a, {986, 982, 299}, 21, 0x3df5c28fu, {0x3f666667u, 0x3ecccccdu, 0xc02ccccdu}},
        {0x609, {1011, 982, 289}, 20, 0x3e8a3d70u, {0xbf19999au, 0x3f800000u, 0xbfc00000u}},
        {0x609, {987, 1026, 284}, 22, 0x3e3851ebu, {0xbf800000u, 0xbf19999au, 0xc0266667u}},
    }};
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        ArrowWorld tested(0);
        const Weapon weapon{"4/6", 0xffff, 0x806, 0x2e, 0};
        ActorState& hero = tested.Archer(weapon, weapon.action, NoTarget);
        const auto struck = StepTheAttack(hero, HeroSlot, tested.Context());
        ASSERT_TRUE(struck.HasValue()) << struck.Error();
        const auto arrows = tested.Arrows();
        ASSERT_EQ(arrows.size(), 1u);
        ModelEffect& arrow = *arrows.front();
        ASSERT_EQ(arrow.type, 0xe1);
        arrow.transform.position = {1000.0f, 1000.0f, 300.0f};
        arrow.transform.angles = {0.0f, 15.0f, 0.0f};
        if (full)
            tested.FillTheParticles();
        tested.random = CrtRandom(0x1234u);

        const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(std::ranges::count_if(tested.sprites.Sprites(), [](const auto& sprite) { return sprite.alive; }), 2);
        EXPECT_EQ(arrow.transform.angles.y, 45.0f);
        if (full)
        {
            // The light, plus the four of each flower's point and type: 17.
            EXPECT_EQ(tested.random.State(), 0x0e390577u);
            EXPECT_TRUE(std::ranges::none_of(tested.particles.All(), [](const Client::Particles::Particle& particle)
                                             { return particle.type >= 0x608 && particle.type <= 0x60a; }));
            continue;
        }
        EXPECT_EQ(tested.random.State(), 0x8946c5bbu);
        for (std::size_t index = 0; index < Flowers.size(); ++index)
        {
            SCOPED_TRACE(index);
            const Flower& expected = Flowers[index];
            const Client::Particles::Particle& flower = tested.particles.All()[index];
            ASSERT_TRUE(flower.alive);
            EXPECT_EQ(flower.type, expected.type);
            EXPECT_EQ(flower.subtype, 0);
            EXPECT_EQ(flower.position, expected.point);
            EXPECT_EQ(flower.angles, (glm::vec3{0.0f, 15.0f, 0.0f}));
            EXPECT_EQ(flower.light, glm::vec3(1.0f));
            EXPECT_EQ(std::bit_cast<std::uint32_t>(flower.scale), expected.scale);
            EXPECT_EQ(flower.life, expected.life);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(flower.velocity.x), expected.velocity[0]);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(flower.velocity.y), expected.velocity[1]);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(flower.velocity.z), expected.velocity[2]);
        }
        EXPECT_FALSE(tested.particles.All()[4].alive);
    }
}

// 0x51c542..0x51c56c and 0x51c6dd..0x51c713: weapon 4/21 shoots the 0xf9, born with joint 0x610/0xe (width 50, key
// 40), and 4/20 the 0x11c, which after its rand() % 360 is born with the 0x610/0xc (width 40, key 80); both owned by
// the arrow and born at its position with its angles (0x531fa3..0x53200e: the key is the life, 16 points and speed 70;
// the light and the blend, those of its subtype). Neither draws rand(): the shot goes on like the others'.
TEST(ArrowEffectsTest, TheViperAndTheTwentiethArrowsAreBornWithTheirTrail)
{
    struct Case
    {
        std::string_view name;
        std::uint16_t left;
        std::int16_t arrow;
        std::int32_t subtype;
        float width;
        std::int32_t life;
        glm::vec3 light;
        std::uint8_t blend;
        int creationDraws;
    };
    const std::array cases{
        Case{"4/21", 0x815, 0xf9, 0xe, 50.0f, 40, glm::vec3(1.0f), 1, 0},
        Case{"4/20",
             0x814,
             0x11c,
             0xc,
             40.0f,
             80,
             {std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3e4ccccdu), std::bit_cast<float>(0x3f4ccccdu)},
             4,
             1}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        ArrowWorld tested(0);
        const Weapon weapon{probe.name, 0xffff, probe.left, 0x2e, probe.arrow};
        ActorState& hero = tested.Archer(weapon, weapon.action, NoTarget);

        const auto struck = StepTheAttack(hero, HeroSlot, tested.Context());

        ASSERT_TRUE(struck.HasValue()) << struck.Error();
        EXPECT_EQ(DrawsSince(Seed, tested.random), CountingWithoutTarget + 2 * SkillRead + ShotReads + OwnRecord +
                                                       probe.creationDraws + ArrowCreation + SkillRead);
        const auto arrows = tested.Arrows();
        ASSERT_EQ(arrows.size(), 1u);
        const ModelEffect& arrow = *arrows.front();
        EXPECT_EQ(arrow.type, probe.arrow);
        const auto& joints = tested.world.DrawTail().joints;
        const auto trail =
            std::ranges::find_if(joints, [](const auto& joint) { return joint.alive && joint.type == 0x610; });
        ASSERT_NE(trail, joints.end());
        EXPECT_EQ(trail->subtype, probe.subtype);
        EXPECT_EQ(trail->ownerKind, Client::Kernel::JointOwnerKind::ModelEffect);
        EXPECT_EQ(trail->ownerContext, &arrow);
        EXPECT_EQ(trail->position, arrow.transform.position);
        EXPECT_EQ(trail->targetPosition, arrow.transform.position);
        EXPECT_EQ(trail->angles, arrow.transform.angles);
        EXPECT_EQ(trail->life, probe.life);
        EXPECT_EQ(trail->key, 0);
        EXPECT_EQ(trail->width, probe.width);
        EXPECT_EQ(trail->maximumCount, 16);
        EXPECT_EQ(trail->speed, 70.0f);
        EXPECT_EQ(trail->light, probe.light);
        EXPECT_EQ(trail->blend, probe.blend);
        EXPECT_EQ(std::ranges::count_if(joints, [](const auto& joint) { return joint.alive; }), 1);
    }
}

// 0x51c6dd..0x51c713: the 0x11c's creation sets +0x64 to 1 and the angles' y to rand() % 360 before releasing its
// 0x610/0xc into the first free slot, born with those angles; then the own record.
TEST(ArrowEffectsTest, TheTwentiethArrowTurnsAtRandomBeforeItsTrail)
{
    ArrowWorld tested(0);
    const Client::Kernel::ModelEffectArguments arguments{
        .type = 0x11c, .position = {2000.0f, 3000.0f, 400.0f}, .light = glm::vec3(1.0f), .subtype = 0};
    const auto made = Client::Effects::CreateTheArrowEffect(arguments, tested.world, tested.random);
    ASSERT_TRUE(made.HasValue()) << ErrorOf(made);
    const auto arrows = tested.Arrows();
    ASSERT_EQ(arrows.size(), 1u);
    const ModelEffect& arrow = *arrows.front();
    CrtRandom twin{Seed};
    EXPECT_EQ(arrow.transform.angles.y, static_cast<float>(twin.Next() % 360));
    EXPECT_EQ(arrow.objectState.blendMesh, 1);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 1 + OwnRecord);
    const auto& joints = tested.world.DrawTail().joints;
    ASSERT_TRUE(joints[0].alive);
    EXPECT_EQ(joints[0].subtype, 0xc);
    EXPECT_EQ(joints[0].angles, arrow.transform.angles);
    EXPECT_FALSE(joints[1].alive);
}

// 0x526b06..0x526bcb: with an even life, the 0x11c draws the random point three times ((& 31) − 16; (& 63) − 32; (&
// 31) − 16) and leaves the 0x60f/5 with scale 0.2f, its angles (already turned 60) and the light (0.4f; 0.2f; 1); with
// an odd life, nothing. Each is repeated in a twin pool with the same CrtRandom.
TEST(ArrowEffectsTest, TheTwentiethArrowLeavesThreeShinesOnEvenLives)
{
    for (const std::int32_t life : {20, 21})
    {
        SCOPED_TRACE(life);
        ArrowWorld tested(0);
        auto& arrow = tested.world.Map().effects.Effects().front();
        arrow.alive = true;
        arrow.type = 0x11c;
        arrow.life = life;
        arrow.transform.position = {1000.0f, 1000.0f, 300.0f};
        arrow.transform.angles = {0.0f, 15.0f, 0.0f};

        const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        CrtRandom expected(Seed);
        static_cast<void>(expected.Next());
        auto twin = std::make_unique<Client::Particles::Particles>();
        const glm::vec3 violet{std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3e4ccccdu), 1.0f};
        const std::size_t shines = life % 2 == 0 ? 3 : 0;
        for (std::size_t index = 0; index < shines; ++index)
        {
            SCOPED_TRACE(index);
            glm::vec3 point{static_cast<float>((expected.Next() & 31) - 16),
                            static_cast<float>((expected.Next() & 63) - 32),
                            static_cast<float>((expected.Next() & 31) - 16)};
            point += glm::vec3{1000.0f, 1000.0f, 300.0f};
            Client::Particles::PortableParticleTrigonometry trigonometry;
            const float clock = 0.0f;
            const auto replayed =
                twin->CreateChecked(0x60f, point, {0.0f, 75.0f, 0.0f}, violet, 5, std::bit_cast<float>(0x3e4ccccdu),
                                    std::cref(clock), expected, 0, trigonometry);
            ASSERT_TRUE(replayed.HasValue() && replayed.Value() == std::optional<std::size_t>{index});
            const Client::Particles::Particle& shine = tested.particles.All()[index];
            const Client::Particles::Particle& reference = twin->All()[index];
            ASSERT_TRUE(shine.alive);
            EXPECT_EQ(shine.type, 0x60f);
            EXPECT_EQ(shine.subtype, 5);
            EXPECT_EQ(shine.position, point);
            EXPECT_EQ(shine.angles, reference.angles);
            EXPECT_EQ(shine.light, reference.light);
            EXPECT_EQ(shine.scale, reference.scale);
            EXPECT_EQ(shine.life, reference.life);
            EXPECT_EQ(shine.rotation, reference.rotation);
            EXPECT_EQ(shine.velocity, reference.velocity);
            EXPECT_EQ(shine.owner, 0u); // no owner: 0x53fe70's last argument (0x526b72)
        }
        EXPECT_FALSE(tested.particles.All()[shines].alive);
        EXPECT_EQ(arrow.transform.angles.y, 75.0f);
        EXPECT_EQ(DrawsSince(Seed, tested.random), DrawsSince(Seed, expected));
    }
}

// 0x524300..0x52434e (the 0xe2, 4/14) and 0x526cd0..0x526d5f (the 0xe3, 4/13): after the light's rand(), the random
// point four times (three rand()) and the 0x607/1 bubble at scale 1 with the arrow's angles (light 1 from its
// creation, 0x542d96, and two rand()). The 0xe3 goes on with its 0x5f2/0 smoke at the arrow's position.
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
        auto twin = std::make_unique<Client::Particles::Particles>();
        CrtRandom expected = before;
        static_cast<void>(expected.Next());
        for (std::size_t index = 0; index < 4; ++index)
        {
            SCOPED_TRACE(index);
            expected.Discard(3);
            const Client::Particles::Particle& bubble = tested.particles.All()[index];
            const auto replayed =
                twin->CreateChecked(0x607, bubble.start, angles, glm::vec3(0.25f), 1, 1.0f, 0.0f, expected);
            ASSERT_TRUE(replayed.HasValue() && replayed.Value() == std::optional<std::size_t>{index});
            const Client::Particles::Particle& reference = twin->All()[index];
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
        const Client::Particles::Particle& smoke = tested.particles.All()[4];
        EXPECT_TRUE(smoke.alive);
        EXPECT_EQ(smoke.type, 0x5f2);
        EXPECT_EQ(smoke.subtype, 0);
        EXPECT_EQ(smoke.start, position);
    }
}

// The fall of arrow 0xe2 (4/14, 0x52435d..0x52443f), with the particle pool full so that its 0x607/1 bubbles (CL11)
// are not created: the step draws the light's rand() and the point's three for each of the four bubbles. z adds +0xe4
// (−10 at creation) and +0xe4 loses 1; with the terrain (0x5f93c0) above z, z goes to the terrain, +0xe4 times −0.4f
// and the speed's y times 0.4f; with the terrain equal to z it does not bounce. On the last life it releases
// 0x51fe10's sparks (40 rand() with the pool full) and, with the hero as owner and without subtype 0x63, it stops at
// 0x517a20 (0x524405, milestone 4); without the terrain height it stops at 0x524382. In Lorencia, Devias and Icarus
// (10).

// 0x51a173, 0x51a24a and 0x51bf35: the arrow of 4/16, 4/18 and 4/19 leaves a 0x618/5 owned by it, from its position (z
// + 130, also kept in its +0x40), with its angles and width 100, no rand(): speed 0, life 999999999, 10 points and
// light (0.5; 0.1; 1) (0x532238..0x53230e). In the step it follows the arrow's +0x40 and, with the arrow dead, dies
// without the tail or the life (0x53820e).
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
        EXPECT_EQ(beam->ownerKind, Client::Kernel::JointOwnerKind::ModelEffect);
        EXPECT_EQ(beam->position, arrow.point40);
        EXPECT_EQ(beam->angles, arrow.transform.angles);
        EXPECT_EQ(beam->width, 100.0f);
        EXPECT_EQ(beam->speed, 0.0f);
        EXPECT_EQ(beam->life, 999999999);
        EXPECT_EQ(beam->maximumCount, 10);
        EXPECT_EQ(beam->light, glm::vec3(0.5f, std::bit_cast<float>(0x3dcccccdu), 1.0f));

        arrow.point40 = {1.0f, 2.0f, 3.0f};
        ASSERT_TRUE(Client::Joints::StepJoints(joints, tested.random).HasValue());
        EXPECT_EQ(beam->position, glm::vec3(1.0f, 2.0f, 3.0f));
        EXPECT_EQ(beam->life, 999999998);
        EXPECT_EQ(beam->count, 1);

        arrow.alive = false;
        ASSERT_TRUE(Client::Joints::StepJoints(joints, tested.random).HasValue());
        EXPECT_FALSE(beam->alive);
        EXPECT_EQ(beam->life, 999999998);
        EXPECT_EQ(beam->count, 1);
    }
}

// 0x51a3d0..0x51a417: arrow 0xf8 of 4/17 leaves four 0x60f/0xb owned by it, from its start (+0x17c), with angles (0,
// 0, z), width 50 requested (the creation sets 30) and skill 1 in the second; each draws its rand() % 3000 (vx). With
// the pool full, none and no rand().
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
        EXPECT_EQ(joint.ownerKind, Client::Kernel::JointOwnerKind::ModelEffect);
        EXPECT_EQ(joint.position, arrow.start);
        EXPECT_EQ(joint.targetPosition, arrow.start);
        EXPECT_EQ(joint.angles, glm::vec3(0.0f, 0.0f, arrow.transform.angles.z));
        EXPECT_EQ(joint.skill, slot == 1 ? 1 : 0);
        EXPECT_EQ(joint.width, 30.0f);
        EXPECT_EQ(joint.life, 30);
        EXPECT_EQ(joint.maximumCount, 15);
    }
    EXPECT_FALSE(joints[4].alive);

    // The creation alone (0x51a2bd..0x51a417, without the attack): the four rand() % 3000 of vx (0x533c06), in order,
    // before those of 0x51a1b0.
    auto alone = std::make_unique<ArrowWorld>(0);
    glm::vec3 angles{15.0f, 25.0f, 35.0f};
    const Client::Kernel::ModelEffectArguments args{
        .type = 0xf8, .position = {10.0f, 20.0f, 30.0f}, .angles = angles, .light = {1.0f, 1.0f, 1.0f}, .subtype = 0};
    const auto made = Client::Effects::CreateTheArrowEffect(args, alone->world, alone->random, &angles);
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

// Skill 0x2e (0x53df2a), with and without a target, in Lorencia, Devias and Icarus (10): no arrow comes out, but effect
// 0x13a owned by the target, with the shooter's +0x8a, the slot and the flag; life 20, scale 1.8f, +0x64 at −2, speed
// (0; −30; 0), light (0.3f; 0.8f; 1), the mouth (−10; −80; 200) rotated and the angles' x at −30. Then its joint
// 0x632/4 (0x51e01a: the 0x13a's position and angles, owned by the effect, width 40, key 0x32, no rand()), the batch
// count in +0x8c (0x51e01f, with the own record) and, back at 0x53e75b, the batch plus one. With a target, the step
// shoots twice (0x504060 and case 0x2e of 0x504453, with sound 0x26e): two 0x13a, each with its joint.

TEST(ArrowEffectsTest, TheBlastSkillLeavesItsEffectAndItsJoint)
{
    for (const std::int32_t map : {0, 2, 10})
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

            ASSERT_TRUE(struck.HasValue()) << ErrorOf(struck);
            // Each shot: the reads, 0x53d9a0's own record, the 0x13a count's and the batch's.
            constexpr int BlastShot = ShotReads + OwnRecord + OwnRecord + OwnRecord;
            const int draws = withTarget ? CountingWithTarget + SkillRead + 2 * BlastShot + AfterTheArrow
                                         : CountingWithoutTarget + 2 * SkillRead + BlastShot + SkillRead;
            EXPECT_EQ(DrawsSince(Seed, tested.random), draws);
            const std::size_t shots = withTarget ? 2 : 1;
            EXPECT_EQ(tested.world.OwnCharacter().bytef04, static_cast<std::uint8_t>(9 + shots));
            EXPECT_EQ(tested.world.Runtime().lastShotSkill, 0x2eu);
            EXPECT_TRUE(tested.Arrows().empty());
            // With a target, the 0x26e of case 0x2e (0x5044ad) and the hit's (0x505224, 0x46 plus rand() & 3).
            ASSERT_EQ(tested.sounds.size(), withTarget ? 2u : 0u);
            if (withTarget)
            {
                EXPECT_EQ(tested.sounds.front(), 0x26e);
            }
            const auto effects = tested.world.Map().effects.Effects();
            const auto& joints = tested.world.DrawTail().joints;
            std::size_t blasts = 0;
            for (const ModelEffect& effect : effects)
            {
                if (!effect.alive || effect.type != 0x13a)
                    continue;
                SCOPED_TRACE(blasts);
                EXPECT_EQ(effect.word8c, static_cast<std::uint16_t>(9 + blasts));
                const auto& joint = joints[blasts];
                ASSERT_TRUE(joint.alive);
                EXPECT_EQ(joint.type, 0x632);
                EXPECT_EQ(joint.subtype, 4);
                EXPECT_EQ(joint.ownerContext, &effect);
                EXPECT_EQ(joint.width, 40.0f);
                EXPECT_EQ(joint.key, 0x32);
                EXPECT_EQ(joint.position, effect.transform.position);
                EXPECT_EQ(joint.angles, effect.transform.angles);
                ++blasts;
            }
            EXPECT_EQ(blasts, shots);
            EXPECT_FALSE(joints[shots].alive);
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

// Skill 0x18 without a target (the flag at 1, 0x53e2e0), in Lorencia and Devias: three arrows with +0x24 at a, a + 15
// and a − 15, which returns to a; with 0xa24 or 0xa25 in +0x2b8, four at a + 5, a + 15, a − 5 and a − 15, and +0x24
// stays at a + 15 (Q-C1218). Each shot with 0x53d9a0's own record and the creation exit's, the arrows with +0x88 at 1,
// and the batch count; with 0xa25, H3's shine follows (0x53e642).

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
            // The fixture offers no joints: the shine's fifteen attempts keep the caller's two rand() and each
            // particle's five; the capacity test below also covers available joints.
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

// The flag's arrow hits through 0x5201ed, in Lorencia and Devias, with the crossbow (0xdc, which only hits): with an
// even rand(), the hero as owner and the slot's skill 0x18, 0x517a20 sends C1 10 with the monster within 40 of the
// arrow (cell 9; 9, batch +0x8c and its key) and the arrow dies with sound rand() % 4 + 0x4b (CL10). With an odd
// rand(), or another owner, nothing else happens and the arrow goes on.

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
            Client::Protocol::Skills::SkillHitsSend expected{.skill = 0x18, .x = 9, .y = 9, .volley = 9, .hits = 1};
            expected.keys[0] = 0x0789;
            EXPECT_TRUE(Client::Tests::SameBytes(tested.sent[0].packet.Bytes(),
                                                 Client::Protocol::Skills::WriteSkillHits(expected).packet.Bytes()));
        }
    }
}

// 0x53e2b0 with skill 0x34: fires 0xdb/2, creates its 0x105/0 trail and keeps the batch of four 0x610/0..3 joint
// attempts and sound 0x7a at any capacity (0x51a08a..0x51a101).

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

            const auto fired = Client::Effects::FireTheArrow(hero, nullptr, HeroSlot, 3, 0, 0x34, tested.Context());

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
            EXPECT_EQ(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{0x7a});
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
                EXPECT_EQ(joints[slot].ownerKind, Client::Kernel::JointOwnerKind::ModelEffect);
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

// 0x52055f: arrow 0x34 hits the monster, sends C1 10 and tries 0x60f/6. The two-step pause and the impact are the same
// with the trail active or without a slot to create it.

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

        const auto fired = Client::Effects::FireTheArrow(hero, nullptr, HeroSlot, 3, 0, 0x34, tested.Context());

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
        Client::Protocol::Skills::SkillHitsSend expected{.skill = 0x34, .x = 9, .y = 9, .volley = 9, .hits = 1};
        expected.keys[0] = 0x0789;
        EXPECT_TRUE(Client::Tests::SameBytes(tested.sent[0].packet.Bytes(),
                                             Client::Protocol::Skills::WriteSkillHits(expected).packet.Bytes()));

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
                EXPECT_EQ(impact.ownerKind, Client::Kernel::JointOwnerKind::ModelEffect);
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

// Arrow 0xdb with subtype 3 or 4 (the siege objects', CL9.5), through 0x518cf0 directly, in Lorencia, Devias and map
// 30: life 40, scale 1.5, +0xe4 of (rand() % 100 + 50) · 0x3d888889, the start at the hero and the speed and the
// angles' x from its rand() (two with 3, one with 4), and the creation's exit.

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
            const Client::Kernel::ModelEffectArguments arguments{
                .type = 0xdb, .position = from, .light = glm::vec3(1.0f), .subtype = subtype};

            const auto created = Client::Effects::CreateTheArrowEffect(arguments, tested.world, tested.random);

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

// The step of arrow 0xdb/3 and /4 (0x52413a), in Lorencia, Devias and map 30: with 3, +0xe4 is added to the angles' x,
// capped at 50; with the cell without 0x08, terrain higher than z sets it to the height + 10 and stops the arrow
// (lower, nothing; with 0x08, the height is not asked). The 0x5d9/5 fire is born before the common tail: three rand()
// besides the light's, no hit. Then the arrow moves and loses a life (0x52cb98).

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
            tested.terrain.attributes = std::move(attributes);
            int heights = 0;
            tested.terrainHeight = [&heights, &probe](float, float)
            {
                ++heights;
                return probe.height;
            };
            const Client::Kernel::ModelEffectArguments arguments{.type = 0xdb,
                                                                 .position = {2000.0f, 3000.0f, 400.0f},
                                                                 .light = glm::vec3(1.0f),
                                                                 .subtype = probe.subtype};
            ASSERT_TRUE(Client::Effects::CreateTheArrowEffect(arguments, tested.world, tested.random).HasValue());
            ModelEffect& arrow = *tested.Arrows().front();
            arrow.transform.angles.x = probe.subtype == 3 ? 48.0f : 60.0f;
            arrow.turnDirection = 5.0f;
            const glm::vec3 velocity = arrow.velocity;
            // CRT 1: light 41, speed 18467, scale 6334 and turn 26500, in that order.
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
            EXPECT_EQ(fire.type, Client::Particles::FireParticle);
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

// Crywolf's 0xe1/1: 0x610/13 at birth and two 0x60f/5 on even lives above 25, with the CRT interleaved. It also keeps
// the fortress path, which uses the same constructor.

TEST(ArrowEffectsTest, TheFortressArrowSparklesOnEvenLives)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::int32_t life : {26, 27, 24})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", vida " << life);
            ArrowWorld tested(map);
            const Client::Kernel::ModelEffectArguments arguments{
                .type = 0xe1, .position = {2000.0f, 3000.0f, 400.0f}, .light = glm::vec3(1.0f), .subtype = 1};
            ASSERT_TRUE(Client::Effects::CreateTheArrowEffect(arguments, tested.world, tested.random).HasValue());
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
        ASSERT_TRUE(Client::Effects::CreateTheArrowEffect({.type = 0xe1, .position = {2000, 3000, 400}, .subtype = 1},
                                                          tested.world, tested.random)
                        .HasValue());
        auto arrows = tested.Arrows();
        ASSERT_EQ(arrows.size(), 1u);
        auto& arrow = *arrows[0];
        arrow.life = 26;
        const auto& joints = tested.world.DrawTail().joints;
        ASSERT_TRUE(joints[0].alive);
        EXPECT_EQ(joints[0].ownerContext, &arrow);
        EXPECT_EQ(joints[0].ownerKind, Client::Kernel::JointOwnerKind::ModelEffect);
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

// The hit reads the skill of slot +0x89 in the own record (0x5205cd..0x5207d1): with 0x33 arrow 0xdb just dies; with
// 0x34 it dies and 0x51fe10 releases its 0x5c5/2 sparks, which stop at 0x51fe9e (CL11) after the first one's two
// rand(). In Lorencia and Devias, with the target at 50.

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
                EXPECT_EQ(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{0x40});
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
        const float luminosity = Client::Effects::StepLuminosity(probe.Next(), arrow.life);

        const auto stepped = StepArrow(arrow, nullptr, tested.StepContext());

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const auto particles = tested.particles.All();
        const auto fire =
            std::ranges::find(particles, Client::Particles::FireParticle, &Client::Particles::Particle::type);
        ASSERT_NE(fire, particles.end());
        const auto times = [luminosity](float factor)
        { return static_cast<float>(static_cast<double>(luminosity) * static_cast<double>(factor)); };
        EXPECT_EQ(fire->light, glm::vec3(times(0.1f), times(0.6f), times(0.3f)));
        EXPECT_TRUE(arrow.alive);
    }
}

// Monster missiles (0x5037d0 without a target and 0x5040a9 with one, the latter without the 0x180): 0x53e2b0(c, o, 0,
// 0, 0, 0) goes to 0x53d9a0 with the hands of +0x2b8/+0x2d8, without the own record; with bow 0xa12 in the left, the
// monster's 0xdb comes out, with the creation exit's 3848 rand() as the only difference from empty hands. In Lorencia
// and Devias.

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

// 0x53e642: own retained pose and shared scale/origin; the pools' draws are interleaved.

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

// The shooting skills (0x34, 0x33 and 0x2e) go to 0x53e741: 0x53d9a0's shot (the hero's own record, the skill in
// [0x5767948], the 0x33's sound 0x82 and an arrow with its creation, which opens and closes the own record again) and
// stop at 0x53e75b's own record (milestone 4); the 0x2e (0x53df2a's effect 0x13a) is in
// TheBlastSkillLeavesItsEffectAndItsJoint, and the 0x34 does not get there: it stops at 0x4f9899. The flag of 0x18
// without a target fires 0x53e2e0's fan: three arrows with the hero's angles' z at 0, 15 and −15, which returns to 0,
// and stops at 0x53e365; with 0xa24 or 0xa25 in +0x2b8, four at 5, 15, −5 and −15, which stays at 15 (Q-C1181), and
// stops at 0x53e4dd. Each shot draws the 3848 rand() of 0x53d9a0's own record and as many at the creation's exit. The
// 0xa25's shine 0x53e642 is covered with the body, the pools and the interleaved RNG below. In Lorencia, Devias and
// Icarus (10).

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
    // Before the arrow: with a target, the head, the skill read and the two of 0x59fb80; without one, the head, the
    // read of 0x502b12 (the flag) and the same.
    const int withTarget = CountingWithTarget + SkillRead + ShotReads;
    const int withoutTarget = CountingWithoutTarget + 2 * SkillRead + ShotReads;
    const std::array cases{
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

// The 0x18's fan with the hero's angles' z at 100.3f: each shift is added in the x87 and stored as float
// (0x53e31b..0x53e362), so each arrow's z is the previous one's sum rounded to float.

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

// The hit reads the skill of slot +0x89 in the own record (0x5205cd..0x5207d1): with 0x33 arrow 0xdb just dies; with
// 0x34 it dies and 0x51fe10 releases its 0x5c5/2 sparks, which stop at 0x51fe9e (CL11) after the first one's two
// rand(). In Lorencia and Devias, with the target at 50.

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

// Arrow 0xdb with subtype 5 (created by milestone 4) does not stop in the step: its 0x5d9/0 fire carries (light · 0.1f;
// light · 0.6f; light · 0.3f) instead of the dynamic light's color (0x5240be..0x5240fa).

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
            actor.bodyPose.resize(1, Client::Math::BoneTransform(1.0f));
            actor.bodyPose[0][3] = {3.0f, 4.0f, 5.0f};
            tested->world.PlayerModel() = {.scale = 2.0f, .origin = {100.0f, 200.0f, 300.0f}};
            // The shot goes on after the shine even without a slot for arrow 0x111.
            for (ModelEffect& effect : tested->world.Map().effects.Effects())
                effect.alive = true;
            if (particlesFull)
                for (Client::Particles::Particle& particle : tested->particles.All())
                    particle.alive = true;
            std::vector<Client::Kernel::JointState> joints(100);
            if (jointsFull)
                for (Client::Kernel::JointState& joint : joints)
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
            const auto shot = Client::Effects::FireTheArrow(actor, nullptr, OtherSlot, 0, 0, 0, context);
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
                    EXPECT_EQ(particle.type, Client::Particles::FairySparkParticle);
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

// 0x517850 walks slots 0..399: it includes 399 and leaves out the ghost record 400. On the plane, the 100 border
// counts; nextafter outside does not, and the native comparison's unordered counts.

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
// 0x524405: the final send happens before the light/hit, even when the particles have no slot.
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
    const auto clear = Client::Network::Xor32Decode(tested.sent[0].packet);
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
        const Client::Kernel::ModelEffectArguments args{.type = static_cast<std::int16_t>(type),
                                                        .position = {100.0f, 200.0f, 300.0f},
                                                        .angles = {},
                                                        .light = {1.0f, 1.0f, 1.0f},
                                                        .subtype = subtype};
        const auto made = Client::Effects::CreateTheArrowEffect(args, tested.world, tested.random);
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
        // 0x52ccbe: the 0xf8 does not move; the speed moves its start.
        EXPECT_EQ(arrow.transform.position.y, type == 0xf8 ? 1000.0f : 930.0f);
        EXPECT_EQ(DrawsSince(Seed, tested.random), type == 0x10e ? 2 : type == 0x11c ? 10 : 1);
    }
}

// 0x52594c..0x5259a4: after the light's rand() (0x5208c5), the step of arrow 0x111 turns y by +30 and releases three
// 0x5f2/13 smokes at its position, with those angles, light 1 (0x520921) and scale 1, three rand() each; then it moves.
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
    auto twin = std::make_unique<Client::Particles::Particles>();
    CrtRandom expected(Seed);
    static_cast<void>(expected.Next());
    for (std::size_t index = 0; index < 3; ++index)
    {
        SCOPED_TRACE(index);
        const auto replayed = twin->CreateChecked(0x5f2, {1000.0f, 1000.0f, 1000.0f}, {0.0f, 45.0f, 0.0f},
                                                  glm::vec3(1.0f), 13, 1.0f, 0.0f, expected);
        ASSERT_TRUE(replayed.HasValue() && replayed.Value() == std::optional<std::size_t>{index});
        const Client::Particles::Particle& ember = tested.particles.All()[index];
        const Client::Particles::Particle& reference = twin->All()[index];
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

// 0x51a347..0x51a3b7: the 0xf8/1 writes the caller's angles (0, 0, z) before its three 0x60f/0x19 joints (creation
// 0x533db8, one rand() % 3000 each, owned by the arrow) and goes on to 0x51a1b0; with the table full, no joints.
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
        const Client::Kernel::ModelEffectArguments args{.type = 0xf8,
                                                        .position = {10.0f, 20.0f, 30.0f},
                                                        .angles = angles,
                                                        .light = {1.0f, 1.0f, 1.0f},
                                                        .subtype = 1};
        const auto made = Client::Effects::CreateTheArrowEffect(args, tested.world, tested.random, &angles);
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
            EXPECT_EQ(joint.ownerKind, Client::Kernel::JointOwnerKind::ModelEffect);
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

// 0x526c05: with the particles full, arrow 0x11c leaves the 0x114/3 wave (0x51c31f) with the key at 0, no rand(), and
// goes on with its light and its hit.
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
    const Client::Kernel::ModelEffectArguments args{
        .type = 0x10e, .position = {10.0f, 20.0f, 30.0f}, .angles = {}, .light = {1.0f, 1.0f, 1.0f}, .subtype = 2};
    const auto made = Client::Effects::CreateTheArrowEffect(args, tested.world, tested.random);
    ASSERT_TRUE(made.HasValue()) << ErrorOf(made);
    EXPECT_EQ(arrow.point40, (glm::vec3{10.0f, 20.0f, 160.0f}));
    EXPECT_EQ(arrow.state, 1);
    EXPECT_EQ(arrow.dword100, 0);
    EXPECT_EQ(arrow.word8c, 11);
    EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord);
}

// 0x4039a0 uses 0xed/1, the arrows' prefix but with pitch −90, vy −42 and vz 4; step 0x524210 lights red and goes on
// through 0x52cb98 without 0x520170's hit.
TEST(ArrowEffectsTest, TheTourProjectileCreatesAndStepsWithoutAttackingActors)
{
    for (const std::uint8_t depth : {std::uint8_t{0}, std::uint8_t{1}})
    {
        auto tested = std::make_unique<ArrowWorld>(34);
        tested->world.Runtime().ownRecordDepth = depth;
        tested->world.OwnCharacter().bytef04 = 9;
        const Client::Kernel::ModelEffectArguments arguments{.type = 0xed,
                                                             .position = {1000, 1000, 0},
                                                             .angles = {},
                                                             .light = {2, 3, 4},
                                                             .subtype = 1,
                                                             .owner = &tested->Hero()};
        const auto created = Client::Effects::CreateTheArrowEffect(arguments, tested->world, tested->random);
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
    tested.world.Runtime().castleFlags.siegeActive = true;
    tested.terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    tested.terrain.attributes->cells[513] = 0x100;
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

// 0x523f33..0x523f72: the 0xf8/1 turns its y by 60 and, at life 13, creates the 0x105/3 pulse owned by it (0x51a01b:
// scale 10 and light (0.9; 0.4; 0.6) for its four 0x610 joints with the ninth argument at 3, sound 0x7a and scale at
// 1); the tail (0x52ccbe) moves the start and not the position. With the table full, neither the pulse nor the sound.
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
            EXPECT_NE(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{0x7a});
            continue;
        }
        EXPECT_EQ(joints, 4);
        EXPECT_EQ(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{0x7a});
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

// With the joints full, the 0xf8/1 is born without stopping: it does not move, its start advances 60 per step and at
// life 13 it releases the 0x105/3 pulse at its position, until its 30 steps run out.
TEST(ArrowEffectsTest, CelestialArrowStaysPutWhileItsStartTravelsForThirtySteps)
{
    ArrowWorld tested(34);
    for (auto& joint : tested.world.DrawTail().joints)
        joint.alive = true;
    const Client::Kernel::ModelEffectArguments args{
        .type = 0xf8, .position = {1000, 3000, 0}, .angles = {}, .light = {1, 1, 1}, .subtype = 1};
    const auto created = Client::Effects::CreateTheArrowEffect(args, tested.world, tested.random);
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
    EXPECT_EQ(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{0x7a});
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
    EXPECT_EQ(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{0x7a});
    EXPECT_TRUE(std::ranges::any_of(tested.lighting->cells, [](const auto& light) { return light[0] > 0; }));
}
