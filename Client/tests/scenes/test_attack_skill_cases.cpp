#include "Scenes/AttackSkillCases.h"

#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/ArrowEffects.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ModelEffects.h"

#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
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
                                    .joints = withJoints ? std::span<JointState>(joints) : std::span<JointState>{},
                                    .soundRequest = [this](std::int32_t sound) { sounds.push_back(sound); },
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
    bool controlDown = false;
    const Client::Data::Tables::SkillTable* skills = nullptr;
    std::vector<JointState> joints = std::vector<JointState>(16);
    bool withJoints = true;
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
// con el registro propio cerrado, y el rand() % 50 de la 0x27). La 0x48 deja +0x78 en 0 antes de cortar.
TEST(AttackSkillCasesTest, TheHeadCasesCutAfterTheirDraws)
{
    struct Case
    {
        std::uint8_t skill;
        std::string_view cut;
        int draws;
    };
    const std::array cases{
        Case{0x1e, "0x4fdf1e", 0},         Case{0x24, "0x4fdf1e", 0},         Case{0x30, "0x4fe0e4", 0},
        Case{0x43, "0x4fe172", 0},         Case{0x45, "0x4fe322", 0},         Case{0x48, "0x4fe590", 0},
        Case{0x35, "0x4fe856", 0},         Case{0x3f, "0x4feae8", 0},         Case{0x41, "0x4febbb", 0},
        Case{0x31, "0x4fec15", 0},         Case{0x29, "0x4fec9a", OwnRecord}, Case{0x0a, "0x4fecf3", OwnRecord},
        Case{0x28, "0x4ff4ef", 0},         Case{0x26, "0x4ff5b2", 0},         Case{0x27, "0x4ffdaf", 1},
        Case{0x05, "0x500531", OwnRecord}, Case{0x08, "0x500c49", OwnRecord}, Case{0x0c, "0x500cb9", OwnRecord},
        Case{0x0d, "0x500d7b", OwnRecord}, Case{0x0e, "0x501494", 0},         Case{0x09, "0x501c67", OwnRecord},
        Case{0x2c, "0x502975", 0},
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
            EXPECT_EQ(tested.Striker().flags78, probe.skill == 0x48 ? 0u : 0x4000u);
            EXPECT_TRUE(tested.sounds.empty());
        }
    }
}

// Con el registro propio abierto (+0x38e de 0x40ad90 en uso), 0x59fb80 no saca rand().
TEST(AttackSkillCasesTest, AnOpenOwnRecordFindsTheSkillWithoutDraws)
{
    SkillWorld tested;
    tested.world.Runtime().ownRecordDepth = 1;

    const auto stepped = tested.Head(0x05);

    EXPECT_TRUE(HasError(stepped, "0x500531")) << ErrorOf(stepped);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
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
        tested.Striker().hands[0].model = probe.right;
        tested.Striker().hands[1].model = probe.left;

        const auto raised = tested.Head(0x40);
        const auto other = tested.Head(0x4b);

        if (probe.raise.empty())
        {
            EXPECT_TRUE(raised.HasValue()) << ErrorOf(raised);
            EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x1cf}));
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
        Case{0x02, 0x20a, "0x5049ef"}, Case{0x2d, 0x20a, "0x504a3a"}, Case{0x49, 0x20a, "0x504b33"},
        Case{0x4a, 0x20a, "0x504b80"}, Case{0x04, 0x20a, "0x504bce"}, Case{0x0b, 0x164, "0x504c38"},
        Case{0x0b, 0x20a, "0x504cc0"}, Case{0x3c, 0x20a, "0x504d05"}, Case{0x3d, 0x20a, "0x504e40"},
        Case{0x1c, 0x20a, "0x50470f"}, Case{0x11, 0x15e, "0x50501a"}, Case{0x11, 0x20a, "0x505053"},
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

        EXPECT_TRUE(HasError(tested.WithTarget(0x10), "0x5048db"));

        EXPECT_EQ(tested.Victim().flags78, 0x100u);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x84}));
        EXPECT_FALSE(tested.joints[0].alive);
        EXPECT_TRUE(tested.joints[1].alive);
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
// 0x53e2b0, que corre igual en un mundo gemelo. La flecha de la 0x34 corta en la estela 0x105 (CL11).
TEST(AttackSkillCasesTest, TheArrowCasesReadTheSkillBeforeShooting)
{
    struct Case
    {
        std::uint8_t skill;
        bool volley;
        std::size_t arrows;
        std::string_view cut;
    };
    for (const Case& probe : {Case{0x33, false, 1, ""}, Case{0x34, false, 1, "0x51a08a"}, Case{0x18, true, 4, ""}})
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

    const auto sparked = SparkTheTarget(tested.Striker(), tested.Victim(), StrikerSlot);

    EXPECT_TRUE(HasError(sparked, "0x52e77b")) << ErrorOf(sparked);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
}
} // namespace
