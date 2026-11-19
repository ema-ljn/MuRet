#include "Scenes/AttackSkillCases.h"

#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Scenes/ActorState.h"
#include "Scenes/ArrowEffects.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HighModelEffects.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ModelEffects.h"
#include "World/KalimaWorld.h"
#include "World/KalimaWaves.h"
#include "World/Particles.h"
#include "World/WizardModelEffects.h"
#include "World/WorldTerrain.h"

#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Scenes::StepTheHeadSkill, StepTheTargetSkill y SparkTheTarget: los casos de habilidad del paso del ataque
// (0x4fdeeb, 0x504372 y 0x5042ce). 06, "El paso del ataque".
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::CharacterStepContext;
using Client::Scenes::JointState;
using Client::Scenes::SparkTheTarget;
using Client::Scenes::StepTheHeadSkill;
using Client::Scenes::StepTheTargetSkill;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 0x2468ace1u;
constexpr std::size_t HeroSlot = 42;
constexpr std::size_t StrikerSlot = 5;
constexpr std::size_t VictimSlot = 9;
// El registro propio cerrado en 0: los 3.848 de 0x40b4d0.
constexpr int OwnRecord = Client::Render::Models::OwnRecordCloseRandoms;
// Una lectura de +0x38e: un rand() al cerrarse en 0.
constexpr int SkillRead = 1;
// La entrada de Skill.bmd de 0x5526f0, cerrada en 0.
constexpr int SkillEntry = static_cast<int>(Client::Data::Tables::SkillRecordSize);
constexpr std::array<std::int32_t, 3> Maps{0, 2, 7};

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

// Skill.bmd con el alcance (+0x26) de la 0x4c.
Client::Data::Tables::SkillTable SkillsWithRange(std::uint8_t range)
{
    constexpr std::size_t AroundSkill = 0x4c;
    constexpr std::size_t RangeOffset = 0x26;
    std::vector<std::byte> plain(Client::Data::Tables::SkillTableSize);
    plain[AroundSkill * Client::Data::Tables::SkillRecordSize + RangeOffset] = static_cast<std::byte>(range);
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Data::Tables::SkillRecordSize);
    return Client::Data::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

struct SkillWorld
{
    explicit SkillWorld(std::int32_t map = 0)
    {
        world.Runtime().map = map;
        world.Runtime().hero = HeroSlot;
        world.Runtime().heroSlot = static_cast<std::int32_t>(HeroSlot);
        world.OwnCharacter().ClearEquipmentForEntry();
        Hero() = Player(1);
        Striker() = Player(0);
        Victim() = Player(2);
    }

    static ActorState Player(std::uint8_t characterClass)
    {
        ActorState actor = Client::Tests::UnarmedPlayer(characterClass);
        actor.alive = true;
        actor.byte88 = 1;
        actor.pk = 3;
        actor.animation.action = 1;
        actor.attackTarget = 0xffff;
        actor.skillTarget4c = -1;
        return actor;
    }

    // Un monstruo vivo (+0x88 en 2) en `slot`, a `x` de o en la x.
    ActorState& Monster(std::size_t slot, float x)
    {
        ActorState& monster = world.Actors()[slot];
        monster = ActorState{};
        monster.alive = true;
        monster.byte88 = 2;
        monster.type = 0x0e;
        monster.animation.action = 1;
        monster.position = Striker().position + glm::vec3(x, 0.0f, 0.0f);
        return monster;
    }

    ActorState& Hero()
    {
        return world.Actors()[HeroSlot];
    }
    ActorState& Striker()
    {
        return world.Actors()[StrikerSlot];
    }
    ActorState& Victim()
    {
        return world.Actors()[VictimSlot];
    }

    CharacterStepContext Context()
    {
        return CharacterStepContext{.world = world,
                                    .random = random,
                                    .skills = skills,
                                    .particles = withParticles ? &world.Particles() : nullptr,
                                    .joints = withJoints ? std::span<JointState>(joints) : std::span<JointState>{},
                                    .soundRequest = [this](std::int32_t sound) { sounds.push_back(sound); },
                                    .soundAt =
                                        [this](std::int32_t sound, const glm::vec3& origin)
                                    {
                                        sounds.push_back(sound);
                                        soundOrigins.push_back(origin);
                                    },
                                    .controlDown = controlDown};
    }

    Client::Core::Expected<void, std::string> Head(std::uint8_t skill)
    {
        return StepTheHeadSkill(Striker(), StrikerSlot, skill, Context());
    }

    Client::Core::Expected<void, std::string> WithTarget(std::uint8_t skill)
    {
        return StepTheTargetSkill(Striker(), Victim(), StrikerSlot, skill, Context());
    }

    std::unique_ptr<WorldState> ownedWorld = std::make_unique<WorldState>();
    WorldState& world = *ownedWorld;
    CrtRandom random{Seed};
    std::vector<std::int32_t> sounds;
    std::vector<glm::vec3> soundOrigins;
    bool controlDown = false;
    const Client::Data::Tables::SkillTable* skills = nullptr;
    std::vector<JointState> joints = std::vector<JointState>(16);
    bool withJoints = true;
    bool withParticles = false;
};

std::vector<const Client::World::ModelEffect*> AliveEffects(const WorldState& world)
{
    std::vector<const Client::World::ModelEffect*> found;
    for (const Client::World::ModelEffect& effect : world.Map().effects.Effects())
    {
        if (effect.alive)
            found.push_back(&effect);
    }
    return found;
}

// Los casos de la cabeza que crean lo que MuRet no tiene (CL11): cortan en su alta, con los rand() de antes (0x59fb80
// con el registro propio cerrado y los dos de 0x51fe10 de la 0x0e, que corta en su primera chispa). La 0x29, la 0x27 y
// la 0x0d, del lote switch, en TheSwitchHeadCasesCreateTheirEffects; la 0x35 y la 0x41, del lote joints, en
// TheEnergyCaseHangsFourJointsAroundItsCaster y TheSpiralCaseRaisesFiveJointsFromItsEffect; la 0x31, del lote altos,
// en TheBreathCaseRaisesTheFireBreathOverTheStriker.
TEST(AttackSkillCasesTest, TheHeadCasesCutAfterTheirDraws)
{
    struct Case
    {
        std::uint8_t skill;
        std::string_view cut;
        int draws;
    };
    const std::array cases{
        Case{0x0e, "0x51fe9e", 2},
    };
    for (const std::int32_t map : Maps)
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", skill " << int{probe.skill});
            SkillWorld tested(map);
            tested.Striker().flags78 = 0x4000;

            const auto stepped = tested.Head(probe.skill);

            EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
            EXPECT_EQ(DrawsSince(Seed, tested.random), probe.draws);
            EXPECT_EQ(tested.Striker().flags78, 0x4000u);
            EXPECT_TRUE(tested.sounds.empty());
        }
    }
}

// 0x4fe795..0x4fe981: la 0x35 cuelga de o cuatro 0x619/0xa de ancho 15 con los ángulos (−45, 0, 45, 405, 225 y 450),
// que nacen en o; su primer tramo queda en el punto (sen 4,5 · 80; cos 4,5 · 80; 300) o (cos 22,5 · 80; sen 22,5 · 80;
// 300) más o. Suena 0x7f, sin rand(). Los puntos, por sus bits: FSIN/FCOS a 64 bits, el producto por 80 y la suma con
// o en PC53 y el fstp a float. Con el lugar nuevo (ancho 0 en 0x530afa), las cuatro esquinas caen justo en el punto.
TEST(AttackSkillCasesTest, TheEnergyCaseHangsFourJointsAroundItsCaster)
{
    SkillWorld tested;
    tested.Striker().position = {1000.0f, 2000.0f, 50.0f};

    const auto stepped = tested.Head(0x35);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    const std::array<float, 4> turns{45.0f, 405.0f, 225.0f, 450.0f};
    for (std::size_t index = 0; index < turns.size(); ++index)
    {
        SCOPED_TRACE(index);
        const JointState& joint = tested.joints[index];
        ASSERT_TRUE(joint.alive);
        EXPECT_EQ(joint.type, 0x619);
        EXPECT_EQ(joint.subtype, 0xa);
        EXPECT_EQ(joint.width, 15.0f);
        EXPECT_EQ(joint.angles, glm::vec3(-45.0f, 0.0f, turns[index]));
        EXPECT_EQ(joint.position, tested.Striker().position);
        EXPECT_EQ(joint.ownerContext, &tested.Striker());
        // 921,7976 y 1983,1364 (4,5 rad); 930,1356 y 1961,0260 (22,5 rad).
        const glm::vec3 from =
            index < 2 ? glm::vec3(std::bit_cast<float>(0x4466730cu), std::bit_cast<float>(0x44f7e45du), 350.0f)
                      : glm::vec3(std::bit_cast<float>(0x446888aeu), std::bit_cast<float>(0x44f520d5u), 350.0f);
        for (const glm::vec3& corner : joint.points[0])
            EXPECT_EQ(corner, from);
    }
    EXPECT_FALSE(tested.joints[4].alive);
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x7f});
}

// 0x4feb3b..0x4febd3: la 0x41 crea el efecto 0x62c en el punto (0, −90, −50) de o (80 más alto en la acción 0x51), con
// vida 0, y su caso (0x51f25e) los cinco 0x62c: /1, /0 de ancho 250, /2, /3 y /4. Cuatro rand(), los de la tanda de la
// /1..4 (0x53479d). Suena 0x1cd.
TEST(AttackSkillCasesTest, TheSpiralCaseRaisesFiveJointsFromItsEffect)
{
    for (const std::uint8_t action : {std::uint8_t{0}, std::uint8_t{0x51}, std::uint8_t{0x5b}})
    {
        SCOPED_TRACE(int{action});
        SkillWorld tested;
        tested.Striker().position = {1000.0f, 2000.0f, 50.0f};
        tested.Striker().animation.action = action;
        const float z = action == 0x51 ? 80.0f : action == 0x5b ? 40.0f : 0.0f;

        const auto stepped = tested.Head(0x41);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(DrawsSince(Seed, tested.random), 4);
        const auto effects = AliveEffects(tested.world);
        ASSERT_EQ(effects.size(), 1u);
        EXPECT_EQ(effects.front()->type, 0x62c);
        EXPECT_EQ(effects.front()->life, 0);
        EXPECT_EQ(effects.front()->transform.position, glm::vec3(1000.0f, 1910.0f, z));
        constexpr std::array<std::pair<std::int32_t, float>, 5> Spirals{
            {{1, 100.0f}, {0, 250.0f}, {2, 100.0f}, {3, 100.0f}, {4, 100.0f}}};
        for (std::size_t index = 0; index < Spirals.size(); ++index)
        {
            SCOPED_TRACE(index);
            const JointState& joint = tested.joints[index];
            ASSERT_TRUE(joint.alive);
            EXPECT_EQ(joint.type, 0x62c);
            EXPECT_EQ(joint.subtype, Spirals[index].first);
            EXPECT_EQ(joint.width, Spirals[index].second);
            EXPECT_EQ(joint.pathIncrement, glm::vec3(1000.0f, 1910.0f, z + 150.0f));
            EXPECT_EQ(joint.ownerContext, &tested.Striker());
        }
        EXPECT_FALSE(tested.joints[5].alive);
        EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x1cd});
    }
}

// 0x4fe015..0x4fe13f: los 36 joints 0x613/2 de la 0x30, colgados de o 100 más arriba con los ángulos (−10, 0, 10 · i),
// sin rand(); 0x53265e y 0x533173: la mezcla 1, la segunda cara, velocidad 50, la luz 0,5, vida 20, tres puntos y la
// partida en +0x24. En las vueltas 0 y 20, después del joint, el calco 0x61e/4 sobre o (0x4fe127), con su escala
// rand() % 50 + 50 por 0,04 (0x51ef79); al final, el sonido 0x7f.
TEST(AttackSkillCasesTest, TheSpiritRingThrowsItsJointsAndTwoDecals)
{
    SkillWorld tested;
    tested.joints.resize(40);
    tested.Striker().position = {1000.0f, 2000.0f, 50.0f};

    const auto stepped = tested.Head(0x30);

    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 2);
    for (std::size_t index = 0; index < 36; ++index)
    {
        SCOPED_TRACE(index);
        const JointState& joint = tested.joints[index];
        ASSERT_TRUE(joint.alive);
        EXPECT_EQ(joint.type, 0x613);
        EXPECT_EQ(joint.subtype, 2);
        EXPECT_EQ(joint.position, glm::vec3(1000.0f, 2000.0f, 150.0f));
        EXPECT_EQ(joint.angles, glm::vec3(-10.0f, 0.0f, 10.0f * static_cast<float>(index)));
        EXPECT_EQ(joint.width, 60.0f);
        EXPECT_EQ(joint.blend, 1);
        EXPECT_EQ(joint.drawEnabled, 2);
        EXPECT_EQ(joint.speed, 50.0f);
        EXPECT_EQ(joint.light, glm::vec3(0.5f));
        EXPECT_EQ(joint.life, 20);
        EXPECT_EQ(joint.maximumCount, 3);
        EXPECT_EQ(joint.pathIncrement, joint.position);
        EXPECT_EQ(joint.ownerContext, &tested.Striker());
    }
    EXPECT_FALSE(tested.joints[36].alive);
    const auto decals = AliveEffects(tested.world);
    ASSERT_EQ(decals.size(), 2u);
    for (std::size_t index = 0; index < decals.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(decals[index]->type, 0x61e);
        EXPECT_EQ(decals[index]->subtype, 4);
        EXPECT_EQ(decals[index]->ownerContext, &tested.Striker());
        EXPECT_EQ(decals[index]->transform.angles, glm::vec3(-10.0f, 0.0f, index == 0 ? 0.0f : 200.0f));
    }
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x7f});
}

// 0x501bab..0x501ca4: la 0x09 da cuatro vueltas; en cada una, 0x59fb80(9) (el registro propio cerrado en 0) y dos
// joints 0x613/0, cada uno con su acceso al registro propio para el +0x9eb (0x532675): 12 cierres de 3.848. El de
// ancho 80 lleva la key +0x8a, el índice de la skill (0 sin ella) y +0x1bd; el de ancho 20, la key −1. Suena 0x75.
TEST(AttackSkillCasesTest, TheEvilSpiritsThrowEightJointsWithTheVolley)
{
    SkillWorld tested;
    tested.Striker().position = {1000.0f, 2000.0f, 50.0f};
    tested.Striker().word8a = 0x1234;
    tested.Striker().magicKey = 7;
    tested.world.OwnCharacter().bytef04 = 9;

    const auto stepped = tested.Head(0x09);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawsSince(Seed, tested.random), 12 * OwnRecord);
    for (std::size_t index = 0; index < 8; ++index)
    {
        SCOPED_TRACE(index);
        const JointState& joint = tested.joints[index];
        const bool wide = index % 2 == 0;
        ASSERT_TRUE(joint.alive);
        EXPECT_EQ(joint.type, 0x613);
        EXPECT_EQ(joint.subtype, 0);
        EXPECT_EQ(joint.position, glm::vec3(1000.0f, 2000.0f, 150.0f));
        EXPECT_EQ(joint.angles, glm::vec3(0.0f, 0.0f, 90.0f * static_cast<float>(index / 2)));
        EXPECT_EQ(joint.width, wide ? 80.0f : 20.0f);
        EXPECT_EQ(joint.key, wide ? 0x1234 : -1);
        EXPECT_EQ(joint.skill, 0);
        EXPECT_EQ(joint.skillSerial, wide ? 7 : 0);
        EXPECT_EQ(joint.volley, 9);
        EXPECT_EQ(joint.blend, 3);
        EXPECT_EQ(joint.speed, 70.0f);
        EXPECT_EQ(joint.life, 49);
        EXPECT_EQ(joint.maximumCount, 6);
    }
    EXPECT_FALSE(tested.joints[8].alive);
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x75});
}

// 0x31 (0x4febdb..0x4fec2b): el 0x6ac sobre o, con o de dueño, su clave (+0x8a) y el byte bajo de [0x5767948], sin
// 0x59fb80 antes; sus cuarenta joints 0x617/1 sacan tres rand() cada uno (el de su alta y los dos de 0x530860).
// Después, el sonido 0x72. Con la tabla de efectos llena, ni efecto ni rand(), y el sonido igual.
TEST(AttackSkillCasesTest, TheBreathCaseRaisesTheFireBreathOverTheStriker)
{
    SkillWorld tested;
    ActorState& striker = tested.Striker();
    striker.word8a = 0x1234;
    tested.world.Runtime().lastShotSkill = 0x1f31;

    ASSERT_TRUE(tested.Head(0x31).HasValue());

    const auto alive = AliveEffects(tested.world);
    ASSERT_EQ(alive.size(), 1u);
    const Client::World::ModelEffect& breath = *alive.front();
    EXPECT_EQ(breath.type, 0x6ac);
    EXPECT_EQ(breath.subtype, 0);
    EXPECT_EQ(breath.ownerContext, &striker);
    EXPECT_EQ(breath.disturbanceState, 0x1234);
    EXPECT_EQ(breath.byte89, 0x31);
    EXPECT_EQ(breath.light, striker.light);
    EXPECT_EQ(breath.life, 10);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 40 * 3);
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x72});

    SkillWorld full;
    for (Client::World::ModelEffect& effect : full.world.Map().effects.Effects())
        effect.alive = true;
    ASSERT_TRUE(full.Head(0x31).HasValue());
    EXPECT_EQ(DrawsSince(Seed, full.random), 0);
    EXPECT_EQ(full.sounds, std::vector<std::int32_t>{0x72});
}

// 0x0c (0x500c64..0x500ccf): el rayo 0x5e8 en el punto (−20, −90, 100) de o girado, con o de dueño, su clave (+0x8a)
// y el lugar de la skill (0x59fb80, con el registro propio cerrado); el sonido 0x7a. Sin ser el héroe, el alta no busca
// blancos.
TEST(AttackSkillCasesTest, TheLightningRayLeavesFromTheHand)
{
    SkillWorld tested;
    ActorState& striker = tested.Striker();
    striker.angles = {0.0f, 0.0f, 90.0f};
    striker.word8a = 0x1234;
    tested.world.Runtime().hero = VictimSlot;

    ASSERT_TRUE(tested.Head(0x0c).HasValue());

    const auto alive = AliveEffects(tested.world);
    ASSERT_EQ(alive.size(), 1u);
    const Client::World::ModelEffect& ray = *alive.front();
    EXPECT_EQ(ray.type, 0x5e8);
    EXPECT_EQ(ray.subtype, 0);
    EXPECT_EQ(ray.transform.position, Client::Scenes::PointAround(striker, {-20.0f, -90.0f, 100.0f}));
    EXPECT_EQ(ray.ownerContext, &striker);
    EXPECT_EQ(ray.disturbanceState, 0x1234);
    EXPECT_EQ(ray.life, 20);
    EXPECT_EQ(ray.transform.scale, 16.0f);
    EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord);
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x7a});
}

// Con el registro propio abierto (+0x38e de 0x40ad90 en uso), 0x59fb80 no saca rand().
TEST(AttackSkillCasesTest, AnOpenOwnRecordFindsTheSkillWithoutDraws)
{
    SkillWorld tested;
    tested.world.Runtime().ownRecordDepth = 1;
    tested.world.Runtime().hero = VictimSlot;

    const auto stepped = tested.Head(0x0c);

    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
}

// 0x4fdef2..0x4fdf26: de la 0x1e a la 0x24, el calco 0x61e/3 sobre o, con o de dueño, sin rand() ni sonido, con o sin
// 0x4000. Con la tabla llena, nada.
TEST(AttackSkillCasesTest, TheSummonsLeaveTheirDecalOverTheStriker)
{
    for (const std::int32_t map : Maps)
    {
        for (std::uint8_t skill = 0x1e; skill <= 0x24; ++skill)
        {
            for (const std::uint32_t flags : {0u, 0x4000u})
            {
                SCOPED_TRACE(testing::Message() << "mapa " << map << ", skill " << int{skill} << ", " << flags);
                SkillWorld tested(map);
                ActorState& striker = tested.Striker();
                striker.flags78 = flags;
                striker.position = glm::vec3(1250.0f, 3350.0f, 120.0f);
                striker.angles = glm::vec3(0.0f, 0.0f, 45.0f);

                ASSERT_TRUE(tested.Head(skill).HasValue());

                const auto alive = AliveEffects(tested.world);
                ASSERT_EQ(alive.size(), 1u);
                const Client::World::ModelEffect& decal = *alive.front();
                EXPECT_EQ(decal.type, Client::World::ColoredDecalEffect);
                EXPECT_EQ(decal.subtype, Client::World::SummonDecalSubtype);
                EXPECT_EQ(decal.ownerContext, &striker);
                EXPECT_EQ(decal.transform.position, striker.position);
                EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
                EXPECT_TRUE(tested.sounds.empty());
                EXPECT_EQ(striker.flags78, flags);
            }
        }
    }
    SkillWorld full;
    for (Client::World::ModelEffect& effect : full.world.Map().effects.Effects())
    {
        effect.alive = true;
        effect.type = 0x01;
    }
    ASSERT_TRUE(full.Head(0x1e).HasValue());
    EXPECT_EQ(DrawsSince(Seed, full.random), 0);
}

// Un mundo plano para la Flame: las alturas en 150 y los atributos en 0.
void Flatten(WorldState& world)
{
    auto& terrain = world.Map().terrain;
    terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    terrain.heights->cells.fill(150.0f);
}

// 0x500496..0x500c0b, la Flame: el chorro 0x5de/0 en el centro de la celda de la skill (+0x39c/+0x39d) a la altura del
// terreno, con o de dueño, +0x8a de o, el lugar de la skill en +0x89 y la cuenta +0xf04 en +0x8c (0x518cf0 la lee con
// el registro propio abierto: 3.848 rand() al cerrarse). Solo con o el héroe publicado, +0xf04 uno más (otro registro).
// Antes, 0x59fb80(5); después, el sonido 0x79.
TEST(AttackSkillCasesTest, TheFlameLightsItsJetOnTheSkillCell)
{
    for (const std::int32_t map : Maps)
    {
        for (const bool hero : {false, true})
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << (hero ? ", el héroe" : ", otro"));
            SkillWorld tested(map);
            Flatten(tested.world);
            if (hero)
                tested.world.Runtime().hero = StrikerSlot;
            ActorState& striker = tested.Striker();
            striker.skillCellX = 120;
            striker.skillCellY = 33;
            striker.word8a = 0x0123;
            striker.angles = glm::vec3(0.0f, 0.0f, 90.0f);
            striker.light = glm::vec3(0.5f, 0.25f, 1.0f);
            tested.world.OwnCharacter().SetSkillAt(3, 0x05);
            tested.world.OwnCharacter().bytef04 = 0x41;

            ASSERT_TRUE(tested.Head(0x05).HasValue());

            EXPECT_EQ(DrawsSince(Seed, tested.random), (hero ? 3 : 2) * OwnRecord);
            const auto alive = AliveEffects(tested.world);
            ASSERT_EQ(alive.size(), 1u);
            const Client::World::ModelEffect& jet = *alive.front();
            EXPECT_EQ(jet.type, Client::World::FlameJetEffect);
            EXPECT_EQ(jet.subtype, 0);
            EXPECT_EQ(jet.life, 40);
            EXPECT_EQ(jet.transform.position, glm::vec3(12'050.0f, 3'350.0f, 150.0f));
            EXPECT_EQ(jet.transform.angles, striker.angles);
            EXPECT_EQ(jet.ownerContext, &striker);
            EXPECT_EQ(jet.disturbanceState, 0x0123);
            EXPECT_EQ(jet.byte89, 3);
            EXPECT_EQ(jet.word8c, 0x41);
            EXPECT_EQ(tested.world.OwnCharacter().bytef04, hero ? 0x42 : 0x41);
            EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
            EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x79}));
        }
    }
}

// Con la tabla llena, sin chorro ni el registro de su alta; el del héroe y el sonido siguen. Sin las alturas del
// terreno, MuRet corta antes de 0x59fb80.
TEST(AttackSkillCasesTest, TheFlameWithoutRoomOrGroundSkipsItsJet)
{
    SkillWorld full;
    Flatten(full.world);
    full.world.Runtime().hero = StrikerSlot;
    for (Client::World::ModelEffect& effect : full.world.Map().effects.Effects())
    {
        effect.alive = true;
        effect.type = 0x01;
    }
    ASSERT_TRUE(full.Head(0x05).HasValue());
    EXPECT_EQ(DrawsSince(Seed, full.random), 2 * OwnRecord);
    EXPECT_EQ(full.world.OwnCharacter().bytef04, 1);
    EXPECT_EQ(full.sounds, (std::vector<std::int32_t>{0x79}));

    SkillWorld bare;
    const auto stepped = bare.Head(0x05);
    EXPECT_TRUE(HasError(stepped, "0x5004ea")) << ErrorOf(stepped);
    EXPECT_EQ(DrawsSince(Seed, bare.random), 0);
    EXPECT_TRUE(bare.sounds.empty());
}

// El 0 y los que no están en las tablas de 0x505530 siguen en 0x502aff sin nada; la 0x37 y la 0x38 solo dejan el arma
// de la derecha − 0x212 en +0x8c y su nivel en +0x8e.
TEST(AttackSkillCasesTest, TheQuietHeadCasesLeaveAtMostTheWeapon)
{
    for (const std::uint8_t skill : std::array<std::uint8_t, 7>{0x00, 0x03, 0x0f, 0x25, 0x2b, 0x4d, 0xff})
    {
        SCOPED_TRACE(int{skill});
        SkillWorld tested;
        tested.Striker().hands[0].model = 0x212 + 7;

        EXPECT_TRUE(tested.Head(skill).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        EXPECT_EQ(tested.Striker().word8c, 0);
        EXPECT_TRUE(AliveEffects(tested.world).empty());
    }
    for (const std::uint8_t skill : std::array<std::uint8_t, 2>{0x37, 0x38})
    {
        SCOPED_TRACE(int{skill});
        SkillWorld tested(2);
        tested.Striker().hands[0].model = 0x212 + 0x20;
        tested.Striker().hands[0].level = 9;

        EXPECT_TRUE(tested.Head(skill).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        EXPECT_EQ(tested.Striker().word8c, 0x20);
        EXPECT_EQ(tested.Striker().byte8e, 9);
        EXPECT_TRUE(tested.sounds.empty());
    }
    // Sin nada en la derecha (0xffff), +0x8c queda en −1 − 0x212.
    SkillWorld unarmed;
    EXPECT_TRUE(unarmed.Head(0x37).HasValue());
    EXPECT_EQ(unarmed.Striker().word8c, -1 - 0x212);
}

// 0x2a: el arma en +0x8c/+0x8e, 0x59fb80(0x2a) y el terremoto 0xfa con el estado 0, el tamaño de +0x8a de o y el
// lugar de la skill en +0x89, cuyo dibujo lee el arma por su dueño; después el sonido 0x95. Los rand(): 0x59fb80, el
// subtipo rand() % 100 y el cierre del registro propio de la cuenta +0xf04.
TEST(AttackSkillCasesTest, TheQuakeKeepsTheWeaponForItsOwner)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        SkillWorld tested(map);
        ActorState& striker = tested.Striker();
        striker.hands[0].model = 0x212 + 0x0c;
        striker.hands[0].level = 4;
        striker.word8a = 0x0123;
        striker.position = glm::vec3(1250.0f, 3350.0f, 120.0f);
        tested.world.OwnCharacter().SetSkillAt(6, 0x2a);
        tested.world.OwnCharacter().bytef04 = 0x41;

        ASSERT_TRUE(tested.Head(0x2a).HasValue());

        CrtRandom expected(Seed);
        expected.Discard(static_cast<std::uint32_t>(OwnRecord));
        const int subtype = expected.Next() % 100;
        expected.Discard(static_cast<std::uint32_t>(OwnRecord));
        EXPECT_EQ(tested.random.State(), expected.State());
        const auto alive = AliveEffects(tested.world);
        ASSERT_EQ(alive.size(), 1u);
        const Client::World::ModelEffect& quake = *alive.front();
        EXPECT_EQ(quake.type, 0xfa);
        EXPECT_EQ(quake.subtype, subtype);
        EXPECT_EQ(quake.state, 0);
        EXPECT_EQ(quake.disturbanceState, 0x0123);
        EXPECT_EQ(quake.byte89, 6);
        EXPECT_EQ(quake.byte1bd, 0);
        EXPECT_EQ(quake.ownerContext, &striker);
        ASSERT_NE(quake.itemType, nullptr);
        ASSERT_NE(quake.itemLevel, nullptr);
        EXPECT_EQ(*quake.itemType, 0x0c);
        EXPECT_EQ(*quake.itemLevel, 4);
        EXPECT_EQ(quake.transform.position, striker.position);
        EXPECT_EQ(tested.world.OwnCharacter().bytef04, 0x42);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x95}));
    }
}

// Con la tabla de efectos llena, el terremoto no se crea (sin su rand() ni el registro) y el sonido suena igual.
TEST(AttackSkillCasesTest, AFullTableSkipsTheQuakeButNotItsSound)
{
    SkillWorld tested;
    for (Client::World::ModelEffect& effect : tested.world.Map().effects.Effects())
    {
        effect.alive = true;
        effect.type = 0x01;
    }

    ASSERT_TRUE(tested.Head(0x2a).HasValue());

    EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x95}));
}

// 0x44 y 0x47: el blanco de +0x3a4 sin rango; sin blanco o con o mismo, nada; con otro, el joint 0x632 (CL11); fuera
// del pool, MuRet corta en la lectura.
TEST(AttackSkillCasesTest, TheBindingSkillsLookAtTheirTarget)
{
    struct Case
    {
        std::uint8_t skill;
        std::string_view read;
        std::string_view joint;
    };
    for (const Case& probe : {Case{0x44, "0x4fe1a6", "0x4fe235"}, Case{0x47, "0x4fe458", "0x4fe4e7"}})
    {
        SCOPED_TRACE(int{probe.skill});
        {
            SkillWorld tested;
            EXPECT_TRUE(tested.Head(probe.skill).HasValue());
            tested.Striker().attackTarget = static_cast<std::uint16_t>(StrikerSlot);
            EXPECT_TRUE(tested.Head(probe.skill).HasValue());
            tested.Striker().attackTarget = static_cast<std::uint16_t>(VictimSlot);
            EXPECT_TRUE(HasError(tested.Head(probe.skill), probe.joint));
            tested.Striker().attackTarget = std::uint16_t{0x8000};
            EXPECT_TRUE(HasError(tested.Head(probe.skill), probe.read));
            tested.Striker().attackTarget = std::uint16_t{500};
            EXPECT_TRUE(HasError(tested.Head(probe.skill), probe.read));
            EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        }
    }
}

// 0x46: con blanco, 0x534f90(0x10f, blanco, 4) y (…, 9) apagan los joints de él con esos subtipos, el calco 0x61e/6
// nace sobre él (vida 60, escala (rand() % 50 + 50) · 0,04f, la partida en su posición y él de dueño) y suena el 0x269;
// sin blanco, nada; sin el pool de joints, corta antes.
TEST(AttackSkillCasesTest, TheReleaseKillsTheFlamesOfTheTarget)
{
    SkillWorld tested;
    EXPECT_TRUE(tested.Head(0x46).HasValue());

    tested.Striker().attackTarget = static_cast<std::uint16_t>(VictimSlot);
    const std::array<std::int32_t, 4> subtypes{4, 9, 0, 4};
    for (std::size_t index = 0; index < subtypes.size(); ++index)
    {
        tested.joints[index].alive = true;
        tested.joints[index].type = 0x10f;
        tested.joints[index].subtype = subtypes[index];
        tested.joints[index].ownerContext = index == 3 ? &tested.Striker() : &tested.Victim();
    }

    tested.Victim().position = glm::vec3(1500.0f, 1600.0f, 20.0f);
    tested.Victim().light = glm::vec3(0.5f);
    ASSERT_TRUE(tested.Head(0x46).HasValue());

    const auto alive = AliveEffects(tested.world);
    ASSERT_EQ(alive.size(), 1u);
    const Client::World::ModelEffect& decal = *alive.front();
    CrtRandom expected{Seed};
    const int grown = expected.Next() % 50 + 50;
    EXPECT_EQ(decal.type, 0x61e);
    EXPECT_EQ(decal.subtype, 6);
    EXPECT_EQ(decal.life, 60);
    EXPECT_EQ(decal.transform.scale,
              static_cast<float>(static_cast<double>(grown) * static_cast<double>(std::bit_cast<float>(0x3d23d70au))));
    EXPECT_EQ(decal.start, tested.Victim().position);
    EXPECT_EQ(decal.light, tested.Victim().light);
    EXPECT_EQ(decal.ownerContext, &tested.Victim());
    EXPECT_EQ(DrawsSince(Seed, tested.random), 1);
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x269});

    EXPECT_FALSE(tested.joints[0].alive);
    EXPECT_FALSE(tested.joints[1].alive);
    EXPECT_TRUE(tested.joints[2].alive);
    EXPECT_TRUE(tested.joints[3].alive);

    tested.withJoints = false;
    EXPECT_TRUE(HasError(tested.Head(0x46), "0x4fe3ce"));
}

// 0x50294e..0x50298e, la 0x2c: el 0x13b/0 sobre o con o de dueño y la luz del paso (vida 15, la z 100 más) y el sonido
// 0x26c, sin rand(); con la tabla llena, sólo el sonido.
TEST(AttackSkillCasesTest, TheSwordForceRisesOverTheStriker)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        SkillWorld tested;
        tested.Striker().position = glm::vec3(100.0f, 200.0f, 300.0f);
        if (full)
        {
            for (auto& effect : tested.world.Map().effects.Effects())
                effect.alive = true;
        }
        ASSERT_TRUE(tested.Head(0x2c).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x26c}));
        if (full)
            continue;
        const auto alive = AliveEffects(tested.world);
        ASSERT_EQ(alive.size(), 1u);
        EXPECT_EQ(alive[0]->type, 0x13b);
        EXPECT_EQ(alive[0]->life, 15);
        EXPECT_EQ(alive[0]->ownerContext, &tested.Striker());
        EXPECT_EQ(alive[0]->transform.position, glm::vec3(100.0f, 200.0f, 400.0f));
        EXPECT_EQ(alive[0]->light, glm::vec3(1.0f));
    }
}

// 0x40 y 0x4b: con otra cosa que 0xa21 en la derecha, el 0x117/0 en el hueso de la derecha; con otra cosa que 0xa19 o
// un escudo (0xe12..0x1011) en la izquierda, el 0x117/1 en el suyo (un rand() cada uno, en su alta), con los ángulos de
// o y la luz (1; 0,6; 0,3) de la 0x40 o (1; 1; 1) de la 0x4b. Después la 0x40 suena (0x1cf, desde o) y la 0x4b crea el
// 0x138/0 sobre o y suena (0x272). Con la tabla llena, sólo los sonidos y sin rand().
TEST(AttackSkillCasesTest, TheHandsDecideTheRaise)
{
    struct Case
    {
        std::uint16_t right;
        std::uint16_t left;
        std::vector<std::int32_t> hands;
    };
    const std::array cases{
        Case{0xffff, 0xa19, {0}}, Case{0xa21, 0xffff, {1}}, Case{0xa21, 0xa20, {1}}, Case{0xa21, 0x1012, {1}},
        Case{0xa21, 0xa19, {}},   Case{0xa21, 0xe12, {}},   Case{0xa21, 0x1011, {}}, Case{0xffff, 0xffff, {0, 1}},
    };
    const glm::vec3 warm{1.0f, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3e99999au)};
    for (const Case& probe : cases)
    {
        for (const std::uint8_t skill : {std::uint8_t{0x40}, std::uint8_t{0x4b}})
        {
            SCOPED_TRACE(testing::Message() << probe.right << " y " << probe.left << ", skill " << int{skill});
            SkillWorld tested(2);
            ActorState& striker = tested.Striker();
            striker.position = glm::vec3(125.0f, 250.0f, 375.0f);
            striker.angles = glm::vec3(0.0f, 0.0f, 60.0f);
            striker.light = glm::vec3(0.25f);
            striker.hands[0].model = probe.right;
            striker.hands[1].model = probe.left;

            const auto stepped = tested.Head(skill);

            ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
            const auto alive = AliveEffects(tested.world);
            const std::size_t runes = skill == 0x4b ? 1 : 0;
            ASSERT_EQ(alive.size(), probe.hands.size() + runes);
            for (std::size_t made = 0; made < probe.hands.size(); ++made)
            {
                EXPECT_EQ(alive[made]->type, 0x117);
                EXPECT_EQ(alive[made]->subtype, probe.hands[made]);
                EXPECT_EQ(alive[made]->light, skill == 0x40 ? warm : glm::vec3(1.0f));
                EXPECT_EQ(alive[made]->life, 10);
                EXPECT_EQ(alive[made]->transform.angles.x, 45.0f);
            }
            if (runes != 0)
            {
                const auto* rune = alive.back();
                EXPECT_EQ(rune->type, 0x138);
                EXPECT_EQ(rune->subtype, 0);
                EXPECT_EQ(rune->life, 50);
                EXPECT_EQ(rune->light, glm::vec3(0.25f));
                EXPECT_EQ(rune->transform.position, glm::vec3(125.0f, 250.0f, 675.0f));
            }
            EXPECT_EQ(DrawsSince(Seed, tested.random), static_cast<int>(probe.hands.size()));
            EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{skill == 0x40 ? 0x1cf : 0x272}));
            EXPECT_EQ(tested.soundOrigins.size(), skill == 0x40 ? 1u : 0u);
        }
    }
    SkillWorld full(2);
    for (auto& effect : full.world.Map().effects.Effects())
        effect.alive = true;
    full.Striker().hands[0].model = 0xffff;
    ASSERT_TRUE(full.Head(0x4b).HasValue());
    EXPECT_EQ(DrawsSince(Seed, full.random), 0);
    EXPECT_EQ(full.sounds, (std::vector<std::int32_t>{0x272}));
}

// 0x4c: el sonido 0x27d y el lazo de los 400: el monstruo lejos lee el alcance y no entra, el de la acción 6 no lo lee,
// y de los doce cerca entran diez (el último, justo en el alcance) y el lazo se corta ahí. Después rand() % 360; sin
// blanco en +0x434, nada más; con él, dos rand() y el joint 0x14f corta.
TEST(AttackSkillCasesTest, TheStrikeAroundGathersTenTargetsInRange)
{
    const auto skills = SkillsWithRange(3);
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        for (const std::int32_t target : {-1, 400, 2})
        {
            SCOPED_TRACE(target);
            SkillWorld tested(map);
            tested.skills = &skills;
            tested.Striker().position = glm::vec3(5000.0f, 5000.0f, 0.0f);
            tested.Striker().skillTarget4c = target;
            tested.Monster(0, 301.0f);
            tested.Monster(1, 10.0f).animation.action = 6;
            for (std::size_t slot = 10; slot < 22; ++slot)
                tested.Monster(slot, slot == 19 ? 300.0f : -50.0f);

            const auto stepped = tested.Head(0x4c);

            const int reads = 1 + 10;
            if (target == 2)
            {
                EXPECT_TRUE(HasError(stepped, "0x5026a4")) << ErrorOf(stepped);
                EXPECT_EQ(DrawsSince(Seed, tested.random), reads * SkillEntry + 3);
            }
            else
            {
                EXPECT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
                EXPECT_EQ(DrawsSince(Seed, tested.random), reads * SkillEntry + 1);
            }
            EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x27d}));
        }
    }
}

// Los jugadores del lazo de la 0x4c pasan por 0x5588d0: el PK de 6 entra, el de 3 solo con Ctrl, el de la acción 0xc3
// no se mira; en Chaos Castle (18) entra cualquiera menos el héroe. Con +0x88 en otra cosa, ninguno.
TEST(AttackSkillCasesTest, TheStrikeAroundAsksWhoIsAttackable)
{
    const auto skills = SkillsWithRange(5);
    struct Case
    {
        std::string_view name;
        std::int32_t map;
        std::uint8_t pk;
        bool control;
        std::uint8_t action;
        std::uint8_t object;
        int reads;
    };
    const std::array cases{
        Case{"PK 6", 0, 6, false, 1, 1, 1},          Case{"PK 3", 2, 3, false, 1, 1, 0},
        Case{"PK 3 con Ctrl", 2, 3, true, 1, 1, 1},  Case{"caído", 0, 6, false, 0xc3, 1, 0},
        Case{"Chaos Castle", 18, 3, false, 1, 1, 1}, Case{"un NPC", 0, 6, false, 1, 4, 0},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        SkillWorld tested(probe.map);
        tested.skills = &skills;
        tested.controlDown = probe.control;
        tested.Victim().pk = probe.pk;
        tested.Victim().animation.action = probe.action;
        tested.Victim().byte88 = probe.object;
        // El que pega, con el PK de 3, entra solo con Ctrl o en Chaos Castle; el héroe, nunca (es él).
        const int others = probe.control || probe.map == 18 ? 1 : 0;

        ASSERT_TRUE(tested.Head(0x4c).HasValue());

        EXPECT_EQ(DrawsSince(Seed, tested.random), (probe.reads + others) * SkillEntry + 1);
    }
}

// 0x4c sin Skill.bmd: corta en el primer blanco que mira; sin ninguno, no lo lee. Con +0x434 negativo y no −1, el
// pool se lee fuera: MuRet corta. Con la entrada abierta, el alcance no saca rand().
TEST(AttackSkillCasesTest, TheStrikeAroundCutsWhereItReads)
{
    {
        SkillWorld tested;
        tested.Monster(0, 10.0f);
        EXPECT_TRUE(HasError(tested.Head(0x4c), "0x5526f0"));
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    }
    {
        SkillWorld tested;
        EXPECT_TRUE(tested.Head(0x4c).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), 1);
    }
    {
        const auto skills = SkillsWithRange(3);
        SkillWorld tested;
        tested.skills = &skills;
        tested.world.Runtime().movement.skillDepths[0x4c] = 1;
        tested.Monster(0, 10.0f);
        tested.Striker().skillTarget4c = -5;
        EXPECT_TRUE(HasError(tested.Head(0x4c), "0x5025b8"));
        EXPECT_EQ(DrawsSince(Seed, tested.random), 1);
    }
}

// Los casos con blanco que crean lo que MuRet no tiene (CL11), sin rand() antes; el 0x20a de la 0x01 crea antes su
// 0xc3 (sin rand()).
TEST(AttackSkillCasesTest, TheTargetCasesCutWithoutDraws)
{
    struct Case
    {
        std::uint8_t skill;
        std::uint16_t type;
        std::string_view cut;
    };
    const std::array cases{
        Case{0x01, 0x20a, "0x50497e"},
        Case{0x01, 0x0e, "0x50497e"},
        Case{0x3c, 0x20a, "0x504d05"},
        Case{0x11, 0x20a, "0x505053"},
    };
    for (const std::int32_t map : Maps)
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", skill " << int{probe.skill});
            SkillWorld tested(map);
            tested.Striker().type = probe.type;

            const auto stepped = tested.WithTarget(probe.skill);

            EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
            EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        }
    }
}

// 0x504a0a..0x504abf, la 0x2d: tres 0x139 sobre o, con los subtipos 0, 1 y 2, el blanco de dueño (la partida en su
// posición), la clave en −1 y un rand() cada uno (la z de +0x28); después el sonido 0x26d.
TEST(AttackSkillCasesTest, TheJavelinCaseThrowsThreeJavelinsAtTheTarget)
{
    SkillWorld tested;
    tested.Striker().position = glm::vec3(100.0f, 200.0f, 300.0f);
    tested.Victim().position = glm::vec3(400.0f, 500.0f, 600.0f);
    ASSERT_TRUE(tested.WithTarget(0x2d).HasValue());
    const auto alive = AliveEffects(tested.world);
    ASSERT_EQ(alive.size(), 3u);
    for (std::size_t index = 0; index < alive.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(alive[index]->type, 0x139);
        EXPECT_EQ(alive[index]->subtype, static_cast<std::int32_t>(index));
        EXPECT_EQ(alive[index]->ownerContext, &tested.Victim());
        EXPECT_EQ(alive[index]->start, tested.Victim().position);
        EXPECT_EQ(alive[index]->transform.position, glm::vec3(100.0f, 200.0f, 450.0f));
        EXPECT_EQ(alive[index]->disturbanceState, 0xffff);
        EXPECT_EQ(alive[index]->life, 35);
    }
    EXPECT_EQ(DrawsSince(Seed, tested.random), 3);
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x26d});
}

// 0x504b51..0x504b99, la 0x4a: el 0x116/2 sobre o con el blanco de dueño (vida 20, la z 20 menos) y el sonido 0x271,
// sin rand(); con la tabla llena, sólo el sonido.
TEST(AttackSkillCasesTest, TheDivingPartFollowsTheTarget)
{
    SkillWorld tested;
    tested.Striker().position = glm::vec3(100.0f, 200.0f, 300.0f);
    tested.Striker().angles = glm::vec3(0.0f, 0.0f, 30.0f);
    ASSERT_TRUE(tested.WithTarget(0x4a).HasValue());
    const auto alive = AliveEffects(tested.world);
    ASSERT_EQ(alive.size(), 1u);
    EXPECT_EQ(alive[0]->type, 0x116);
    EXPECT_EQ(alive[0]->subtype, 2);
    EXPECT_EQ(alive[0]->ownerContext, &tested.Victim());
    EXPECT_EQ(alive[0]->transform.position, glm::vec3(100.0f, 200.0f, 280.0f));
    EXPECT_EQ(alive[0]->transform.angles, glm::vec3(0.0f, 0.0f, 30.0f));
    EXPECT_EQ(alive[0]->life, 20);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x271}));

    SkillWorld full;
    for (auto& effect : full.world.Map().effects.Effects())
        effect.alive = true;
    ASSERT_TRUE(full.WithTarget(0x4a).HasValue());
    EXPECT_EQ(full.sounds, (std::vector<std::int32_t>{0x271}));
}

// 0x504d87..0x504f51, la 0x3d: tres 0x116/0 desde el primer hueso de o con el blanco de dueño, los rumbos z + 90, z y
// z − 90 y la partida (0, 0, techo) y (0, 0, techo / 2); después el 0x117/0 y el /1 (un rand() cada uno). Sin sonido.
TEST(AttackSkillCasesTest, TheLaunchedPartsAimAtTheTargetsTop)
{
    SkillWorld tested;
    tested.Striker().angles = glm::vec3(0.0f, 0.0f, 30.0f);
    tested.Victim().boundsTop = 150.0f;
    ASSERT_TRUE(tested.WithTarget(0x3d).HasValue());
    const auto alive = AliveEffects(tested.world);
    ASSERT_EQ(alive.size(), 5u);
    const std::array<float, 3> yaws{120.0f, 30.0f, -60.0f};
    const std::array<float, 3> aims{150.0f, 75.0f, 75.0f};
    for (std::size_t part = 0; part < 3; ++part)
    {
        SCOPED_TRACE(part);
        EXPECT_EQ(alive[part]->type, 0x116);
        EXPECT_EQ(alive[part]->subtype, 0);
        EXPECT_EQ(alive[part]->ownerContext, &tested.Victim());
        EXPECT_EQ(alive[part]->transform.angles, glm::vec3(0.0f, 0.0f, yaws[part]));
        EXPECT_EQ(alive[part]->start, glm::vec3(0.0f, 0.0f, aims[part]));
        EXPECT_EQ(alive[part]->light, glm::vec3(1.0f));
        EXPECT_EQ(alive[part]->transform.position, alive[0]->transform.position);
    }
    EXPECT_EQ(alive[3]->type, 0x117);
    EXPECT_EQ(alive[3]->subtype, 0);
    EXPECT_EQ(alive[4]->type, 0x117);
    EXPECT_EQ(alive[4]->subtype, 1);
    EXPECT_EQ(alive[4]->transform.position, alive[0]->transform.position);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 2);
    EXPECT_TRUE(tested.sounds.empty());
}

// 0x504ac7..0x504b49, la 0x49: un 0x61c/4 sin dueño desde o 130 más arriba, con los ángulos (0, 0, z de o) y ancho 40
// (InfernoJoints.cpp: Effect\Hole.jpg, velocidad 8, vx 15,5, vz 3, 13 puntos, vida 20), sin rand(), y el sonido 0x26f.
TEST(AttackSkillCasesTest, TheInfernoRisesFromTheStriker)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(testing::Message() << "mapa " << map);
        SkillWorld tested(map);
        tested.Striker().position = {1500.0f, 1600.0f, 40.0f};
        tested.Striker().angles = {10.0f, 20.0f, 135.0f};

        ASSERT_TRUE(tested.WithTarget(0x49).HasValue());

        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x26f});
        const JointState& inferno = tested.joints[0];
        ASSERT_TRUE(inferno.alive);
        EXPECT_EQ(inferno.type, 0x61c);
        EXPECT_EQ(inferno.subtype, 4);
        EXPECT_EQ(inferno.ownerContext, nullptr);
        EXPECT_EQ(inferno.position, glm::vec3(1500.0f, 1600.0f, 170.0f));
        EXPECT_EQ(inferno.pathIncrement, inferno.position);
        EXPECT_EQ(inferno.angles, glm::vec3(0.0f, 0.0f, 135.0f));
        EXPECT_EQ(inferno.bendAngles, glm::vec3(0.0f, 0.0f, 135.0f));
        EXPECT_EQ(inferno.width, 40.0f);
        EXPECT_EQ(inferno.texture, 0x6ae);
        EXPECT_EQ(inferno.speed, 8.0f);
        EXPECT_EQ(inferno.velocity, glm::vec3(15.5f, 0.0f, 3.0f));
        EXPECT_EQ(inferno.life, 20);
        EXPECT_EQ(inferno.maximumCount, 13);
        EXPECT_EQ(inferno.count, -1);
        EXPECT_EQ(inferno.field9ec, 10);
        EXPECT_FALSE(tested.joints[1].alive);
    }
}

// 0x5046bf..0x50473b, la 0x1c: sin 0x4000 en o, el calco 0x61e/3 sobre el blanco, con él de dueño, y con +0x38d de o
// el 4 en +0x78 del blanco; sin rand() ni sonido. Con la tabla llena, sin el calco y el 4 igual.
TEST(AttackSkillCasesTest, TheSummonTargetLeavesItsDecalAndItsMark)
{
    for (const std::int32_t map : Maps)
    {
        for (const std::uint8_t succeeded : {std::uint8_t{0}, std::uint8_t{1}})
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", +0x38d " << int{succeeded});
            SkillWorld tested(map);
            tested.Striker().skillSucceeded = succeeded;
            ActorState& victim = tested.Victim();
            victim.position = glm::vec3(2250.0f, 1350.0f, 80.0f);
            victim.flags78 = 0x10;

            ASSERT_TRUE(tested.WithTarget(0x1c).HasValue());

            const auto alive = AliveEffects(tested.world);
            ASSERT_EQ(alive.size(), 1u);
            const Client::World::ModelEffect& decal = *alive.front();
            EXPECT_EQ(decal.type, Client::World::ColoredDecalEffect);
            EXPECT_EQ(decal.subtype, Client::World::SummonDecalSubtype);
            EXPECT_EQ(decal.ownerContext, &victim);
            EXPECT_EQ(decal.transform.position, victim.position);
            EXPECT_EQ(victim.flags78, succeeded != 0 ? 0x14u : 0x10u);
            EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
            EXPECT_TRUE(tested.sounds.empty());
        }
    }
    SkillWorld full;
    full.Striker().skillSucceeded = 1;
    for (Client::World::ModelEffect& effect : full.world.Map().effects.Effects())
    {
        effect.alive = true;
        effect.type = 0x01;
    }
    ASSERT_TRUE(full.WithTarget(0x1c).HasValue());
    EXPECT_EQ(full.Victim().flags78, 4u);
}

// 0x1b: sin +0x38d o con 0x4000 en +0x78, nada; si no, el calco 0x61e/2. 0x1c: con 0x4000, nada. 0x11: los +0x376 de
// las tablas de 0x50561a y 0x505672, nada. El 3 y el 0, nada.
TEST(AttackSkillCasesTest, TheQuietTargetCasesDoNothing)
{
    SkillWorld tested;
    ActorState& striker = tested.Striker();
    EXPECT_TRUE(tested.WithTarget(0x1b).HasValue());
    striker.skillSucceeded = 1;
    // El calco 0x61e/2 nace sobre el blanco, sin rand(); después, los cinco joints 0x10f/4 de 0x5045f9 (en
    // BindingFlagsPrecedeTheJointAndExistingBindingsSkipIt).
    EXPECT_TRUE(tested.WithTarget(0x1b).HasValue());
    const auto bound = AliveEffects(tested.world);
    ASSERT_EQ(bound.size(), 1u);
    EXPECT_EQ(bound.front()->type, 0x61e);
    EXPECT_EQ(bound.front()->subtype, 2);
    EXPECT_EQ(bound.front()->life, 20);
    EXPECT_EQ(bound.front()->ownerContext, &tested.Victim());
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    striker.flags78 = 0x4000;
    EXPECT_TRUE(tested.WithTarget(0x1b).HasValue());
    EXPECT_TRUE(tested.WithTarget(0x1c).HasValue());
    striker.flags78 = 0;
    const std::array<std::uint16_t, 31> quietKinds{0x25, 0x2e, 0x3d, 0x42, 0x45, 0x46, 0x49, 0x4b,  0x4d, 0x57, 0x59,
                                                   0x5d, 0x5f, 0x63, 0x70, 0x74, 0x76, 0x7a, 0x7c,  0x80, 0x82, 0x8d,
                                                   0x8f, 0xa3, 0xa5, 0xa7, 0xa9, 0xab, 0xad, 0x125, 0x12f};
    for (const std::uint16_t kind : quietKinds)
    {
        SCOPED_TRACE(kind);
        striker.word376 = kind;
        EXPECT_TRUE(tested.WithTarget(0x11).HasValue());
    }
    striker.word376 = 0x26;
    EXPECT_TRUE(HasError(tested.WithTarget(0x11), "0x505053"));
    EXPECT_TRUE(tested.WithTarget(0x03).HasValue());
    EXPECT_TRUE(tested.WithTarget(0x00).HasValue());
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    EXPECT_TRUE(tested.sounds.empty());
    EXPECT_EQ(AliveEffects(tested.world).size(), 1u); // el 0x61e/2 de la 0x1b
}

// 0x10: el 0x189 prende 0x100 en su +0x78; con 0x4000, nada; si no, 0x100 en +0x78 del blanco, el sonido 0x84,
// 0x534f90(0x10f, blanco, 0) y el primero de los cinco joints corta. Sin el pool de joints, corta después del sonido.
TEST(AttackSkillCasesTest, TheGuardMarksTheTarget)
{
    {
        SkillWorld tested;
        tested.Striker().type = 0x189;
        EXPECT_TRUE(tested.WithTarget(0x10).HasValue());
        EXPECT_EQ(tested.Striker().flags78, 0x100u);
        EXPECT_EQ(tested.Victim().flags78, 0u);
    }
    {
        SkillWorld tested;
        tested.Striker().flags78 = 0x4000;
        EXPECT_TRUE(tested.WithTarget(0x10).HasValue());
        EXPECT_EQ(tested.Victim().flags78, 0u);
        EXPECT_TRUE(tested.sounds.empty());
    }
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        SkillWorld tested(map);
        for (std::int32_t subtype = 0; subtype < 2; ++subtype)
        {
            JointState& joint = tested.joints[static_cast<std::size_t>(subtype)];
            joint.alive = true;
            joint.type = 0x10f;
            joint.subtype = subtype;
            joint.ownerContext = &tested.Victim();
        }

        // 0x5048db: apaga el 10f/0 del blanco y crea cinco, de ancho 20, el primero en su lugar.
        ASSERT_TRUE(tested.WithTarget(0x10).HasValue());

        EXPECT_EQ(tested.Victim().flags78, 0x100u);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x84}));
        EXPECT_EQ(tested.joints[1].subtype, 1);
        EXPECT_EQ(std::ranges::count_if(tested.joints,
                                        [&tested](const JointState& joint)
                                        {
                                            return joint.alive && joint.type == 0x10f && joint.subtype == 0 &&
                                                   joint.ownerContext == &tested.Victim() && joint.width == 20.0f;
                                        }),
                  5);
        EXPECT_EQ(tested.joints[0].subtype, 0);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    }
    SkillWorld unjointed;
    unjointed.withJoints = false;
    EXPECT_TRUE(HasError(unjointed.WithTarget(0x10), "0x504876"));
    EXPECT_EQ(unjointed.sounds, (std::vector<std::int32_t>{0x84}));
}

// 0x1a: el calco 0x61e/1 sobre el blanco, con él de dueño, sin rand(); con la tabla llena, nada.
TEST(AttackSkillCasesTest, TheSummonGoesOverTheTarget)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        SkillWorld tested(map);
        tested.Victim().position = glm::vec3(700.0f, 900.0f, 40.0f);

        ASSERT_TRUE(tested.WithTarget(0x1a).HasValue());

        const auto alive = AliveEffects(tested.world);
        ASSERT_EQ(alive.size(), 1u);
        EXPECT_EQ(alive.front()->type, 0x61e);
        EXPECT_EQ(alive.front()->subtype, 1);
        EXPECT_EQ(alive.front()->life, 20);
        EXPECT_EQ(alive.front()->ownerContext, &tested.Victim());
        EXPECT_EQ(alive.front()->transform.position, tested.Victim().position);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    }
}

// 0x33 y 0x34: dos lecturas de +0x38e (la skill de 0x53e2b0 y la de 0x59fb80) y un tiro; 0x18: antes, la tanda sin
// blanco con su propia lectura y 0x59fb80. Los rand(), las flechas y los sonidos son los de las lecturas y los de
// 0x53e2b0, que corre igual en un mundo gemelo. La flecha de la 0x34 deja la estela 0x105 con sus cuatro joints 0x610
// (0x51a08a), sin corte.
TEST(AttackSkillCasesTest, TheArrowCasesReadTheSkillBeforeShooting)
{
    struct Case
    {
        std::uint8_t skill;
        bool volley;
        std::size_t arrows;
        std::string_view cut;
    };
    for (const Case& probe : {Case{0x33, false, 1, ""}, Case{0x34, false, 1, ""}, Case{0x18, true, 4, ""}})
    {
        SCOPED_TRACE(int{probe.skill});
        SkillWorld tested;
        SkillWorld twin;
        for (SkillWorld* each : {&tested, &twin})
        {
            each->Striker().hands[1].model = 0xa12;
            each->Striker().byte38e = probe.skill;
            each->world.OwnCharacter().SetSkillAt(2, probe.skill);
        }

        const auto stepped = tested.WithTarget(probe.skill);

        if (probe.volley)
        {
            twin.random.Discard(static_cast<std::uint32_t>(SkillRead + OwnRecord));
            ASSERT_TRUE(
                Client::Scenes::FireTheArrow(twin.Striker(), nullptr, StrikerSlot, 2, 1, 0, twin.Context()).HasValue());
        }
        twin.random.Discard(static_cast<std::uint32_t>(2 * SkillRead + OwnRecord));
        const auto fired =
            Client::Scenes::FireTheArrow(twin.Striker(), nullptr, StrikerSlot, 2, 0, probe.skill, twin.Context());
        if (probe.cut.empty())
        {
            EXPECT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
        }
        else
        {
            EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        }
        EXPECT_EQ(ErrorOf(stepped), ErrorOf(fired));
        EXPECT_EQ(tested.random.State(), twin.random.State());
        EXPECT_EQ(tested.sounds, twin.sounds);
        const auto arrows = AliveEffects(tested.world);
        EXPECT_EQ(arrows.size(), AliveEffects(twin.world).size());
        EXPECT_GE(arrows.size(), probe.arrows);
        for (const Client::World::ModelEffect* arrow : arrows)
        {
            if (Client::Scenes::IsArrowEffect(arrow->type))
            {
                EXPECT_EQ(arrow->byte89, 2);
            }
        }
    }
}

// 0x2e: lo mismo con el blanco; 0x53e2b0 crea el 0x13a, que corta en su joint (CL11), antes del sonido 0x26e.
TEST(AttackSkillCasesTest, TheBlastCutsInItsJointBeforeItsSound)
{
    SkillWorld tested;
    tested.Striker().byte38e = 0x2e;

    const auto stepped = tested.WithTarget(0x2e);

    EXPECT_TRUE(HasError(stepped, "0x51e01a")) << ErrorOf(stepped);
    EXPECT_TRUE(tested.sounds.empty());
    EXPECT_GE(DrawsSince(Seed, tested.random), 2 * SkillRead + OwnRecord);
}

// 0x5042ce: 0x52e740 con las acciones 0x38 a 0x3c: la chispa 0x5c6/0 en el blanco, sin rand(), y veinte veces dos
// rand() y la 0x5c5/0. Con el pool lleno, sólo los cuarenta rand().
TEST(AttackSkillCasesTest, TheSparkLeavesItsHitAndTwentyFairySparks)
{
    SkillWorld tested;
    tested.withParticles = true;
    tested.Striker().animation.action = 0x3a;

    const auto sparked = SparkTheTarget(tested.Striker(), tested.Victim(), StrikerSlot, tested.Context());

    ASSERT_TRUE(sparked.HasValue()) << ErrorOf(sparked);
    const auto count = [&](std::int32_t type)
    {
        return std::ranges::count_if(tested.world.Particles().All(), [type](const auto& particle)
                                     { return particle.alive && particle.type == type && particle.subtype == 0; });
    };
    EXPECT_EQ(count(0x5c6), 1);
    EXPECT_EQ(count(0x5c5), 20);
    EXPECT_EQ(tested.world.Particles().All()[0].position, tested.Victim().position);
    // La 0x5c6/0 no saca ninguno; cada vuelta, dos y los cinco de la alta de la 0x5c5/0 (0x540333, 0x54041f, 0x540436,
    // 0x54045c y 0x540480).
    EXPECT_EQ(DrawsSince(Seed, tested.random), 20 * (2 + 5));

    SkillWorld full;
    full.withParticles = true;
    for (auto& particle : full.world.Particles().All())
        particle.alive = true;
    ASSERT_TRUE(SparkTheTarget(full.Striker(), full.Victim(), StrikerSlot, full.Context()).HasValue());
    EXPECT_EQ(DrawsSince(Seed, full.random), 40);
}
// 501494→51fff0: el estallido y las piedras preceden al efecto 0xf6/0 sobre o, con o de dueño (0x5014da, CL11).
TEST(AttackSkillCasesTest, Skill0eRunsTheBurstBeforeTheNextCapability)
{
    SkillWorld tested;
    tested.withParticles = true;
    const auto used = tested.Head(0x0e);
    ASSERT_TRUE(used.HasValue()) << used.Error();
    const int plainDraws = DrawsSince(Seed, tested.random);
    EXPECT_TRUE(std::ranges::any_of(tested.world.Particles().All(), [](const auto& p) { return p.alive; }));
    EXPECT_EQ(
        std::ranges::count_if(tested.world.Map().effects.Effects(), [](const auto& effect) { return effect.alive; }),
        17);
    const auto inferno = std::ranges::find_if(tested.world.Map().effects.Effects(),
                                              [](const auto& effect) { return effect.alive && effect.type == 0xf6; });
    ASSERT_NE(inferno, tested.world.Map().effects.Effects().end());
    EXPECT_EQ(inferno->subtype, 0);
    EXPECT_EQ(inferno->ownerContext, &tested.Striker());
    EXPECT_EQ(inferno->transform.position, tested.Striker().position);
    for (auto& effect : tested.world.Map().effects.Effects())
        effect.alive = true;
    for (auto& particle : tested.world.Particles().All())
        particle.alive = true;
    const auto before = tested.random.State();
    const auto full = tested.Head(0x0e);
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_EQ(DrawsSince(before, tested.random), 8 * (20 * 2 + 2) + OwnRecord);

    // Con o de héroe y la tabla libre, el 0xf6/0 copia la cuenta en su registro propio (0x519a66) y pide los blancos a
    // 0x517a20, que lee la skill en el suyo; después la tanda del héroe (0x5014e5) cierra el tercero: tres cierres más
    // que sin el héroe, con el mismo estallido.
    SkillWorld hero;
    hero.withParticles = true;
    hero.world.Runtime().hero = StrikerSlot;
    hero.world.Runtime().heroSlot = static_cast<std::int32_t>(StrikerSlot);
    hero.world.OwnCharacter().bytef04 = 0x41;
    const auto heroUsed = hero.Head(0x0e);
    ASSERT_TRUE(heroUsed.HasValue()) << heroUsed.Error();
    EXPECT_EQ(DrawsSince(Seed, hero.random), plainDraws + 3 * OwnRecord);
    const auto heroInferno = std::ranges::find_if(hero.world.Map().effects.Effects(), [](const auto& effect)
                                                  { return effect.alive && effect.type == 0xf6; });
    ASSERT_NE(heroInferno, hero.world.Map().effects.Effects().end());
    EXPECT_EQ(heroInferno->word8c, 0x41);
    EXPECT_EQ(hero.world.OwnCharacter().bytef04, 0x42);
}

// 0x4fec30..0x4fecb0 (la 0x29), 0x4ffcbe..0x4ffdb7 (la 0x27) y 0x500cd4..0x500dba (la 0x0d), del lote switch de la
// CL11: el 0xf3 sobre o (con su registro propio: la cuenta +0xf04 uno más) y el sonido 0x73; diez 0x108/0 sin dueño en
// la celda de la skill, cada uno con su rand() % 50 antes del alta, la key de la vuelta y el sonido 0x90; y dos 0xf5 en
// la celda con o de dueño, que cortan en el joint 0x618/5 (0x5193d6, lote joints) con lugar en la tabla y siguen sin
// él. Sin las alturas del terreno, la 0x27 y la 0x0d cortan antes de sus rand().
TEST(AttackSkillCasesTest, TheSwitchHeadCasesCreateTheirEffects)
{
    const auto alive = [](SkillWorld& tested, std::int16_t type)
    {
        std::vector<const Client::World::ModelEffect*> found;
        for (const Client::World::ModelEffect& effect : tested.world.Map().effects.Effects())
        {
            if (effect.alive && effect.type == type)
                found.push_back(&effect);
        }
        return found;
    };
    {
        SkillWorld tested;
        ActorState& striker = tested.Striker();
        striker.word8a = 0x0123;
        striker.hands[0].model = 0x212 + 5;
        tested.world.OwnCharacter().SetSkillAt(3, 0x29);
        tested.world.OwnCharacter().bytef04 = 0x41;
        ASSERT_TRUE(tested.Head(0x29).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), 2 * OwnRecord);
        EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x73});
        EXPECT_EQ(tested.world.OwnCharacter().bytef04, 0x42);
        const auto trails = alive(tested, 0xf3);
        ASSERT_EQ(trails.size(), 1u);
        EXPECT_EQ(trails[0]->ownerContext, &striker);
        EXPECT_EQ(trails[0]->transform.position, striker.position);
        EXPECT_EQ(trails[0]->disturbanceState, 0x0123);
        EXPECT_EQ(trails[0]->byte89, 3);
        EXPECT_EQ(trails[0]->itemType, &striker.word8c);
        EXPECT_EQ(striker.word8c, 5);
    }
    {
        SkillWorld tested;
        Flatten(tested.world);
        ActorState& striker = tested.Striker();
        striker.skillCellX = 120;
        striker.skillCellY = 33;
        ASSERT_TRUE(tested.Head(0x27).HasValue());
        constexpr int StormAltaDraws = 5;
        EXPECT_EQ(DrawsSince(Seed, tested.random), 10 * (1 + StormAltaDraws));
        EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x90});
        const auto storm = alive(tested, 0x108);
        ASSERT_EQ(storm.size(), 10u);
        for (std::size_t round = 0; round < storm.size(); ++round)
        {
            EXPECT_EQ(storm[round]->subtype, 0);
            EXPECT_EQ(storm[round]->ownerContext, nullptr);
            EXPECT_EQ(storm[round]->disturbanceState, static_cast<std::uint16_t>(round + 1));
        }
    }
    for (const bool jointsFull : {false, true})
    {
        SCOPED_TRACE(jointsFull);
        SkillWorld tested;
        Flatten(tested.world);
        ActorState& striker = tested.Striker();
        striker.skillCellX = 120;
        striker.skillCellY = 33;
        tested.world.OwnCharacter().SetSkillAt(2, 0x0d);
        for (JointState& joint : tested.joints)
            joint.alive = jointsFull;
        const auto stepped = tested.Head(0x0d);
        constexpr int BlastDraws = 5;
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord + 2 * (BlastDraws + OwnRecord));
        // 0x5193d6: con lugar, cada 0xf5 cuelga su joint 0x618/5 de ancho 100, sin rand() (lote joints); con el pool
        // lleno, ninguno.
        const auto beams = std::ranges::count_if(tested.joints, [](const JointState& joint)
                                                 { return joint.alive && joint.type == 0x618 && joint.subtype == 5; });
        EXPECT_EQ(beams, jointsFull ? 0 : 2);
        const auto blasts = alive(tested, 0xf5);
        ASSERT_EQ(blasts.size(), 2u);
        for (const Client::World::ModelEffect* blast : blasts)
        {
            EXPECT_EQ(blast->ownerContext, &striker);
            EXPECT_EQ(blast->byte89, 2);
            // 0x51931b..0x51936a: 300 a 799 sobre la celda (12.050; 3.350) a la altura 150.
            EXPECT_GE(blast->transform.position.x, 12050.0f + 200.0f);
            EXPECT_GE(blast->transform.position.z, 150.0f + 300.0f);
        }
    }
    for (const std::uint8_t skill : {std::uint8_t{0x27}, std::uint8_t{0x0d}})
    {
        SkillWorld tested;
        const auto cut = tested.Head(skill);
        EXPECT_TRUE(HasError(cut, skill == 0x27 ? "0x4ffd1e" : "0x500d28")) << ErrorOf(cut);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    }
}

TEST(AttackSkillCasesTest, TheFullPoolsPreserveEveryHeadTail)
{
    struct Case
    {
        std::uint8_t skill;
        int draws, sound;
        std::uint8_t volley;
    };
    const std::vector<Case> cases{{0x30, 0, 127, 0},    {0x43, 0, 614, 0},    {0x44, 0, 615, 0},    {0x45, 0, 616, 0},
                                  {0x46, 0, 617, 0},    {0x47, 0, 615, 0},    {0x48, 0, 619, 0},    {0x35, 0, 127, 0},
                                  {0x40, 0, 463, 0},    {0x4b, 0, 626, 0},    {0x3f, 0, 459, 0},    {0x41, 0, 461, 0},
                                  {0x31, 0, 114, 0},    {0x29, 3848, 115, 0}, {0xa, 3848, 119, 0},  {0x28, 0, 147, 0},
                                  {0x26, 3848, 142, 1}, {0x27, 3858, 144, 1}, {0x8, 3848, 116, 0},  {0xc, 3848, 122, 0},
                                  {0xd, 7696, -1, 1},   {0xe, 8032, -1, 1},   {0x9, 19240, 117, 1}, {0x2c, 0, 620, 0}};
    for (const auto& probe : cases)
    {
        SCOPED_TRACE(int{probe.skill});
        SkillWorld tested;
        tested.withParticles = true;
        // La 0x27 y la 0x0d leen la altura de la celda de la skill (0x5f93c0) aun sin lugar en la tabla.
        Flatten(tested.world);
        auto& actor = tested.Hero();
        actor.position = glm::vec3(125.0f, 250.0f, 375.0f);
        actor.skillTarget4c = static_cast<std::int32_t>(VictimSlot);
        actor.attackTarget = static_cast<std::uint16_t>(VictimSlot);
        actor.flags78 = probe.skill == 0x48 ? 0x8000u : 0u;
        if (probe.skill == 0x26)
            Flatten(tested.world);
        for (auto& effect : tested.world.Map().effects.Effects())
            effect.alive = true;
        for (auto& joint : tested.joints)
            joint.alive = true;
        for (auto& particle : tested.world.Particles().All())
            particle.alive = true;
        const auto used = StepTheHeadSkill(actor, HeroSlot, probe.skill, tested.Context());
        ASSERT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(DrawsSince(Seed, tested.random), probe.draws);
        EXPECT_EQ(tested.world.OwnCharacter().bytef04, probe.volley);
        EXPECT_EQ(tested.sounds,
                  probe.sound < 0 ? std::vector<std::int32_t>{} : std::vector<std::int32_t>{probe.sound});
        if (probe.skill == 0x40 || probe.skill == 0x3f || probe.skill == 0x41)
        {
            ASSERT_EQ(tested.soundOrigins.size(), 1u);
            EXPECT_EQ(tested.soundOrigins.front(), actor.position);
        }
        if (probe.skill == 0x48)
        {
            EXPECT_EQ(actor.flags78, 0u);
        }
    }
}

TEST(AttackSkillCasesTest, TheFullPoolsPreserveEveryTargetTail)
{
    struct Case
    {
        std::uint8_t skill;
        std::uint32_t flags;
        int sound;
    };
    const std::vector<Case> cases{{1, 1, 0x22},     {7, 2, 0x78},     {0x1b, 8, -1},    {0x10, 0x100, 0x84},
                                  {2, 0, 0x42},     {4, 0, 0x42},     {0xb, 0, 0x76},   {0x2d, 0, 0x26d},
                                  {0x49, 0, 0x26f}, {0x4a, 0, 0x271}, {0x3c, 0, 0x1c8}, {0x3d, 0, -1},
                                  {0x11, 0, 0x76},  {0x1c, 4, -1}};
    for (const auto& probe : cases)
    {
        SCOPED_TRACE(int{probe.skill});
        SkillWorld tested;
        tested.withParticles = true;
        tested.Striker().skillSucceeded = 1;
        for (auto& effect : tested.world.Map().effects.Effects())
            effect.alive = true;
        for (auto& joint : tested.joints)
            joint.alive = true;
        for (auto& particle : tested.world.Particles().All())
            particle.alive = true;
        const auto used = tested.WithTarget(probe.skill);
        ASSERT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(tested.Victim().flags78, probe.flags);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        EXPECT_EQ(tested.sounds,
                  probe.sound < 0 ? std::vector<std::int32_t>{} : std::vector<std::int32_t>{probe.sound});
    }
}

TEST(AttackSkillCasesTest, MixedPoolsStopAtTheFirstConstructorWithRoom)
{
    {
        SkillWorld tested;
        for (auto& joint : tested.joints)
            joint.alive = true;
        EXPECT_TRUE(tested.Head(0x30).HasValue());
        EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x7f});
        EXPECT_EQ(AliveEffects(tested.world).size(), 2u);
        tested.sounds.clear();
        tested.joints.front().alive = false;
        for (auto& effect : tested.world.Map().effects.Effects())
            effect.alive = true;
        // Con un lugar, el primero de los 36 joints y nada más; los calcos no tienen lugar y suena 0x7f.
        const auto stepped = tested.Head(0x30);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested.joints.front().type, 0x613);
        EXPECT_EQ(tested.joints.front().subtype, 2);
        EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x7f});
    }
    {
        SkillWorld tested;
        tested.withParticles = true;
        for (auto& effect : tested.world.Map().effects.Effects())
            effect.alive = true;
        // 0x50492e..0x504986: con lugar, las diez 0x5f2/1 sobre el blanco, sin el 1 de +0x78 (sin +0x38d), y el sonido.
        ASSERT_TRUE(tested.WithTarget(1).HasValue());
        const auto smokes =
            std::ranges::count_if(tested.world.Particles().All(), [](const auto& particle)
                                  { return particle.alive && particle.type == 0x5f2 && particle.subtype == 1; });
        EXPECT_EQ(smokes, 10);
        EXPECT_EQ(tested.Victim().flags78, 0u);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x22}));
        tested.sounds.clear();
        tested.Striker().skillSucceeded = 1;
        tested.Striker().type = 0x164;
        for (auto& effect : tested.world.Map().effects.Effects())
            effect.alive = false;
        for (auto& particle : tested.world.Particles().All())
            particle.alive = true;
        ASSERT_TRUE(tested.WithTarget(1).HasValue());
        EXPECT_EQ(tested.Victim().flags78, 1u);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x22}));
    }
}

TEST(AttackSkillCasesTest, KillingABuffJointRestoresTheConstructorFrontier)
{
    SkillWorld tested;
    for (auto& joint : tested.joints)
        joint.alive = true;
    auto& killed = tested.joints.front();
    killed.type = 0x10f;
    killed.subtype = 0;
    killed.ownerContext = &tested.Victim();
    // El único lugar que queda es el que apagó: lo toma el primero de los cinco y los demás no entran.
    ASSERT_TRUE(tested.WithTarget(0x10).HasValue());
    EXPECT_TRUE(killed.alive);
    EXPECT_EQ(killed.life, 999999);
    EXPECT_EQ(killed.width, 20.0f);
    EXPECT_EQ(tested.Victim().flags78, 0x100u);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x84}));
}

TEST(AttackSkillCasesTest, BindingFlagsPrecedeTheJointAndExistingBindingsSkipIt)
{
    SkillWorld tested;
    tested.Striker().skillSucceeded = 1;
    for (auto& effect : tested.world.Map().effects.Effects())
        effect.alive = true;
    tested.Striker().attackTarget = 0xfffe;
    ASSERT_TRUE(tested.WithTarget(0x1b).HasValue());
    EXPECT_EQ(tested.Victim().flags78, 8u);
    // 0x5045f9: cinco 10f/4 de ancho 20 con el +0x3a4 de c, con signo, en +0xa0c.
    const auto bindings = [&tested]
    {
        return std::ranges::count_if(tested.joints,
                                     [&tested](const JointState& joint)
                                     {
                                         return joint.alive && joint.type == 0x10f && joint.subtype == 4 &&
                                                joint.ownerContext == &tested.Victim() && joint.target == -2;
                                     });
    };
    EXPECT_EQ(bindings(), 5);
    for (auto& joint : tested.joints)
        joint.alive = false;
    auto& binding = tested.joints.front();
    binding.alive = true;
    binding.type = 0x10f;
    binding.subtype = 9;
    binding.ownerContext = &tested.Victim();
    EXPECT_TRUE(tested.WithTarget(0x1b).HasValue());
    EXPECT_EQ(bindings(), 0);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
}

// 0x4fe505..0x4fe790: la 0x48 apaga +0x78 de o y crea seis 0x10f/5..7 de ancho 170 desde o + 100 en z (la copia
// baja 10 salvo después de los /7), girados 45, 135, 225, 90, 180 y 270; después el sonido 0x26b. Sin rand().
TEST(AttackSkillCasesTest, TheRisingAurasLeaveFromAboveTheCaster)
{
    SkillWorld tested;
    tested.Striker().flags78 = 0x30;
    tested.Striker().position.z = 40;
    ASSERT_TRUE(tested.Head(0x48).HasValue());
    EXPECT_EQ(tested.Striker().flags78, 0u);
    constexpr std::array<float, 6> Turns{45, 135, 225, 90, 180, 270};
    constexpr std::array<float, 6> Heights{140, 130, 120, 120, 110, 100};
    for (std::size_t i = 0; i < Turns.size(); ++i)
    {
        SCOPED_TRACE(i);
        const JointState& rise = tested.joints[i];
        ASSERT_TRUE(rise.alive);
        EXPECT_EQ(rise.type, 0x10f);
        EXPECT_EQ(rise.subtype, static_cast<std::int32_t>(5 + i % 3));
        EXPECT_EQ(rise.width, 170.0f);
        EXPECT_EQ(rise.angles.z, Turns[i]);
        EXPECT_EQ(rise.pathIncrement.z, Heights[i]);
        EXPECT_EQ(rise.ownerContext, &tested.Striker());
    }
    EXPECT_FALSE(tested.joints[6].alive);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x26b}));
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
}

// 0x4fe147..0x4fe18b: la 0x43 crea el 0x632/7 sobre o, que suelta tres 0x632/6 con dos rand() cada uno, y suena
// 0x266. El paso de los /6 corta en su partícula 0x5ee (0x53bf6e) después de su rand().
TEST(AttackSkillCasesTest, TheStormDropsThreeBolts)
{
    SkillWorld tested;
    tested.Striker().angles = {0, 0, 90};
    ASSERT_TRUE(tested.Head(0x43).HasValue());
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x266}));
    const JointState& storm = tested.joints[0];
    ASSERT_TRUE(storm.alive);
    EXPECT_EQ(storm.subtype, 7);
    EXPECT_EQ(storm.life, 0);
    EXPECT_FALSE(storm.tailEnabled);
    for (std::size_t i = 1; i <= 3; ++i)
    {
        SCOPED_TRACE(i);
        const JointState& bolt = tested.joints[i];
        ASSERT_TRUE(bolt.alive);
        EXPECT_EQ(bolt.type, 0x632);
        EXPECT_EQ(bolt.subtype, 6);
        EXPECT_EQ(bolt.life, 25);
        EXPECT_EQ(bolt.volley, 25);
        EXPECT_EQ(bolt.width, 40.0f);
        EXPECT_EQ(bolt.orientation, 3);
        EXPECT_EQ(bolt.count, 0);
        EXPECT_EQ(bolt.ownerContext, nullptr);
        EXPECT_EQ(bolt.pathIncrement, bolt.position);
    }
    EXPECT_EQ(DrawsSince(Seed, tested.random), 6);
    const auto stepped = Client::Scenes::StepJoints(tested.joints, tested.random);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x53bf6e"), std::string::npos) << stepped.Error();
    EXPECT_EQ(DrawsSince(Seed, tested.random), 7);
}

TEST(AttackSkillCasesTest, FullJointsKeepAllStrikeAroundSelectors)
{
    const auto skills = SkillsWithRange(10);
    for (const int count : {0, 1, 10})
    {
        SCOPED_TRACE(count);
        SkillWorld tested;
        tested.skills = &skills;
        tested.Hero().alive = false;
        tested.Victim().alive = false;
        tested.Striker().byte88 = 0;
        tested.Striker().skillTarget4c = static_cast<std::int32_t>(VictimSlot);
        for (int target = 0; target < count; ++target)
            tested.Monster(static_cast<std::size_t>(100 + target), 50.0f);
        for (auto& joint : tested.joints)
            joint.alive = true;
        const auto used = tested.Head(0x4c);
        ASSERT_TRUE(used.HasValue()) << ErrorOf(used);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 23 + count * (4 + SkillEntry));
    }
}

TEST(AttackSkillCasesTest, FullParticlesKeepTheFortySparkSelectors)
{
    SkillWorld tested;
    tested.withParticles = true;
    for (auto& particle : tested.world.Particles().All())
        particle.alive = true;
    const auto sparked = SparkTheTarget(tested.Striker(), tested.Victim(), StrikerSlot, tested.Context());
    ASSERT_TRUE(sparked.HasValue()) << ErrorOf(sparked);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 40);
}

TEST(AttackSkillCasesTest, AFullEffectPoolStillDisturbsKalimaWhenItsOwnerExists)
{
    for (const bool hasOwner : {false, true})
    {
        SCOPED_TRACE(hasOwner);
        SkillWorld tested(24);
        tested.Striker().cellX = 64;
        tested.Striker().cellY = 63;
        if (hasOwner)
            tested.world.Map().kalima = std::make_unique<Client::World::KalimaWorld>(24);
        for (auto& effect : tested.world.Map().effects.Effects())
            effect.alive = true;
        const auto used = tested.Head(0x0a);
        ASSERT_TRUE(used.HasValue()) << ErrorOf(used);
        EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord + 8);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x77}));
        if (hasOwner)
        {
            const auto& waves = tested.world.Map().kalima->Waves();
            EXPECT_NE(waves.planes[0][126 * 256 + 128], 0u);
            EXPECT_EQ(waves.planes[0][0], 0u);
        }
    }
}

// Los efectos vivos de un tipo, en el orden de la tabla.
std::vector<const Client::World::ModelEffect*> AliveOfType(const WorldState& world, std::int16_t type)
{
    std::vector<const Client::World::ModelEffect*> found;
    for (const Client::World::ModelEffect* effect : AliveEffects(world))
    {
        if (effect->type == type)
            found.push_back(effect);
    }
    return found;
}

// 0x5042d3..0x504324: el rumbo de o al blanco.
float HeadingTo(const ActorState& from, const ActorState& to)
{
    return static_cast<float>(
        Client::Render::Models::CreateAngle(from.position.x, from.position.y, to.position.x, to.position.y));
}

// 0x4feabb..0x4feb36, la 0x3f: el 0xcb/2 con o de dueño y el 0xcc/3 sin dueño, los dos sobre o con su luz (vida 250,
// sin rand()), y el sonido 0x1cb desde o.
TEST(AttackSkillCasesTest, TheRaisedRingsGoOverTheStriker)
{
    SkillWorld tested;
    ActorState& striker = tested.Striker();
    striker.position = glm::vec3(1250.0f, 3350.0f, 120.0f);
    striker.angles = glm::vec3(0.0f, 0.0f, 45.0f);
    striker.light = glm::vec3(0.5f, 0.25f, 1.0f);

    ASSERT_TRUE(tested.Head(0x3f).HasValue());

    const auto inner = AliveOfType(tested.world, Client::World::InnerCircleEffect);
    const auto outer = AliveOfType(tested.world, Client::World::OuterCircleEffect);
    ASSERT_EQ(inner.size(), 1u);
    ASSERT_EQ(outer.size(), 1u);
    EXPECT_EQ(inner.front()->subtype, 2);
    EXPECT_EQ(inner.front()->life, 250);
    EXPECT_EQ(inner.front()->ownerContext, &striker);
    EXPECT_EQ(inner.front()->transform.position, striker.position);
    EXPECT_EQ(inner.front()->light, striker.light);
    EXPECT_EQ(outer.front()->subtype, 3);
    EXPECT_EQ(outer.front()->life, 250);
    EXPECT_EQ(outer.front()->ownerContext, nullptr);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x1cb}));
    ASSERT_EQ(tested.soundOrigins.size(), 1u);
    EXPECT_EQ(tested.soundOrigins.front(), striker.position);
}

// 0x4fecb5..0x4fed2a, la 0x0a con otro que el héroe: 0x59fb80(0xa), el 0xcb/0 con o de dueño, +0x8a de o y el lugar de
// la skill en +0x89 (sin el registro propio ni los blancos, que son del héroe), el 0xcc/0 sin dueño y el sonido 0x77.
TEST(AttackSkillCasesTest, TheHellfireLeavesBothCirclesOverTheStriker)
{
    SkillWorld tested;
    ActorState& striker = tested.Striker();
    striker.position = glm::vec3(1250.0f, 3350.0f, 120.0f);
    striker.word8a = 0x0123;
    tested.world.OwnCharacter().SetSkillAt(3, 0x0a);
    tested.world.OwnCharacter().bytef04 = 0x41;

    ASSERT_TRUE(tested.Head(0x0a).HasValue());

    const auto inner = AliveOfType(tested.world, Client::World::InnerCircleEffect);
    const auto outer = AliveOfType(tested.world, Client::World::OuterCircleEffect);
    ASSERT_EQ(inner.size(), 1u);
    ASSERT_EQ(outer.size(), 1u);
    EXPECT_EQ(inner.front()->subtype, 0);
    EXPECT_EQ(inner.front()->life, 45);
    EXPECT_EQ(inner.front()->ownerContext, &striker);
    EXPECT_EQ(inner.front()->disturbanceState, 0x0123);
    EXPECT_EQ(inner.front()->byte89, 3);
    EXPECT_EQ(outer.front()->subtype, 0);
    EXPECT_EQ(outer.front()->life, 40);
    EXPECT_EQ(outer.front()->ownerContext, nullptr);
    EXPECT_EQ(outer.front()->disturbanceState, 0xffff);
    EXPECT_EQ(tested.world.OwnCharacter().bytef04, 0x41);
    EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x77}));
}

// 0x4ff4c3..0x4ff514, la 0x28: el 0xcb/1 oculto (+0x58 en −2) con o de dueño, sin rand(), y el sonido 0x93.
TEST(AttackSkillCasesTest, TheHiddenCircleGoesWithTheStriker)
{
    SkillWorld tested;
    ActorState& striker = tested.Striker();

    ASSERT_TRUE(tested.Head(0x28).HasValue());

    const auto inner = AliveOfType(tested.world, Client::World::InnerCircleEffect);
    ASSERT_EQ(inner.size(), 1u);
    EXPECT_EQ(inner.front()->subtype, 1);
    EXPECT_EQ(inner.front()->objectState.hiddenMesh, -2);
    EXPECT_EQ(inner.front()->ownerContext, &striker);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x93}));
}

// 0x4ff51c..0x4ff5f2, la 0x26: dos 0xc2/6 en el centro de la celda de la skill a la altura del terreno, con la luz
// (0,8f; 0,5; 0,1f), sin dueño y la clave 0 (+0xd8 en 0), cada uno 500 a 799 más alto y con su estela 0x5f2/0 de dueño
// (0x51b098, ancho 100, sin rand()); después el sonido 0x8e y, con el héroe, la tanda en +0xf04. Sin las alturas,
// corta antes del primero y sin rand().
TEST(AttackSkillCasesTest, TheMeteorsFallWithTheirTrails)
{
    SkillWorld tested;
    Flatten(tested.world);
    ActorState& striker = tested.Striker();
    striker.skillCellX = 120;
    striker.skillCellY = 33;

    const auto stepped = tested.Head(0x26);
    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);

    const auto meteors = AliveOfType(tested.world, Client::World::FireEffect);
    ASSERT_EQ(meteors.size(), 2u);
    const auto& joints = tested.joints;
    for (std::size_t index = 0; index < meteors.size(); ++index)
    {
        SCOPED_TRACE(index);
        const Client::World::ModelEffect& meteor = *meteors[index];
        EXPECT_EQ(meteor.subtype, 6);
        EXPECT_EQ(meteor.ownerContext, nullptr);
        EXPECT_EQ(meteor.speed, 0.0f);
        EXPECT_EQ(meteor.light, glm::vec3(0.8f, 0.5f, 0.1f));
        EXPECT_GE(meteor.transform.position.x, 12050.0f + 200.0f);
        EXPECT_LT(meteor.transform.position.x, 12050.0f + 300.0f);
        EXPECT_GE(meteor.transform.position.z, 150.0f + 500.0f);
        EXPECT_LT(meteor.transform.position.z, 150.0f + 800.0f);
        const auto& trail = joints[index];
        ASSERT_TRUE(trail.alive);
        EXPECT_EQ(trail.type, 0x5f2);
        EXPECT_EQ(trail.subtype, 0);
        EXPECT_EQ(trail.ownerContext, &meteor);
        EXPECT_EQ(trail.position, meteor.transform.position);
        EXPECT_EQ(trail.width, 100.0f);
        EXPECT_EQ(trail.life, 20);
        EXPECT_EQ(trail.texture, 0x60f);
    }
    EXPECT_FALSE(joints[2].alive);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 10);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x8e}));

    SkillWorld bare;
    EXPECT_TRUE(HasError(bare.Head(0x26), "0x4ff570"));
    EXPECT_EQ(DrawsSince(Seed, bare.random), 0);
    EXPECT_TRUE(AliveEffects(bare.world).empty());
}

// 0x500c10..0x500c5c, la 0x08: 0x59fb80(8) y el 0xcf/0 sobre o con la luz local de 0x4fd5f0, o de dueño, +0x8a de o y
// el lugar en +0x89, a la altura del terreno; su alta cuenta en el registro propio (+0x8c con la cuenta, que sube).
// Después, el sonido 0x74.
TEST(AttackSkillCasesTest, TheTwisterTakesTheBodyLight)
{
    for (const glm::vec3& light : {Client::Scenes::BodyLight, glm::vec3(0.3f, 0.5f, 1.0f)})
    {
        SkillWorld tested;
        Flatten(tested.world);
        ActorState& striker = tested.Striker();
        striker.position = glm::vec3(1250.0f, 3350.0f, 20.0f);
        striker.light = glm::vec3(0.5f, 0.25f, 1.0f);
        striker.word8a = 0x0123;
        tested.world.OwnCharacter().SetSkillAt(3, 0x08);
        tested.world.OwnCharacter().bytef04 = 0x41;

        const auto stepped = StepTheHeadSkill(striker, StrikerSlot, 0x08, tested.Context(), light);
        ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);

        const auto storms = AliveOfType(tested.world, Client::World::StormEffect);
        ASSERT_EQ(storms.size(), 1u);
        const Client::World::ModelEffect& storm = *storms.front();
        EXPECT_EQ(storm.subtype, 0);
        EXPECT_EQ(storm.light, light);
        EXPECT_EQ(storm.ownerContext, &striker);
        EXPECT_EQ(storm.disturbanceState, 0x0123);
        EXPECT_EQ(storm.byte89, 3);
        EXPECT_EQ(storm.word8c, 0x41);
        EXPECT_EQ(storm.transform.position.z, 150.0f);
        EXPECT_EQ(tested.world.OwnCharacter().bytef04, 0x42);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 2 * OwnRecord);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x74}));
    }
}

// 0x504740..0x504800, la 0x07: el 0xc1/0 sobre el blanco con los ángulos de o y la luz local, cinco pedazos de hielo
// 0xca (cinco rand() cada uno), con +0x38d el 2 en +0x78 del blanco y el sonido 0x78.
TEST(AttackSkillCasesTest, TheIceFreezesTheTarget)
{
    for (const std::uint8_t succeeded : {std::uint8_t{0}, std::uint8_t{1}})
    {
        SCOPED_TRACE(int{succeeded});
        SkillWorld tested;
        ActorState& striker = tested.Striker();
        striker.skillSucceeded = succeeded;
        striker.angles = glm::vec3(0.0f, 0.0f, 90.0f);
        ActorState& victim = tested.Victim();
        victim.position = glm::vec3(2250.0f, 1350.0f, 80.0f);
        const glm::vec3 light(0.3f, 0.5f, 1.0f);

        const auto stepped = StepTheTargetSkill(striker, victim, StrikerSlot, 0x07, tested.Context(), light);
        ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);

        const auto ice = AliveOfType(tested.world, Client::World::IceEffect);
        ASSERT_EQ(ice.size(), 1u);
        EXPECT_EQ(ice.front()->subtype, 0);
        EXPECT_EQ(ice.front()->life, 50);
        EXPECT_EQ(ice.front()->light, light);
        EXPECT_EQ(ice.front()->transform.position, victim.position);
        EXPECT_EQ(AliveOfType(tested.world, Client::World::IceShardEffect).size(), 5u);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 25);
        EXPECT_EQ(victim.flags78, succeeded != 0 ? 2u : 0u);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x78}));
    }
}

// 0x5049bf..0x5049fd, la 0x02: el 0xc2/0 sobre el blanco con sus ángulos y la luz de o, y el sonido 0x42. 0x504b9e, la
// 0x04: el 0xc2/1 desde o hacia el blanco, con él de dueño, y el mismo sonido.
TEST(AttackSkillCasesTest, TheFiresGoToTheTarget)
{
    {
        SkillWorld tested;
        tested.Striker().light = glm::vec3(0.5f, 0.25f, 1.0f);
        ActorState& victim = tested.Victim();
        victim.position = glm::vec3(2250.0f, 1350.0f, 80.0f);

        ASSERT_TRUE(tested.WithTarget(0x02).HasValue());

        const auto fires = AliveOfType(tested.world, Client::World::FireEffect);
        ASSERT_EQ(fires.size(), 1u);
        EXPECT_EQ(fires.front()->subtype, 0);
        EXPECT_EQ(fires.front()->ownerContext, nullptr);
        EXPECT_EQ(fires.front()->light, tested.Striker().light);
        EXPECT_EQ(fires.front()->transform.position.z, 480.0f);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x42}));
    }
    {
        SkillWorld tested;
        ActorState& striker = tested.Striker();
        striker.position = glm::vec3(1250.0f, 3350.0f, 20.0f);
        striker.angles = glm::vec3(5.0f, 7.0f, 90.0f);
        ActorState& victim = tested.Victim();
        victim.position = glm::vec3(2250.0f, 1350.0f, 80.0f);

        ASSERT_TRUE(tested.WithTarget(0x04).HasValue());

        const auto fires = AliveOfType(tested.world, Client::World::FireEffect);
        ASSERT_EQ(fires.size(), 1u);
        EXPECT_EQ(fires.front()->subtype, 1);
        EXPECT_EQ(fires.front()->ownerContext, &victim);
        EXPECT_EQ(fires.front()->transform.angles, glm::vec3(5.0f, 7.0f, HeadingTo(striker, victim)));
        EXPECT_EQ(fires.front()->transform.position.z, 140.0f);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x42}));
    }
}

// 0x504be9..0x504cce, la 0x0b: el 0xce/0 desde o hacia el blanco; el 0x164 crea antes dos más, con el rumbo 10 más y
// 10 menos. El sonido 0x76.
TEST(AttackSkillCasesTest, ThePowerWaveFansOutForTheTriple)
{
    for (const std::uint16_t type : {std::uint16_t{0x20a}, std::uint16_t{0x164}})
    {
        SCOPED_TRACE(type);
        SkillWorld tested;
        ActorState& striker = tested.Striker();
        striker.type = type;
        striker.position = glm::vec3(1250.0f, 3350.0f, 20.0f);
        ActorState& victim = tested.Victim();
        victim.position = glm::vec3(2250.0f, 1350.0f, 80.0f);
        const float heading = HeadingTo(striker, victim);

        ASSERT_TRUE(tested.WithTarget(0x0b).HasValue());

        const auto waves = AliveOfType(tested.world, Client::World::MagicEffect);
        std::vector<float> headings;
        for (const Client::World::ModelEffect* wave : waves)
        {
            EXPECT_EQ(wave->subtype, 0);
            EXPECT_EQ(wave->ownerContext, nullptr);
            headings.push_back(wave->transform.angles.z);
        }
        const auto turned = [heading](double by) { return static_cast<float>(static_cast<double>(heading) + by); };
        const float back = turned(10.0);
        const float other = static_cast<float>(static_cast<double>(back) - 20.0);
        const float last = static_cast<float>(static_cast<double>(other) + 10.0);
        EXPECT_EQ(headings, type == 0x164 ? (std::vector<float>{back, other, last}) : (std::vector<float>{heading}));
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x76}));
    }
}

// 0x504fdc..0x505022, la 0x11 del 0x15e: el 0xd7/0 desde o hacia el blanco, con él de dueño, sin sonido. 0x5048f1, la
// 0x01 del 0x20a: el 0xc3/0 sobre el blanco, antes de las partículas.
TEST(AttackSkillCasesTest, TheSnowballAndThePoisonFindTheTarget)
{
    {
        SkillWorld tested;
        ActorState& striker = tested.Striker();
        striker.type = 0x15e;
        striker.position = glm::vec3(1250.0f, 3350.0f, 20.0f);
        ActorState& victim = tested.Victim();
        victim.position = glm::vec3(2250.0f, 1350.0f, 80.0f);

        ASSERT_TRUE(tested.WithTarget(0x11).HasValue());

        const auto snow = AliveOfType(tested.world, Client::World::SnowEffect);
        ASSERT_EQ(snow.size(), 1u);
        EXPECT_EQ(snow.front()->ownerContext, &victim);
        EXPECT_EQ(snow.front()->transform.angles.z, HeadingTo(striker, victim));
        EXPECT_TRUE(tested.sounds.empty());
    }
    {
        SkillWorld tested;
        tested.Striker().type = 0x20a;
        ActorState& victim = tested.Victim();
        victim.position = glm::vec3(2250.0f, 1350.0f, 80.0f);

        EXPECT_TRUE(HasError(tested.WithTarget(0x01), "0x50497e"));

        const auto poison = AliveOfType(tested.world, Client::World::PoisonEffect);
        ASSERT_EQ(poison.size(), 1u);
        EXPECT_EQ(poison.front()->transform.position, victim.position);
        EXPECT_EQ(poison.front()->life, 40);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    }
}

} // namespace
