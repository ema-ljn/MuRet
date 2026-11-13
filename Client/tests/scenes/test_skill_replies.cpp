#include "Scenes/SkillReplies.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/SkillTable.h"
#include "Protocol/Skill.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorFlags.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/ModelEffects.h"

#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// Las skills que llegan (CL9 hito 4, 06 § Las skills que llegan): las claves y los personajes son inventados.
namespace
{
using Client::Protocol::Skill::ActorEffect;
using Client::Protocol::Skill::DurationMagic;
using Client::Protocol::Skill::MagicAttack;
using Client::Protocol::Skill::MagicCancel;
using Client::Protocol::Skill::SkillList;
using Client::Protocol::Skill::SkillListEntry;
using Client::Protocol::Skill::SkillTarget;
using Client::Protocol::Skill::SummonLife;
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;

constexpr std::uint16_t HeroKey = 0x21;
constexpr std::uint16_t CasterKey = 0x1234;
constexpr std::uint16_t TargetKey = 0x0456;
constexpr std::size_t HeroIndex = 7;
constexpr std::size_t CasterIndex = 3;
constexpr std::size_t TargetIndex = 5;
constexpr std::uint16_t MonsterType = 0x1a;
// +0x38e y +0x42e: un rand() al cerrarlos en 0.
constexpr int ActorByteRandoms = 1;
constexpr int OwnRecordRandoms = 3848;
constexpr int SkillEntryRandoms = 60;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

// Skill.bmd inventado: las skills de `hidden` con el +0x31 en 1.
Client::Data::Tables::SkillTable SkillsHiding(std::initializer_list<std::uint8_t> hidden)
{
    std::vector<std::byte> plain(Client::Data::Tables::SkillTableSize);
    for (const std::uint8_t skill : hidden)
        plain[std::size_t{skill} * Client::Data::Tables::SkillRecordSize + 0x31] = std::byte{1};
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Data::Tables::SkillRecordSize);
    return Client::Data::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

// El esqueleto de los jugadores y el de un monstruo inventado.
class SkillModels final : public Client::Scenes::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == MonsterType || model == Client::Data::Models::PlayerSkeletonModel ? &skeleton : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    Client::Render::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
};

struct Replies
{
    Replies()
    {
        world.Runtime().hero = HeroIndex;
        Place(HeroIndex, HeroKey, Client::Tests::UnarmedPlayer());
        Place(CasterIndex, CasterKey, Client::Tests::UnarmedPlayer());
        Place(TargetIndex, TargetKey, Client::Tests::UnarmedPlayer());
        Actor(CasterIndex).position = {1000.0f, 1000.0f, 0.0f};
        Actor(TargetIndex).position = {1300.0f, 1200.0f, 0.0f};
        Actor(TargetIndex).seen = true;
    }
    void Place(std::size_t index, std::uint16_t key, ActorState actor)
    {
        actor.alive = true;
        actor.key = key;
        world.Actors()[index] = actor;
    }
    ActorState& Actor(std::size_t index)
    {
        return world.Actors()[index];
    }
    Client::Scenes::WorldEntryResources Resources()
    {
        Client::Scenes::WorldEntryResources resources{.world = world,
                                                      .selection = selection,
                                                      .dataRoot = {},
                                                      .cloth = cloth,
                                                      .camera = camera,
                                                      .cameraInput = input,
                                                      .models = models,
                                                      .log = log};
        resources.skills = skills;
        return resources;
    }
    Client::Core::Expected<void, std::string> Attack(std::uint8_t skill, std::uint16_t source = CasterKey,
                                                     bool succeeded = true)
    {
        return Client::Scenes::ApplyMagicAttack(MagicAttack{skill, source, TargetKey, succeeded}, Resources());
    }
    std::size_t AliveEffects(std::int16_t type)
    {
        std::size_t count = 0;
        for (const auto& effect : world.Map().effects.Effects())
            count += effect.alive && effect.type == type ? 1u : 0u;
        return count;
    }

    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth;
    SkillModels models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    const Client::Data::Tables::SkillTable* skills = nullptr;
};

// ---------------------------------------------------------------------------------------------------------------
// 0x19 (0x4cc530)

// Sobre otro: el rumbo hacia el blanco, el blanco en +0x3a4, +0x38d, la skill en +0x38e (un rand()) y la rama 0x13.
TEST(SkillRepliesTest, TheMagicAttackOfAnotherTurnsTowardsTheTargetAndActs)
{
    auto tested = std::make_unique<Replies>();
    const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms);
    ASSERT_TRUE(tested->Attack(0x13).HasValue());
    const ActorState& caster = tested->Actor(CasterIndex);
    EXPECT_EQ(caster.angles.z,
              static_cast<float>(Client::Render::Models::CreateAngle(1000.0f, 1000.0f, 1300.0f, 1200.0f)));
    EXPECT_EQ(caster.attackTarget, TargetIndex);
    EXPECT_EQ(caster.skillSucceeded, 1);
    EXPECT_EQ(caster.byte38e, 0x13);
    EXPECT_EQ(caster.byte38eAccesses, 0);
    EXPECT_EQ(caster.animation.action, 0x38);
    EXPECT_EQ(caster.byte381, 1);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, 0x70);
    EXPECT_EQ(tested->world.Runtime().skillSource, static_cast<std::int32_t>(CasterIndex));
    EXPECT_EQ(tested->cloth.random.State(), expected.State());
}

// El héroe no gira, y en la 0x10 tampoco golpea: solo +0x381.
TEST(SkillRepliesTest, TheMagicAttackOfTheHeroKeepsItsDirection)
{
    auto tested = std::make_unique<Replies>();
    ActorState& hero = tested->Actor(HeroIndex);
    hero.angles.z = 45.0f;
    const std::uint8_t action = hero.animation.action;
    ASSERT_TRUE(tested->Attack(0x10, HeroKey, false).HasValue());
    EXPECT_EQ(hero.angles.z, 45.0f);
    EXPECT_EQ(hero.skillSucceeded, 0);
    EXPECT_EQ(hero.byte38e, 0x10);
    EXPECT_EQ(hero.animation.action, action);
    EXPECT_EQ(hero.byte381, 1);
    EXPECT_EQ(tested->world.Runtime().skillSource, static_cast<std::int32_t>(HeroIndex));
}

// Sin el blanco (o con su bit 15, que el lector ya sacó y queda en el éxito), nada.
TEST(SkillRepliesTest, TheMagicAttackWithoutTheTargetDoesNothing)
{
    auto tested = std::make_unique<Replies>();
    tested->world.Runtime().skillSource = -5;
    const auto applied =
        Client::Scenes::ApplyMagicAttack(MagicAttack{0x13, CasterKey, 0x0999, true}, tested->Resources());
    ASSERT_TRUE(applied.HasValue());
    EXPECT_EQ(tested->Actor(CasterIndex).byte38e, 0);
    EXPECT_EQ(tested->world.Runtime().skillSource, -5);
    EXPECT_EQ(tested->cloth.random.State(), 1u);
}

// Q-C1237: una fuente que no está es el 400 (0x4cce50). Lo de antes de la rama va ahí: 0x5767964, el rumbo hacia el
// blanco, +0x3a4, +0x38d y la skill en +0x38e con su rand(); la rama, que en el original sale de lo que haya en el 400
// (su tipo, su montura), se descarta sin cortar: ni la acción, ni +0x381, ni el sonido, ni lo que toca del blanco. La
// 0x3b no tiene nada antes de la rama. En Lorencia y Devias.
TEST(SkillRepliesTest, AnUnknownSourceIsTheLastActor)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t skill :
             {std::uint8_t{0x13}, std::uint8_t{0x10}, std::uint8_t{0x4b}, std::uint8_t{0x3b}})
        {
            SCOPED_TRACE(std::to_string(map) + " " + std::to_string(skill));
            auto tested = std::make_unique<Replies>();
            tested->world.Runtime().map = map;
            const bool head = skill != 0x3b;
            const CrtRandom expected = Advanced(tested->cloth.random, head ? ActorByteRandoms : 0);
            const std::uint8_t action = tested->Actor(400).animation.action;
            ASSERT_TRUE(tested->Attack(skill, 0x0777).HasValue());
            const ActorState& phantom = tested->Actor(400);
            EXPECT_EQ(tested->world.Runtime().skillSource, 400);
            EXPECT_EQ(phantom.byte38e, head ? skill : 0);
            EXPECT_EQ(phantom.attackTarget, head ? TargetIndex : 0);
            EXPECT_EQ(phantom.skillSucceeded, head ? 1 : 0);
            EXPECT_EQ(phantom.angles.z,
                      head ? static_cast<float>(Client::Render::Models::CreateAngle(0.0f, 0.0f, 1300.0f, 1200.0f))
                           : 0.0f);
            EXPECT_EQ(phantom.animation.action, action);
            EXPECT_EQ(phantom.byte381, 0);
            EXPECT_EQ(tested->Actor(TargetIndex).flags78, 0u);
            EXPECT_FALSE(tested->world.Runtime().lastSoundId.has_value());
            EXPECT_EQ(tested->cloth.random.State(), expected.State());
        }
    }
}

// La 0x10 de otro jugador: 0x4f5290 abre los stats propios (0x4f3f80) y saca el rand() de la acción 0x8e o 0x8f.
TEST(SkillRepliesTest, TheHitOfAnotherPlayerDrawsAfterTheSpeeds)
{
    auto tested = std::make_unique<Replies>();
    CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms + OwnRecordRandoms);
    const int bit = expected.Next() & 1;
    ASSERT_TRUE(tested->Attack(0x10).HasValue());
    EXPECT_EQ(tested->Actor(CasterIndex).animation.action, 0x8e + bit);
    EXPECT_EQ(tested->Actor(CasterIndex).animation.frame, 0.0f);
    EXPECT_EQ(tested->cloth.random.State(), expected.State());
}

// En un monstruo, 3 y 4 de a tres con +0x38f.
TEST(SkillRepliesTest, TheHitOfAMonsterAlternatesEveryThree)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.type = MonsterType;
    ASSERT_TRUE(tested->Attack(0x10).HasValue());
    EXPECT_EQ(caster.animation.action, 3);
    EXPECT_EQ(caster.attackCount, 1);
    ASSERT_TRUE(tested->Attack(0x10).HasValue());
    EXPECT_EQ(caster.animation.action, 4);
    EXPECT_EQ(caster.attackCount, 2);
}

// 0x4cd3e0 → 0x4cd68e: las ramas 1, 2, 4, 7, 0xb y 0x11 en un monstruo hacen la acción de ataque de 0x4f4a10
// (SetActorAttack: la 3 con +0x38f en 0, que sube dos veces), el frame en 0 y +0x381 en 1. Los rand(): el cierre de
// +0x38e y el del sonido de pegar sin la tabla del modelo (P-C253). En Lorencia, Devias y Kalima (un monstruo que no es
// de los de 0x43a2f0).
TEST(SkillRepliesTest, TheStrikeOfAMonsterIsTheAttackAction)
{
    for (const std::int32_t map : {0, 2, 0x18})
    {
        for (const int skill : {1, 2, 4, 7, 0xb, 0x11})
        {
            SCOPED_TRACE(std::to_string(map) + " " + std::to_string(skill));
            auto tested = std::make_unique<Replies>();
            tested->world.Runtime().map = map;
            ActorState& caster = tested->Actor(CasterIndex);
            caster.type = MonsterType;
            caster.animation.frame = 2.0f;
            const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms + 1);
            ASSERT_TRUE(tested->Attack(static_cast<std::uint8_t>(skill)).HasValue());
            EXPECT_EQ(caster.byte38e, skill);
            EXPECT_EQ(caster.animation.action, 3);
            EXPECT_EQ(caster.attackCount, 2);
            EXPECT_EQ(caster.animation.frame, 0.0f);
            EXPECT_EQ(caster.byte381, 1);
            EXPECT_EQ(tested->cloth.random.State(), expected.State());
        }
    }
}

// 0x4cd316, 0x4cd34b y 0x4cd35b: la 0xc9, la 0xca y la 0xc8 de un jugador pegan con 0x4f4a10 (sin armas, la 0x22, con
// los 3848 rand() de 0x4f3f80) y vuelven sin el frame en 0 (ya en la 0x22, 0x4ea030 no lo toca); las dos primeras
// prenden su bandera y +0x381.
TEST(SkillRepliesTest, TheMonsterSkillsOfAPlayerAreTheAttackAction)
{
    for (const auto& [skill, flag, settled] :
         {std::tuple{0xc9, 0x100u, 1}, std::tuple{0xca, 0x8u, 1}, std::tuple{0xc8, 0u, 0}})
    {
        SCOPED_TRACE(skill);
        auto tested = std::make_unique<Replies>();
        ActorState& caster = tested->Actor(CasterIndex);
        caster.animation.action = 0x22;
        caster.animation.frame = 2.0f;
        const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms + OwnRecordRandoms);
        ASSERT_TRUE(tested->Attack(static_cast<std::uint8_t>(skill)).HasValue());
        EXPECT_EQ(caster.animation.action, 0x22);
        EXPECT_EQ(caster.attackCount, 1);
        EXPECT_EQ(caster.flags78, flag);
        EXPECT_EQ(caster.byte381, settled);
        EXPECT_EQ(caster.animation.frame, 2.0f);
        EXPECT_EQ(tested->cloth.random.State(), expected.State());
    }
}

// 0x4cd681 y 0x4cdd21: la 0x37 sin salir y la 0x3f en un monstruo terminan en 0x4f4a10 (la 3, que pisa la 0x4d de la
// 0x3f), el frame en 0 y +0x381; en un jugador, la 0x37 vuelve sin nada y la 0x3f se queda con su acción.
TEST(SkillRepliesTest, TheSkills37And3FOfAMonsterEndInTheAttackAction)
{
    for (const std::uint8_t skill : {std::uint8_t{0x37}, std::uint8_t{0x3f}})
    {
        SCOPED_TRACE(int{skill});
        auto monster = std::make_unique<Replies>();
        monster->Actor(CasterIndex).type = MonsterType;
        ASSERT_TRUE(monster->Attack(skill, CasterKey, false).HasValue());
        EXPECT_EQ(monster->Actor(CasterIndex).animation.action, 3);
        EXPECT_EQ(monster->Actor(CasterIndex).attackCount, 2);
        EXPECT_EQ(monster->Actor(CasterIndex).byte381, 1);

        auto player = std::make_unique<Replies>();
        ASSERT_TRUE(player->Attack(skill, CasterKey, false).HasValue());
        EXPECT_EQ(player->Actor(CasterIndex).attackCount, 0);
        EXPECT_EQ(player->Actor(CasterIndex).byte381, skill == 0x3f ? 1 : 0);
    }
}

// 0x4cdda4: la 0x31 en Atlans (8) y en el cielo (10) es la 0x41; en Lorencia, Devias y los demás, la 0x40.
TEST(SkillRepliesTest, TheSkill31DependsOnTheMap)
{
    for (const auto& [map, action] :
         {std::pair{0, 0x40}, std::pair{2, 0x40}, std::pair{8, 0x41}, std::pair{10, 0x41}, std::pair{7, 0x40}})
    {
        auto tested = std::make_unique<Replies>();
        tested->world.Runtime().map = map;
        ASSERT_TRUE(tested->Attack(0x31).HasValue()) << map;
        EXPECT_EQ(tested->Actor(CasterIndex).animation.action, action) << map;
        EXPECT_EQ(tested->Actor(CasterIndex).byte381, 1) << map;
    }
}

// Las monturas de las ramas 0x43 a 0x48, fuera de la zona segura.
TEST(SkillRepliesTest, TheMountChoosesTheActionOfTheLordSkills)
{
    for (const auto& [model, safe, action] :
         {std::tuple{std::uint16_t{0x1c16}, 0, 0x52}, std::tuple{std::uint16_t{0x1c14}, 0, 0x40},
          std::tuple{std::uint16_t{0x1c15}, 0, 0x41}, std::tuple{std::uint16_t{0x1c37}, 0, 0x5d},
          std::tuple{std::uint16_t{0x1c16}, 1, 0x3f}, std::tuple{Client::Scenes::NoItemModel, 0, 0x3f}})
    {
        auto tested = std::make_unique<Replies>();
        tested->Actor(CasterIndex).pet.model = model;
        tested->Actor(CasterIndex).safeZone = static_cast<std::uint8_t>(safe);
        ASSERT_TRUE(tested->Attack(0x43).HasValue());
        EXPECT_EQ(tested->Actor(CasterIndex).animation.action, action) << model << " " << safe;
    }
}

// 0x4cd410: el blanco se va y la fuente llega, cada uno con su destello 0x5c6 y el 0x76; después el 0x83.
TEST(SkillRepliesTest, TheTeleportSkillSwapsTheVanishing)
{
    auto tested = std::make_unique<Replies>();
    tested->world.Runtime().movement.gateRequestBlocked = 1;
    tested->Actor(HeroIndex).position = {1000.0f, 1000.0f, 0.0f};
    ASSERT_TRUE(tested->Attack(0xf, HeroKey).HasValue());
    const ActorState& hero = tested->Actor(HeroIndex);
    const ActorState& target = tested->Actor(TargetIndex);
    // La 0xf no gira la fuente.
    EXPECT_EQ(hero.angles.z, 0.0f);
    EXPECT_EQ(target.alphaTarget, 0.0f);
    EXPECT_EQ(target.byte80, 1);
    EXPECT_EQ(target.animation.action, 0x93);
    EXPECT_EQ(hero.alphaTarget, 1.0f);
    EXPECT_EQ(hero.byte80, 3);
    EXPECT_EQ(hero.animation.frame, 5.0f);
    // 0x4cd442 salta a 0x4cde69, después del +0x381 de 0x4cde62.
    EXPECT_EQ(hero.byte381, 0);
    EXPECT_EQ(tested->AliveEffects(0x5c6), 2u);
    EXPECT_EQ(tested->world.Runtime().movement.gateRequestBlocked, 0);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, 0x83);
}

// 0x4cdb0f: la 0x44 salida baja el 0x1000 del blanco (0x4e6910) y apaga su 0x628/5.
TEST(SkillRepliesTest, TheSucceededSkill44LowersTheTargetFlag)
{
    auto tested = std::make_unique<Replies>();
    ActorState& target = tested->Actor(TargetIndex);
    target.flags78 = 0x1001;
    // El 0x628/5 del blanco, puesto a mano: MuRet no crea el 0x628 (CL11).
    auto& sword = tested->world.Map().effects.Effects()[4];
    sword.alive = true;
    sword.type = 0x628;
    sword.subtype = 5;
    sword.ownerContext = &target;
    ASSERT_TRUE(tested->Attack(0x44).HasValue());
    EXPECT_EQ(target.flags78, 1u);
    EXPECT_FALSE(sword.alive);

    auto failed = std::make_unique<Replies>();
    failed->Actor(TargetIndex).flags78 = 0x1001;
    ASSERT_TRUE(failed->Attack(0x44, CasterKey, false).HasValue());
    EXPECT_EQ(failed->Actor(TargetIndex).flags78, 0x1001u);
}

TEST(SkillRepliesTest, TheSkill46MarksAnotherTarget)
{
    auto tested = std::make_unique<Replies>();
    ASSERT_TRUE(tested->Attack(0x46).HasValue());
    EXPECT_EQ(tested->Actor(TargetIndex).flags78, 0x4000u);
}

// Los efectos de 0x518cf0 (CL11) cortan después de lo que el original hace antes.
TEST(SkillRepliesTest, TheEffectsOfTheSkillsAreCL11Cuts)
{
    for (const auto& [skill, where] :
         {std::pair{std::uint8_t{0x37}, "0x4cd664"}, std::pair{std::uint8_t{0x33}, "0x4cd6ff"},
          std::pair{std::uint8_t{0x45}, "0x4cdb72"}, std::pair{std::uint8_t{0x35}, "0x4cdc88"},
          std::pair{std::uint8_t{0x30}, "0x4cdd84"}, std::pair{std::uint8_t{0x3b}, "0x4cddfa"}})
    {
        auto tested = std::make_unique<Replies>();
        const auto applied = tested->Attack(skill);
        ASSERT_FALSE(applied.HasValue()) << int{skill};
        EXPECT_NE(applied.Error().find(where), std::string::npos) << applied.Error();
        EXPECT_NE(applied.Error().find("CL11"), std::string::npos) << applied.Error();
    }
}

// 0x4ccea1: la 0x3b no gira, ni toca +0x3a4, +0x38d ni +0x38e (sin el rand()).
TEST(SkillRepliesTest, TheSkill3bGoesStraightToTheTable)
{
    auto tested = std::make_unique<Replies>();
    static_cast<void>(tested->Attack(0x3b));
    EXPECT_EQ(tested->Actor(CasterIndex).byte38e, 0);
    EXPECT_EQ(tested->Actor(CasterIndex).attackTarget, 0);
    EXPECT_EQ(tested->Actor(CasterIndex).angles.z, 0.0f);
    EXPECT_EQ(tested->cloth.random.State(), 1u);
}

// 0x4ccef1: la 0x4c también deja el blanco en +0x434.
TEST(SkillRepliesTest, TheSkill4cKeepsItsTarget)
{
    auto tested = std::make_unique<Replies>();
    ASSERT_TRUE(tested->Attack(0x4c).HasValue());
    EXPECT_EQ(tested->Actor(CasterIndex).skillTarget4c, static_cast<std::int32_t>(TargetIndex));
}

// 0x4cde2f..0x4cde58: la 0x28 del héroe suelta el derecho: el soltado (0x7d36840), el clic (0x7d36880), el abajo
// (0x7d3685c) y los frames de la carga (0x7d368a4). La de otro no los toca.
TEST(SkillRepliesTest, TheSkill28OfTheHeroDropsTheRightButton)
{
    auto tested = std::make_unique<Replies>();
    auto& ui = tested->world.Runtime().ui;
    ui.rightReleased = true;
    ui.rightClick = true;
    ui.rightDown = true;
    ui.rightHoldFrames = 12;
    ASSERT_TRUE(tested->Attack(0x28, HeroKey).HasValue());
    EXPECT_FALSE(ui.rightReleased);
    EXPECT_FALSE(ui.rightClick);
    EXPECT_FALSE(ui.rightDown);
    EXPECT_EQ(ui.rightHoldFrames, 0);
    EXPECT_EQ(tested->Actor(HeroIndex).animation.action, 0x45);
    EXPECT_EQ(tested->Actor(HeroIndex).byte381, 1);

    auto other = std::make_unique<Replies>();
    auto& otherUi = other->world.Runtime().ui;
    otherUi.rightReleased = true;
    otherUi.rightClick = true;
    otherUi.rightHoldFrames = 12;
    ASSERT_TRUE(other->Attack(0x28).HasValue());
    EXPECT_TRUE(otherUi.rightReleased);
    EXPECT_TRUE(otherUi.rightClick);
    EXPECT_EQ(otherUi.rightHoldFrames, 12);
}

// Las manos de 0x4cc110: un bastón en la izquierda, otro arma en la derecha, y el elfo con alas.
TEST(SkillRepliesTest, TheStaffSkillsActByTheHands)
{
    auto staff = std::make_unique<Replies>();
    staff->Actor(CasterIndex).hands[1].model = 0xa12;
    ASSERT_TRUE(staff->Attack(0x18).HasValue());
    EXPECT_EQ(staff->Actor(CasterIndex).animation.action, 0x2e);

    auto winged = std::make_unique<Replies>();
    winged->Actor(CasterIndex).characterClass = 2;
    winged->Actor(CasterIndex).wings.model = 0xc00;
    winged->Actor(CasterIndex).hands[0].model = 0xa1a;
    ASSERT_TRUE(winged->Attack(0x34).HasValue());
    EXPECT_EQ(winged->Actor(CasterIndex).animation.action, 0x31);

    auto bare = std::make_unique<Replies>();
    const std::uint8_t before = bare->Actor(CasterIndex).animation.action;
    ASSERT_TRUE(bare->Attack(0x36).HasValue());
    EXPECT_EQ(bare->Actor(CasterIndex).animation.action, before);
    EXPECT_EQ(bare->Actor(CasterIndex).byte381, 1);
}

// ---------------------------------------------------------------------------------------------------------------
// 0x1E (0x4ce000)

// Sobre otro jugador: la skill en +0x38e, el rumbo, la acción de la tabla con el frame en 0 y la celda.
TEST(SkillRepliesTest, TheDurationMagicOfAnotherPlayerActsAndKeepsTheCell)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.attackTarget = 3;
    caster.byte380 = 9;
    const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms);
    ASSERT_TRUE(Client::Scenes::ApplyDurationMagic(DurationMagic{0xc, CasterKey, 0x80, 0x81, 0x40}, tested->Resources())
                    .HasValue());
    EXPECT_EQ(caster.byte38e, 0xc);
    EXPECT_EQ(caster.angles.z, static_cast<float>(64.0 * static_cast<double>(1.0f / 255.0f) * 360.0));
    EXPECT_EQ(caster.animation.action, 0x94);
    EXPECT_EQ(caster.skillCellX, 0x80);
    EXPECT_EQ(caster.skillCellY, 0x81);
    EXPECT_EQ(caster.byte381, 1);
    EXPECT_EQ(caster.attackTarget, 0xffff);
    EXPECT_EQ(caster.byte380, 0);
    EXPECT_EQ(tested->cloth.random.State(), expected.State());
}

// 0x4cea84 (0x18 y 0x34) y 0x4ceaa9 (0x36): otro jugador hace la acción de ataque de 0x4f4a10 (sin armas, la 0x22 con
// los 3848 rand() de 0x4f3f80) y el frame en 0, en Lorencia, Devias y Atlans (8).
TEST(SkillRepliesTest, TheDurationMagicOfAnotherPlayerCanBeTheAttackAction)
{
    for (const std::int32_t map : {0, 2, 8})
    {
        for (const std::uint8_t skill : {std::uint8_t{0x18}, std::uint8_t{0x34}, std::uint8_t{0x36}})
        {
            SCOPED_TRACE(std::to_string(map) + " " + std::to_string(skill));
            auto tested = std::make_unique<Replies>();
            tested->world.Runtime().map = map;
            ActorState& caster = tested->Actor(CasterIndex);
            caster.animation.frame = 2.0f;
            const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms + OwnRecordRandoms);
            ASSERT_TRUE(
                Client::Scenes::ApplyDurationMagic(DurationMagic{skill, CasterKey, 4, 5, 0}, tested->Resources())
                    .HasValue());
            EXPECT_EQ(caster.animation.action, 0x22);
            EXPECT_EQ(caster.attackCount, 1);
            EXPECT_EQ(caster.animation.frame, 0.0f);
            EXPECT_EQ(caster.skillCellX, 4);
            EXPECT_EQ(caster.byte381, 1);
            EXPECT_EQ(tested->cloth.random.State(), expected.State());
        }
    }
}

// Sobre el héroe: sin acción, pero con la celda.
TEST(SkillRepliesTest, TheDurationMagicOfTheHeroOnlyKeepsTheCell)
{
    auto tested = std::make_unique<Replies>();
    ActorState& hero = tested->Actor(HeroIndex);
    const std::uint8_t action = hero.animation.action;
    ASSERT_TRUE(
        Client::Scenes::ApplyDurationMagic(DurationMagic{0xe, HeroKey, 10, 11, 0}, tested->Resources()).HasValue());
    EXPECT_EQ(hero.byte38e, 0xe);
    EXPECT_EQ(hero.animation.action, action);
    EXPECT_EQ(hero.skillCellX, 10);
    EXPECT_EQ(hero.skillCellY, 11);
    EXPECT_EQ(hero.attackTarget, 0xffff);
}

// 0x4cece8: un monstruo hace la acción de ataque de 0x4f4a10 después de la skill y el rumbo, con el frame en 0, y
// sigue con la celda, +0x381, +0x3a4 y +0x380. Los rand(): el cierre de +0x38e y el del sonido de pegar.
TEST(SkillRepliesTest, TheDurationMagicOfAMonsterIsTheAttackAction)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.type = MonsterType;
    caster.animation.frame = 2.0f;
    caster.byte380 = 2;
    const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms + 1);
    ASSERT_TRUE(
        Client::Scenes::ApplyDurationMagic(DurationMagic{0xc, CasterKey, 1, 2, 0xff}, tested->Resources()).HasValue());
    EXPECT_EQ(caster.byte38e, 0xc);
    EXPECT_EQ(caster.animation.action, 3);
    EXPECT_EQ(caster.attackCount, 2);
    EXPECT_EQ(caster.animation.frame, 0.0f);
    EXPECT_EQ(caster.skillCellX, 1);
    EXPECT_EQ(caster.skillCellY, 2);
    EXPECT_EQ(caster.byte381, 1);
    EXPECT_EQ(caster.attackTarget, 0xffff);
    EXPECT_EQ(caster.byte380, 0);
    EXPECT_EQ(tested->cloth.random.State(), expected.State());
    // 255 · (1/255) · 360 en double pasa de 360 y queda en el float de arriba.
    EXPECT_EQ(tested->Actor(CasterIndex).angles.z,
              static_cast<float>(255.0 * static_cast<double>(1.0f / 255.0f) * 360.0));
    EXPECT_GT(tested->Actor(CasterIndex).angles.z, 360.0f);
}

// Q-C1204: sin el personaje de la clave, el 400: la skill en +0x38e (su rand()), el rumbo, el frame en 0, la celda,
// +0x381, +0x3a4 y +0x380. La acción, que en el original sale del tipo que haya en el 400, se descarta sin cortar y
// sin rand(). El 1B también va al 400. En Lorencia y Devias.
TEST(SkillRepliesTest, TheDurationMagicWithoutTheKeyWritesTheLastActor)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<Replies>();
        tested->world.Runtime().map = map;
        ActorState& phantom = tested->Actor(400);
        phantom.animation.frame = 2.0f;
        phantom.byte380 = 4;
        const std::uint8_t action = phantom.animation.action;
        const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms);
        ASSERT_TRUE(
            Client::Scenes::ApplyDurationMagic(DurationMagic{0xc, 0x0777, 1, 2, 0x40}, tested->Resources()).HasValue());
        EXPECT_EQ(phantom.byte38e, 0xc);
        EXPECT_EQ(phantom.angles.z, static_cast<float>(64.0 * static_cast<double>(1.0f / 255.0f) * 360.0));
        EXPECT_EQ(phantom.animation.action, action);
        EXPECT_EQ(phantom.attackCount, 0);
        EXPECT_EQ(phantom.animation.frame, 0.0f);
        EXPECT_EQ(phantom.skillCellX, 1);
        EXPECT_EQ(phantom.skillCellY, 2);
        EXPECT_EQ(phantom.byte381, 1);
        EXPECT_EQ(phantom.attackTarget, 0xffff);
        EXPECT_EQ(phantom.byte380, 0);
        EXPECT_EQ(tested->cloth.random.State(), expected.State());
        phantom.flags78 = 8;
        Client::Scenes::ApplyMagicCancel(MagicCancel{0x1b, 0x0777}, tested->Resources());
        EXPECT_EQ(phantom.flags78, 0u);
    }
}

TEST(SkillRepliesTest, TheDurationTableChoosesTheActionOfTheOtherPlayer)
{
    for (const auto& [skill, model, action] : {std::tuple{std::uint8_t{0xa}, Client::Scenes::NoItemModel, 0x96},
                                               std::tuple{std::uint8_t{0x41}, std::uint16_t{0x1c37}, 0x5b},
                                               std::tuple{std::uint8_t{0x41}, Client::Scenes::NoItemModel, 0x94},
                                               std::tuple{std::uint8_t{0x2a}, Client::Scenes::NoItemModel, 0x3e},
                                               std::tuple{std::uint8_t{0x37}, Client::Scenes::NoItemModel, 0x3d},
                                               std::tuple{std::uint8_t{0x46}, Client::Scenes::NoItemModel, 0x3f},
                                               std::tuple{std::uint8_t{0x4c}, Client::Scenes::NoItemModel, 0x62}})
    {
        auto tested = std::make_unique<Replies>();
        tested->Actor(CasterIndex).pet.model = model;
        ASSERT_TRUE(Client::Scenes::ApplyDurationMagic(DurationMagic{skill, CasterKey, 1, 2, 3}, tested->Resources())
                        .HasValue());
        EXPECT_EQ(tested->Actor(CasterIndex).animation.action, action) << int{skill};
    }
}

// ---------------------------------------------------------------------------------------------------------------
// 0x1B (0x4cbe90), 0x07 (0x4cbe00) y 0x4e6910

TEST(SkillRepliesTest, TheMagicCancelLowersItsFlag)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.flags78 = 0x4fff;
    Client::Scenes::ApplyMagicCancel(MagicCancel{0x1b, CasterKey}, tested->Resources());
    EXPECT_EQ(caster.flags78, 0x4ff7u);
    Client::Scenes::ApplyMagicCancel(MagicCancel{0x46, CasterKey}, tested->Resources());
    EXPECT_EQ(caster.flags78, 0x0ff7u);
    // Una skill sin bandera no hace nada.
    Client::Scenes::ApplyMagicCancel(MagicCancel{0x99, CasterKey}, tested->Resources());
    EXPECT_EQ(caster.flags78, 0x0ff7u);
}

// 0x4cbfc8: la 0xc9 pasa antes por 0x439d40, que en un monstruo baja el 0x100 y pone +0x110 en 0x21.
TEST(SkillRepliesTest, TheMonsterCancelSettlesBeforeTheFlag)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.type = MonsterType;
    caster.flags78 = 0x108;
    Client::Scenes::ApplyMagicCancel(MagicCancel{0xc9, CasterKey}, tested->Resources());
    EXPECT_EQ(caster.state110, 0x21);
    EXPECT_EQ(caster.flags78, 0u);
}

// Con el tipo 1 y la máscara prendida, +0x7c queda en 0x200000 si lo tenía; sin prender, 0x4e6380 (hito 1).
TEST(SkillRepliesTest, TheActorStateOfTypeOneRewritesTheSecondFlags)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.flags78 = 0x30;
    caster.flags7c = 0x200123;
    ASSERT_TRUE(
        Client::Scenes::ApplyActorState(Client::Protocol::Skill::ActorState{1, 0x10, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(caster.flags7c, 0x200000u);
    caster.flags7c = 0x123;
    ASSERT_TRUE(
        Client::Scenes::ApplyActorState(Client::Protocol::Skill::ActorState{1, 0x10, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(caster.flags7c, 0u);
    // 0x4cbe47: sin prender, 0x4e6380 con la máscara (ApplyEffectFlags): el 4 se prende y +0x7c queda.
    ASSERT_TRUE(
        Client::Scenes::ApplyActorState(Client::Protocol::Skill::ActorState{1, 0x4, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(caster.flags78, 0x34u);
    EXPECT_EQ(caster.flags7c, 0u);
    // El 0x1000 prende su bit y corta en los joints (CL11).
    const auto joints =
        Client::Scenes::ApplyActorState(Client::Protocol::Skill::ActorState{1, 0x1000, CasterKey}, tested->Resources());
    ASSERT_FALSE(joints.HasValue());
    EXPECT_NE(joints.Error().find("0x4e662d"), std::string::npos) << joints.Error();
    EXPECT_EQ(caster.flags78, 0x1034u);
}

// 0x4cbe45: 0x4e6380 sabe si es el héroe publicado ([0x743a170]): el 0x2000000 corta en él y en otro se prende.
TEST(SkillRepliesTest, TheActorStateOfTypeOneKnowsThePublishedHero)
{
    auto tested = std::make_unique<Replies>();
    const auto hero = Client::Scenes::ApplyActorState(Client::Protocol::Skill::ActorState{1, 0x2000000, HeroKey},
                                                      tested->Resources());
    ASSERT_FALSE(hero.HasValue());
    EXPECT_NE(hero.Error().find("0x4e68f5"), std::string::npos) << hero.Error();
    ASSERT_TRUE(Client::Scenes::ApplyActorState(Client::Protocol::Skill::ActorState{1, 0x2000000, CasterKey},
                                                tested->Resources())
                    .HasValue());
    EXPECT_EQ(tested->Actor(CasterIndex).flags78, 0x2000000u);
}

TEST(SkillRepliesTest, TheActorStateOfAnotherTypeLowersTheLitFlag)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.flags78 = 0x30;
    ASSERT_TRUE(
        Client::Scenes::ApplyActorState(Client::Protocol::Skill::ActorState{2, 0x20, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(caster.flags78, 0x10u);
    // Sin prender, nada.
    ASSERT_TRUE(
        Client::Scenes::ApplyActorState(Client::Protocol::Skill::ActorState{2, 0x20, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(caster.flags78, 0x10u);
}

// 0x4e6cb6: el 0x100000 baja el suyo de +0x7c, los trece de +0x78 y apaga los efectos y los joints del personaje.
TEST(SkillRepliesTest, TheBigClearLowersEverything)
{
    auto tested = std::make_unique<Replies>();
    ActorState& actor = tested->Actor(CasterIndex);
    actor.flags78 = 0xffffffffu;
    actor.flags7c = 0x100001u;
    auto& joints = tested->world.DrawTail().joints;
    joints[0].alive = true;
    joints[0].type = 0x10f;
    joints[0].subtype = 9;
    joints[0].ownerContext = &actor;
    joints[1].alive = true;
    joints[1].type = 0x10f;
    joints[1].subtype = 9;
    joints[1].ownerContext = &tested->Actor(TargetIndex);
    Client::Scenes::RemoveActorFlag(0x100000, actor, tested->world);
    EXPECT_EQ(actor.flags7c, 1u);
    EXPECT_EQ(actor.flags78, 0xffffffffu & ~0x5f7fu);
    EXPECT_FALSE(joints[0].alive);
    EXPECT_TRUE(joints[1].alive);
}

// 0x4e6c23: los tipos 0x204 a 0x208 restan el 0x10000 de +0x7c, lo tengan o no.
TEST(SkillRepliesTest, SomeTypesLowerTheFlag10000FromTheSecondFlags)
{
    auto tested = std::make_unique<Replies>();
    ActorState& actor = tested->Actor(CasterIndex);
    actor.type = 0x205;
    actor.flags78 = 0x10000;
    Client::Scenes::RemoveActorFlag(0x10000, actor, tested->world);
    EXPECT_EQ(actor.flags78, 0x10000u);
    EXPECT_EQ(actor.flags7c, 0u - 0x10000u);
}

// ---------------------------------------------------------------------------------------------------------------
// 0x69 (0x4cc290) y 0x48 (0x4dbf50)

TEST(SkillRepliesTest, TheSkillTargetKeepsTheSkillAndTheTarget)
{
    auto tested = std::make_unique<Replies>();
    const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms);
    ASSERT_TRUE(
        Client::Scenes::ApplySkillTarget(SkillTarget{0x20, CasterKey, TargetKey}, tested->Resources()).HasValue());
    const ActorState& caster = tested->Actor(CasterIndex);
    EXPECT_EQ(caster.byte42e, 0x20);
    EXPECT_EQ(caster.attackTarget, TargetIndex);
    EXPECT_EQ(caster.skillSucceeded, 0);
    EXPECT_EQ(tested->world.Runtime().skillSource, static_cast<std::int32_t>(CasterIndex));
    EXPECT_EQ(tested->cloth.random.State(), expected.State());

    // Con el bit 15, 0x5110d0 no lo encuentra: nada.
    ASSERT_TRUE(Client::Scenes::ApplySkillTarget(
                    SkillTarget{0x21, CasterKey, static_cast<std::uint16_t>(TargetKey | 0x8000)}, tested->Resources())
                    .HasValue());
    EXPECT_EQ(caster.byte42e, 0x20);
}

// 0x4cc503..0x4cc51c: con las skills 1 a 0xd, 0x4f4a10 (un jugador sin armas: los 3848 rand() de 0x4f3f80 y la
// 0x22), el frame en 0 y +0x381 en 1; con 0 o 0xe, nada de eso.
TEST(SkillRepliesTest, TheSkillTargetOfTheFirstSkillsIsTheAttackAction)
{
    for (const std::uint8_t skill : {std::uint8_t{1}, std::uint8_t{0xd}})
    {
        auto tested = std::make_unique<Replies>();
        ActorState& caster = tested->Actor(CasterIndex);
        caster.animation.frame = 2.0f;
        const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms + OwnRecordRandoms);
        ASSERT_TRUE(
            Client::Scenes::ApplySkillTarget(SkillTarget{skill, CasterKey, TargetKey}, tested->Resources()).HasValue());
        EXPECT_EQ(caster.byte42e, skill);
        EXPECT_EQ(caster.animation.action, 0x22);
        EXPECT_EQ(caster.attackCount, 1);
        EXPECT_EQ(caster.animation.frame, 0.0f);
        EXPECT_EQ(caster.byte381, 1);
        EXPECT_EQ(tested->cloth.random.State(), expected.State());
    }
    for (const std::uint8_t skill : {std::uint8_t{0}, std::uint8_t{0xe}})
    {
        auto tested = std::make_unique<Replies>();
        ASSERT_TRUE(
            Client::Scenes::ApplySkillTarget(SkillTarget{skill, CasterKey, TargetKey}, tested->Resources()).HasValue());
        EXPECT_EQ(tested->Actor(CasterIndex).byte381, 0);
        EXPECT_EQ(tested->Actor(CasterIndex).attackCount, 0);
    }
}

// Q-C1237: una fuente que no está es el 400 (0x4cc2b0): 0x5767964, la skill en +0x42e (su rand()), +0x3a4 y +0x38d, y
// con las skills 1 a 0xd el frame en 0 y +0x381 en 1; el 0x4f4a10 del 400, que sale del tipo que haya ahí, se descarta
// sin cortar y sin rand(). En Lorencia y Devias.
TEST(SkillRepliesTest, TheSkillTargetOfAnUnknownSourceWritesTheLastActor)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<Replies>();
        tested->world.Runtime().map = map;
        ActorState& phantom = tested->Actor(400);
        phantom.animation.frame = 2.0f;
        const std::uint8_t action = phantom.animation.action;
        const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms);
        ASSERT_TRUE(
            Client::Scenes::ApplySkillTarget(SkillTarget{1, 0x0777, TargetKey}, tested->Resources()).HasValue());
        EXPECT_EQ(tested->world.Runtime().skillSource, 400);
        EXPECT_EQ(phantom.byte42e, 1);
        EXPECT_EQ(phantom.attackTarget, TargetIndex);
        EXPECT_EQ(phantom.skillSucceeded, 0);
        EXPECT_EQ(phantom.animation.action, action);
        EXPECT_EQ(phantom.attackCount, 0);
        EXPECT_EQ(phantom.animation.frame, 0.0f);
        EXPECT_EQ(phantom.byte381, 1);
        EXPECT_EQ(tested->cloth.random.State(), expected.State());
    }
}

TEST(SkillRepliesTest, TheLevelUpEffectOfAPlayerIsACL11Cut)
{
    auto tested = std::make_unique<Replies>();
    tested->Actor(CasterIndex).byte88 = 1;
    const auto applied = Client::Scenes::ApplyActorEffect(ActorEffect{CasterKey, 0x10}, tested->Resources());
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4dbfc5"), std::string::npos) << applied.Error();
    EXPECT_TRUE(Client::Scenes::ApplyActorEffect(ActorEffect{CasterKey, 0x11}, tested->Resources()).HasValue());
    EXPECT_TRUE(Client::Scenes::ApplyActorEffect(ActorEffect{0x0777, 0x10}, tested->Resources()).HasValue());
    tested->Actor(CasterIndex).byte88 = 2;
    EXPECT_TRUE(Client::Scenes::ApplyActorEffect(ActorEffect{CasterKey, 0x10}, tested->Resources()).HasValue());
}

// ---------------------------------------------------------------------------------------------------------------
// F3 11 (0x4c5070) y F3 20

// La lista entera: con la lista en 0, los 256 en 0, las entradas y la cuenta de las 64 primeras, con su entrada de
// Skill.bmd abierta y cerrada.
TEST(SkillRepliesTest, TheWholeSkillListCountsTheShownAndTheHidden)
{
    auto tested = std::make_unique<Replies>();
    const auto skills = SkillsHiding({0x29});
    tested->skills = &skills;
    auto& own = tested->world.OwnCharacter();
    own.SetSkillAt(9, 0x33);
    own.SetSkillAt(0x90, 0x34);
    const CrtRandom expected =
        Advanced(tested->cloth.random, OwnRecordRandoms + 2 * SkillEntryRandoms + OwnRecordRandoms);
    const SkillList list{
        .count = 2, .list = 0, .slot = 0, .skill = 0, .entries = {SkillListEntry{0, 0x11}, SkillListEntry{1, 0x29}}};
    ASSERT_TRUE(Client::Scenes::ApplySkillList(list, tested->Resources()).HasValue());
    EXPECT_EQ(own.SkillAt(0), 0x11);
    EXPECT_EQ(own.SkillAt(1), 0x29);
    EXPECT_EQ(own.SkillAt(9), 0);
    EXPECT_EQ(own.SkillAt(0x90), 0);
    EXPECT_EQ(own.byte6a, 2);
    EXPECT_EQ(own.byte6b, 1);
    EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested->cloth.random.State(), expected.State());
}

// 0x4c539b: el Dark Lord recibe las cuatro órdenes del cuervo en 0x78 a 0x7b, que no cuentan.
TEST(SkillRepliesTest, TheDarkLordGetsTheRavenOrders)
{
    auto tested = std::make_unique<Replies>();
    const auto skills = SkillsHiding({});
    tested->skills = &skills;
    tested->Actor(HeroIndex).characterClass = 4;
    const SkillList list{.count = 1, .list = 1, .slot = 0, .skill = 0, .entries = {SkillListEntry{0, 0x3c}}};
    ASSERT_TRUE(Client::Scenes::ApplySkillList(list, tested->Resources()).HasValue());
    const auto& own = tested->world.OwnCharacter();
    for (std::uint8_t order = 0x78; order < 0x7c; ++order)
        EXPECT_EQ(own.SkillAt(order), order);
    EXPECT_EQ(own.byte6a, 1);
}

// 0xfe agrega y 0xff saca, y +0x425 del héroe vuelve a 0 sin skill y saltea las 0x43 a 0x48.
TEST(SkillRepliesTest, OneSkillAddedOrRemovedFixesTheHeroSkill)
{
    auto tested = std::make_unique<Replies>();
    const auto skills = SkillsHiding({});
    tested->skills = &skills;
    auto& own = tested->world.OwnCharacter();
    own.SetSkillAt(0, 0x43);
    own.SetSkillAt(1, 0x48);
    own.SetSkillAt(2, 0x11);
    ActorState& hero = tested->Actor(HeroIndex);
    hero.skillIndex = 0;
    const SkillList add{.count = 0xfe, .list = 0, .slot = 3, .skill = 0x12, .entries = {}};
    ASSERT_TRUE(Client::Scenes::ApplySkillList(add, tested->Resources()).HasValue());
    EXPECT_EQ(own.SkillAt(3), 0x12);
    EXPECT_EQ(own.byte6a, 4);
    EXPECT_EQ(hero.skillIndex, 2);

    hero.skillIndex = 3;
    const SkillList remove{.count = 0xff, .list = 0, .slot = 3, .skill = 0, .entries = {}};
    ASSERT_TRUE(Client::Scenes::ApplySkillList(remove, tested->Resources()).HasValue());
    EXPECT_EQ(own.SkillAt(3), 0);
    EXPECT_EQ(own.byte6a, 3);
    // 3 >= +0x6a: a 0, y de ahí saltea las dos del Dark Lord.
    EXPECT_EQ(hero.skillIndex, 2);
}

// La lista en 2 vacía el lugar.
TEST(SkillRepliesTest, TheListTwoClearsTheSlot)
{
    auto tested = std::make_unique<Replies>();
    const auto skills = SkillsHiding({});
    tested->skills = &skills;
    auto& own = tested->world.OwnCharacter();
    own.SetSkillAt(5, 0x11);
    own.SetSkillAt(6, 0x12);
    const SkillList clear{.count = 1, .list = 2, .slot = 5, .skill = 0, .entries = {}};
    ASSERT_TRUE(Client::Scenes::ApplySkillList(clear, tested->Resources()).HasValue());
    EXPECT_EQ(own.SkillAt(5), 0);
    EXPECT_EQ(own.SkillAt(6), 0x12);
    EXPECT_EQ(own.byte6a, 1);
}

TEST(SkillRepliesTest, TheSkillListWithoutSkillBmdIsACut)
{
    auto tested = std::make_unique<Replies>();
    const SkillList add{.count = 0xfe, .list = 0, .slot = 0, .skill = 0x11, .entries = {}};
    const auto applied = Client::Scenes::ApplySkillList(add, tested->Resources());
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4c54ed"), std::string::npos) << applied.Error();
    EXPECT_EQ(tested->world.OwnCharacter().SkillAt(0), 0x11);
}

TEST(SkillRepliesTest, TheSummonLifeGoesToTheHud)
{
    auto tested = std::make_unique<Replies>();
    Client::Scenes::ApplySummonLife(SummonLife{0x42}, tested->Resources());
    EXPECT_EQ(tested->world.Runtime().hud.summonLife, 0x42);
}
} // namespace
