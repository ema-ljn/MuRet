#include "Scenes/AttackSkillCases.h"

#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/ArrowEffects.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ModelEffects.h"
#include "World/KalimaWorld.h"
#include "World/KalimaWaves.h"
#include "World/Particles.h"
#include "World/WorldTerrain.h"

#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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
// con el registro propio cerrado, el rand() % 50 de la 0x27 y los dos de 0x51fe10 de la 0x0e, que corta en su primera
// chispa). La 0x48 deja +0x78 en 0 antes de cortar.
TEST(AttackSkillCasesTest, TheHeadCasesCutAfterTheirDraws)
{
    struct Case
    {
        std::uint8_t skill;
        std::string_view cut;
        int draws;
    };
    const std::array cases{
        Case{0x30, "0x4fe127", 0},         Case{0x45, "0x4fe360", 0},         Case{0x3f, "0x4feae8", 0},
        Case{0x31, "0x4fec15", 0},         Case{0x29, "0x4fec9a", OwnRecord}, Case{0x0a, "0x4fecf3", OwnRecord},
        Case{0x28, "0x4ff4ef", 0},         Case{0x26, "0x4ff5b2", 0},         Case{0x27, "0x4ffdaf", 1},
        Case{0x08, "0x500c49", OwnRecord}, Case{0x0c, "0x500cb9", OwnRecord}, Case{0x0d, "0x500d7b", OwnRecord},
        Case{0x0e, "0x51fe9e", 2},         Case{0x2c, "0x502975", 0},
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
// 300) más o. Suena 0x7f, sin rand().
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
        const bool near = index < 2;
        const double argument = near ? 4.5 : 22.5;
        const glm::vec3 from{1000.0f + 80.0f * static_cast<float>(near ? std::sin(argument) : std::cos(argument)),
                             2000.0f + 80.0f * static_cast<float>(near ? std::cos(argument) : std::sin(argument)),
                             350.0f};
        const glm::vec3 center = (joint.points[0][0] + joint.points[0][1]) * 0.5f;
        EXPECT_NEAR(center.x, from.x, 1e-3f);
        EXPECT_NEAR(center.y, from.y, 1e-3f);
        EXPECT_NEAR(center.z, from.z, 1e-3f);
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

// 0x4fe015..0x4fe0e4: el primero de los 36 joints 0x613/2 de la 0x30, colgado de o 100 más arriba con los ángulos
// (−10, 0, 0), sin rand(); 0x53265e y 0x533173: la mezcla 1, la segunda cara, velocidad 50, la luz 0,5, vida 20, tres
// puntos y la partida en +0x24. El calco 0x61e/4 de la vuelta 0 corta (0x4fe127).
TEST(AttackSkillCasesTest, TheSpiritRingStartsWithItsFirstJoint)
{
    SkillWorld tested;
    tested.Striker().position = {1000.0f, 2000.0f, 50.0f};

    const auto stepped = tested.Head(0x30);

    EXPECT_TRUE(HasError(stepped, "0x4fe127")) << ErrorOf(stepped);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    const JointState& joint = tested.joints.front();
    ASSERT_TRUE(joint.alive);
    EXPECT_EQ(joint.type, 0x613);
    EXPECT_EQ(joint.subtype, 2);
    EXPECT_EQ(joint.position, glm::vec3(1000.0f, 2000.0f, 150.0f));
    EXPECT_EQ(joint.angles, glm::vec3(-10.0f, 0.0f, 0.0f));
    EXPECT_EQ(joint.width, 60.0f);
    EXPECT_EQ(joint.blend, 1);
    EXPECT_EQ(joint.drawEnabled, 2);
    EXPECT_EQ(joint.speed, 50.0f);
    EXPECT_EQ(joint.light, glm::vec3(0.5f));
    EXPECT_EQ(joint.life, 20);
    EXPECT_EQ(joint.maximumCount, 3);
    EXPECT_EQ(joint.pathIncrement, joint.position);
    EXPECT_EQ(joint.ownerContext, &tested.Striker());
    EXPECT_FALSE(tested.joints[1].alive);
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

// Con el registro propio abierto (+0x38e de 0x40ad90 en uso), 0x59fb80 no saca rand().
TEST(AttackSkillCasesTest, AnOpenOwnRecordFindsTheSkillWithoutDraws)
{
    SkillWorld tested;
    tested.world.Runtime().ownRecordDepth = 1;

    const auto stepped = tested.Head(0x08);

    EXPECT_TRUE(HasError(stepped, "0x500c49")) << ErrorOf(stepped);
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

// 0x46: con blanco, 0x534f90(0x10f, blanco, 4) y (…, 9) apagan los joints de él con esos subtipos, y el calco 0x61e/6
// corta; sin blanco, nada; sin el pool de joints, corta antes.
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

    EXPECT_TRUE(HasError(tested.Head(0x46), "0x4fe424"));

    EXPECT_FALSE(tested.joints[0].alive);
    EXPECT_FALSE(tested.joints[1].alive);
    EXPECT_TRUE(tested.joints[2].alive);
    EXPECT_TRUE(tested.joints[3].alive);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);

    tested.withJoints = false;
    EXPECT_TRUE(HasError(tested.Head(0x46), "0x4fe3ce"));
}

// 0x40 y 0x4b: con otra cosa que 0xa21 en la derecha, o que 0xa19 o un escudo (0xe12..0x1011) en la izquierda, el
// 0x117 en el hueso corta; con las dos, la 0x40 suena (0x1cf) y la 0x4b corta en el 0x138.
TEST(AttackSkillCasesTest, TheHandsDecideTheRaise)
{
    struct Case
    {
        std::uint16_t right;
        std::uint16_t left;
        std::string_view raise;
        std::string_view other;
    };
    const std::array cases{
        Case{0xffff, 0xa19, "0x4fea10", "0x502a23"}, Case{0xa21, 0xffff, "0x4fea9b", "0x502ab4"},
        Case{0xa21, 0xa20, "0x4fea9b", "0x502ab4"},  Case{0xa21, 0x1012, "0x4fea9b", "0x502ab4"},
        Case{0xa21, 0xa19, "", "0x502ae6"},          Case{0xa21, 0xe12, "", "0x502ae6"},
        Case{0xa21, 0x1011, "", "0x502ae6"},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << probe.right << " y " << probe.left);
        SkillWorld tested(2);
        tested.Striker().position = glm::vec3(125.0f, 250.0f, 375.0f);
        tested.Striker().hands[0].model = probe.right;
        tested.Striker().hands[1].model = probe.left;

        const auto raised = tested.Head(0x40);
        const auto other = tested.Head(0x4b);

        if (probe.raise.empty())
        {
            EXPECT_TRUE(raised.HasValue()) << ErrorOf(raised);
            EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x1cf}));
            ASSERT_EQ(tested.soundOrigins.size(), 1u);
            EXPECT_EQ(tested.soundOrigins.front(), tested.Striker().position);
        }
        else
        {
            EXPECT_TRUE(HasError(raised, probe.raise)) << ErrorOf(raised);
            EXPECT_TRUE(tested.sounds.empty());
        }
        EXPECT_TRUE(HasError(other, probe.other)) << ErrorOf(other);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    }
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

// Los casos con blanco que crean lo que MuRet no tiene (CL11), sin rand() antes.
TEST(AttackSkillCasesTest, TheTargetCasesCutWithoutDraws)
{
    struct Case
    {
        std::uint8_t skill;
        std::uint16_t type;
        std::string_view cut;
    };
    const std::array cases{
        Case{0x07, 0x20a, "0x504768"}, Case{0x01, 0x20a, "0x504926"}, Case{0x01, 0x0e, "0x50497e"},
        Case{0x02, 0x20a, "0x5049ef"}, Case{0x2d, 0x20a, "0x504a3a"}, Case{0x4a, 0x20a, "0x504b80"},
        Case{0x04, 0x20a, "0x504bce"}, Case{0x0b, 0x164, "0x504c38"}, Case{0x0b, 0x20a, "0x504cc0"},
        Case{0x3c, 0x20a, "0x504d05"}, Case{0x3d, 0x20a, "0x504e40"}, Case{0x11, 0x15e, "0x50501a"},
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
    EXPECT_TRUE(HasError(tested.WithTarget(0x1b), "0x504561"));
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
    EXPECT_TRUE(AliveEffects(tested.world).empty());
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

// 0x5042ce: 0x52e740 con las acciones 0x38 a 0x3c empieza por la partícula 0x5c6/0 (CL11), sin rand() antes.
TEST(AttackSkillCasesTest, TheSparkCutsAtItsFirstParticle)
{
    SkillWorld tested;
    tested.Striker().animation.action = 0x3a;

    const auto sparked = SparkTheTarget(tested.Striker(), tested.Victim(), StrikerSlot, tested.Context());

    EXPECT_TRUE(HasError(sparked, "0x52e77b")) << ErrorOf(sparked);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
}
// 501494→51fff0: el estallido y las piedras preceden al efecto 0xf6.
TEST(AttackSkillCasesTest, Skill0eRunsTheBurstBeforeTheNextCapability)
{
    SkillWorld tested;
    tested.withParticles = true;
    const auto used = tested.Head(0x0e);
    ASSERT_FALSE(used.HasValue());
    EXPECT_NE(used.Error().find("0x5014da"), std::string::npos) << used.Error();
    EXPECT_TRUE(std::ranges::any_of(tested.world.Particles().All(), [](const auto& p) { return p.alive; }));
    EXPECT_EQ(
        std::ranges::count_if(tested.world.Map().effects.Effects(), [](const auto& effect) { return effect.alive; }),
        16);
    for (auto& effect : tested.world.Map().effects.Effects())
        effect.alive = true;
    for (auto& particle : tested.world.Particles().All())
        particle.alive = true;
    const auto before = tested.random.State();
    const auto full = tested.Head(0x0e);
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_EQ(DrawsSince(before, tested.random), 8 * (20 * 2 + 2) + OwnRecord);
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
        auto& actor = tested.Hero();
        actor.position = glm::vec3(125.0f, 250.0f, 375.0f);
        actor.skillTarget4c = static_cast<std::int32_t>(VictimSlot);
        actor.attackTarget = static_cast<std::uint16_t>(VictimSlot);
        actor.flags78 = probe.skill == 0x48 ? 0x8000u : 0u;
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
        EXPECT_TRUE(HasError(tested.Head(0x30), "0x4fe127"));
        EXPECT_TRUE(tested.sounds.empty());
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
        EXPECT_TRUE(HasError(tested.WithTarget(1), "0x50497e"));
        EXPECT_TRUE(tested.sounds.empty());
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

} // namespace
