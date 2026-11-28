#include "Models/IModelLibrary.h"
#include "Combat/MagicAttackCount.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Kernel/PlayerKey.h"
#include "Tables/PlayerModels.h"
#include "Protocol/Actors/Viewport.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Assets/FaunaModels.h"
#include "Kernel/DrawTailState.h"
#include "Effects/HandShine.h"
#include "Kernel/OwnCharacterRecord.h"
#include "FrontEnd/SelectionState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"

#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "Session/ModuleStates.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// ApplyMagicAttackCount: the 1A (0x4cedd0). The keys, cells and life are invented; the expected values come from the
// ASM (0x4cedd0, 0x53d810, 0x53fdb0, 0x548e40).
namespace
{
using Client::Kernel::ActorState;
using Client::Particles::HandShineParticle;

constexpr std::uint16_t CasterKey = 0x1234;
constexpr std::size_t CasterSlot = 12;
constexpr std::uint16_t PlayerKey = 0x0777;
constexpr std::size_t HeroSlot = 30;
constexpr std::uint16_t FirstKey = 0x0101;
constexpr std::size_t FirstSlot = 20;
constexpr std::uint16_t SecondKey = 0x0102;
constexpr std::size_t SecondSlot = 21;
constexpr std::uint8_t Magic = 0x29;
// 0x152 + 1, Monster02.bmd, with the actions up to 0x96.
constexpr std::uint16_t MonsterModel = 0x153;
constexpr std::size_t MonsterActionCount = 0x97;
// 0x40b3f0 and 0x40b4d0: what each close spends.
constexpr std::uint32_t SkillCloseRandoms = 1;
constexpr std::uint32_t OwnRecordCloseRandoms = 0xf08;

class PlayerModels final : public Client::Models::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        return model.Value() == Client::Tables::PlayerSkeletonModel ? &player : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
    Client::Models::Skeleton player = Client::Tests::PlayerActionsSkeleton();
};

// What 0x612d90 leaves in Assets::AssetsState::fauna for a monster: only its actions.
class MonsterModels final : public Client::Models::IModelLibrary
{
public:
    MonsterModels()
    {
        monster.actions.resize(MonsterActionCount);
    }
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        return model.Value() == MonsterModel ? &monster : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
    Client::Models::Skeleton monster;
};

Client::Math::BoneTransform BoneAt(const glm::vec3& at)
{
    Client::Math::BoneTransform bone(1.0f);
    bone[3] = at;
    return bone;
}

// The seed with which each target's rand() (after the +0x38e one) comes out even or odd as `evens`, counting the rand()
// of a class-0 player's hit sound (0x4f54d7) when it is even.
std::uint32_t SeedFor(std::initializer_list<bool> evens)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        Client::Crt::CrtRandom random(seed);
        static_cast<void>(random.Next());
        bool matches = true;
        for (const bool even : evens)
        {
            if ((random.Next() % 2 == 0) != even)
            {
                matches = false;
                break;
            }
            if (even)
                static_cast<void>(random.Next());
        }
        if (matches)
            return seed;
    }
}

class MagicAttackCountTest : public ::testing::Test
{
protected:
    MagicAttackCountTest()
    {
        auto& runtime = m_world->Runtime();
        runtime.hero = HeroSlot;
        Caster() = Client::Tests::UnarmedPlayer();
        Caster().alive = true;
        Caster().key = CasterKey;
        Caster().scale = 1.0f;
        Caster().position = {1000.0f, 2000.0f, 50.0f};
        Caster().angles = {0.0f, 0.0f, 90.0f};
        Caster().hands[0].bone = 3;
        Caster().bodyPose = {
            BoneAt({}), BoneAt({}), BoneAt({}), BoneAt({10.0f, 0.0f, 0.0f}), BoneAt({}), BoneAt({0.0f, 20.0f, 0.0f})};
        Hero() = Client::Tests::UnarmedPlayer();
        Hero().alive = true;
        Hero().key = PlayerKey;
        Hero().hands[0].bone = 5;
        Hero().damage = 0x30;
        Hero().position = {300.0f, 400.0f, 10.0f};
        m_world->OwnCharacter().life = 100;
        for (const auto& [slot, key] : {std::pair{FirstSlot, FirstKey}, std::pair{SecondSlot, SecondKey}})
        {
            ActorState& target = m_world->Actors()[slot];
            target = Client::Tests::UnarmedPlayer();
            target.alive = true;
            target.key = key;
            target.position = {static_cast<float>(slot), 7.0f, 8.0f};
        }
    }

    ActorState& Caster()
    {
        return m_world->Actors()[CasterSlot];
    }
    ActorState& Hero()
    {
        return m_world->Actors()[HeroSlot];
    }
    ActorState& Target(std::size_t slot)
    {
        return m_world->Actors()[slot];
    }

    Client::Core::Expected<void, std::string> Apply(std::vector<std::uint16_t> targets,
                                                    std::uint16_t caster = CasterKey)
    {
        const Client::Scenes::WorldEntryResources resources{.world = *m_world,
                                                            .selection = m_selection,
                                                            .dataRoot = {},
                                                            .cloth = m_cloth,
                                                            .camera = m_camera,
                                                            .cameraInput = m_input,
                                                            .models = m_models,
                                                            .log = m_log,
                                                            .states = &m_states};
        return Client::Combat::ApplyMagicAttackCount(
            Client::Protocol::Actors::MagicAttackCount{.key = caster, .magic = Magic, .targets = std::move(targets)},
            Client::Kernel::PlayerKey{PlayerKey}, resources);
    }

    std::vector<const Client::Particles::Particle*> Shines() const
    {
        std::vector<const Client::Particles::Particle*> shines;
        for (const auto& particle : m_world->Particles().All())
            if (particle.alive && particle.type == HandShineParticle)
                shines.push_back(&particle);
        return shines;
    }

    std::vector<const Client::Kernel::DamageNumberState*> Numbers() const
    {
        std::vector<const Client::Kernel::DamageNumberState*> numbers;
        for (const auto& number : m_world->DrawTail().damage)
            if (number.alive)
                numbers.push_back(&number);
        return numbers;
    }

    // Before the states: it lends the models to their fauna and is destroyed after them.
    MonsterModels m_fauna;
    Client::Session::ModuleStates m_states;
    std::unique_ptr<Client::Kernel::WorldState> m_world = std::make_unique<Client::Kernel::WorldState>();
    Client::FrontEnd::SelectionState m_selection{*m_world};
    Client::Camera::GameCamera m_camera;
    Client::Camera::GameCameraInput m_input;
    Client::Models::ClothWorld m_cloth;
    PlayerModels m_models;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
};

// 0x4cedec..0x4cefe4 without targets: [0x5767964] with the slot, the two 0x5fd of 0x53d810 (born at the striker's bone
// +0x2bd, with its angles and the light (1; 0.5; 0.2), and 0x53fdb0 takes them to the HERO's hand bone over the
// striker's bones: Q-C854), sound 0x76, action 0x96, the skill in +0x38e and +0x381 at 1. A single rand(): the close
// of +0x38e.
TEST_F(MagicAttackCountTest, TheCasterRaisesTheShineAndKeepsTheSkill)
{
    const auto seed = m_cloth.random.State();
    ASSERT_TRUE(Apply({}).HasValue());
    EXPECT_EQ(m_world->Runtime().lastAttacker, static_cast<std::int32_t>(CasterSlot));
    const auto shines = Shines();
    ASSERT_EQ(shines.size(), 2u);
    for (std::size_t index = 0; index < shines.size(); ++index)
    {
        const Client::Particles::Particle& shine = *shines[index];
        EXPECT_EQ(shine.subtype, static_cast<std::int32_t>(index * 2));
        EXPECT_EQ(shine.life, 36);
        EXPECT_EQ(shine.start, glm::vec3(1010.0f, 2000.0f, 50.0f));
        EXPECT_EQ(shine.position, glm::vec3(1000.0f, 2020.0f, 50.0f));
        EXPECT_EQ(shine.angles, glm::vec3(45.0f, 0.0f, 90.0f));
        EXPECT_EQ(shine.light, glm::vec3(1.0f, 0.5f, 0.2f));
        EXPECT_EQ(shine.typedOwner,
                  (Client::Particles::ParticleOwner{Client::Particles::ParticleOwnerKind::Actor, CasterSlot}));
    }
    EXPECT_EQ(m_world->Runtime().lastSoundId, Client::Audio::SoundId{0x76});
    EXPECT_EQ(Caster().animation.action, 0x96);
    EXPECT_EQ(Caster().byte38e, Magic);
    EXPECT_EQ(Caster().byte38eAccesses, 0);
    EXPECT_EQ(Caster().byte381, 1);
    Client::Crt::CrtRandom expected(seed);
    expected.Discard(SkillCloseRandoms);
    EXPECT_EQ(m_cloth.random.State(), expected.State());
}

// 0x4cf009..0x4cf084: each target by its key; with an even rand(), 0x4f53a0's hit (a player's action 0xc2 and the
// rand() of its sound); with +0x384, 0x548e40's number at its position with z + 140, orange yellow, of 15 and rising;
// without +0x384, neither hit nor number.
TEST_F(MagicAttackCountTest, EachTargetIsShockedOnAnEvenDrawAndShowsItsDamage)
{
    m_cloth.random = Client::Crt::CrtRandom(SeedFor({true, false}));
    Target(FirstSlot).damage = 0x30;
    Target(SecondSlot).damage = 0x11;
    ASSERT_TRUE(Apply({FirstKey, SecondKey}).HasValue());
    EXPECT_EQ(Target(FirstSlot).animation.action, 0xc2);
    EXPECT_NE(Target(SecondSlot).animation.action, 0xc2);
    const auto numbers = Numbers();
    ASSERT_EQ(numbers.size(), 2u);
    EXPECT_EQ(numbers[0]->number, 0x30);
    EXPECT_EQ(numbers[0]->position, glm::vec3(20.0f, 7.0f, 148.0f));
    EXPECT_EQ(numbers[0]->light, glm::vec3(1.0f, 0.6f, 0.0f));
    EXPECT_EQ(numbers[0]->scale, 15.0f);
    EXPECT_TRUE(numbers[0]->rise);
    EXPECT_EQ(numbers[0]->speed, 10.0f);
    EXPECT_EQ(numbers[1]->number, 0x11);
    EXPECT_EQ(numbers[1]->position, glm::vec3(21.0f, 7.0f, 148.0f));
    EXPECT_EQ(m_world->OwnCharacter().life, 100);

    m_world->DrawTail().damage = {};
    m_cloth.random = Client::Crt::CrtRandom(SeedFor({true}));
    Target(FirstSlot).damage = 0;
    Target(FirstSlot).animation.action = 0;
    ASSERT_TRUE(Apply({FirstKey}).HasValue());
    EXPECT_EQ(Target(FirstSlot).animation.action, 0);
    EXPECT_TRUE(Numbers().empty());
}

// 0x4cf089..0x4cf17d: the target with the player's key lowers the own record's life (open: its 3848 rand() when it
// closes), down to 0 with a bigger hit; the hero is the target by its key, not by being the hero.
TEST_F(MagicAttackCountTest, ThePlayerLosesItsLifeDownToZero)
{
    const std::uint32_t seed = SeedFor({false});
    m_cloth.random = Client::Crt::CrtRandom(seed);
    ASSERT_TRUE(Apply({PlayerKey}).HasValue());
    EXPECT_EQ(m_world->OwnCharacter().life, 100 - 0x30);
    EXPECT_EQ(m_world->Runtime().ownRecordDepth, 0);
    Client::Crt::CrtRandom expected(seed);
    expected.Discard(SkillCloseRandoms + 1 + OwnRecordCloseRandoms);
    EXPECT_EQ(m_cloth.random.State(), expected.State());

    m_world->OwnCharacter().life = 0x20;
    m_cloth.random = Client::Crt::CrtRandom(seed);
    ASSERT_TRUE(Apply({PlayerKey}).HasValue());
    EXPECT_EQ(m_world->OwnCharacter().life, 0);
}

// 0x5110d0 with nobody holding the key: the phantom 400 (Q-C778, Q-C1125) strikes too, in Lorencia and in Devias. The
// shine, the sound, the skill in its +0x38e (with its close's rand()), +0x381 and the targets go as with anyone; action
// 0x96 reads whatever type is in the 400, memory MuRet does not model, and is discarded with a warning, without
// cutting the session.
TEST_F(MagicAttackCountTest, AnUnknownCasterIsTheGhostSlot)
{
    constexpr std::size_t Ghost = 400;
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        m_world->DrawTail().damage = {};
        m_world->Actors()[Ghost] = ActorState{};
        Target(FirstSlot).animation.action = 0;
        Target(FirstSlot).damage = 0x30;
        const std::size_t shinesBefore = Shines().size();
        const std::size_t linesBefore = m_sink.lines.size();
        // The target with an even rand(): 0x4f53a0's hit and its sound's rand().
        const std::uint32_t seed = SeedFor({true});
        m_cloth.random = Client::Crt::CrtRandom(seed);
        const auto applied = Apply({FirstKey}, 0x0999);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const ActorState& ghost = m_world->Actors()[Ghost];
        EXPECT_EQ(m_world->Runtime().lastAttacker, static_cast<std::int32_t>(Ghost));
        EXPECT_EQ(Shines().size(), shinesBefore + 2);
        EXPECT_EQ(ghost.animation.action, 0);
        EXPECT_EQ(ghost.byte38e, Magic);
        EXPECT_EQ(ghost.byte38eAccesses, 0);
        EXPECT_EQ(ghost.byte381, 1);
        EXPECT_EQ(Target(FirstSlot).animation.action, 0xc2);
        ASSERT_EQ(Numbers().size(), 1u);
        EXPECT_EQ(Numbers()[0]->number, 0x30);
        EXPECT_EQ(m_cloth.random.State(),
                  Client::Tests::AfterDraws(seed, static_cast<int>(SkillCloseRandoms) + 2).State());
        ASSERT_EQ(m_sink.lines.size(), linesBefore + 1);
        EXPECT_NE(m_sink.lines.back().find("0x96"), std::string::npos) << m_sink.lines.back();
    }
}

// A striking monster: 0x4ea030 reads the actions of its model, the one its creation loaded into
// Assets::AssetsState::fauna (0x612d90), not Player.bmd. GS 0.97 also sends the 1A for a monster (user.cpp:11887). In
// Lorencia and in Devias.
TEST_F(MagicAttackCountTest, AMonsterCasterTakesTheActionOfItsModel)
{
    m_states.assets.fauna.Bind(&m_fauna);
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        Caster().type = MonsterModel;
        Caster().animation = {};
        Caster().byte381 = 0;
        Target(FirstSlot).damage = 0;
        // The target with an odd rand(): no hit; without +0x384, no number.
        const std::uint32_t seed = SeedFor({false});
        m_cloth.random = Client::Crt::CrtRandom(seed);
        const auto applied = Apply({FirstKey});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Caster().animation.action, 0x96);
        EXPECT_EQ(Caster().byte38e, Magic);
        EXPECT_EQ(Caster().byte381, 1);
        EXPECT_EQ(m_cloth.random.State(),
                  Client::Tests::AfterDraws(seed, static_cast<int>(SkillCloseRandoms) + 1).State());
    }
    // Without the model loaded, its 0x5769ad8 record is at 0 (+0x26 without actions): 0x4ea030 leaves at 0x4ea056
    // without touching the action and the 1A goes on.
    m_states.assets.fauna.Bind(nullptr);
    Caster().animation = {};
    const auto before = Caster().animation.action;
    const auto unloaded = Apply({});
    ASSERT_TRUE(unloaded.HasValue()) << unloaded.Error();
    EXPECT_EQ(Caster().animation.action, before);
    EXPECT_EQ(Caster().byte38e, Magic);
}

// The particle step asks for the hand with ParticleActorHandsAmong: the hero's over the owner's bones.
TEST_F(MagicAttackCountTest, TheStepFindsTheHandOfTheOwner)
{
    const auto hands = Client::Effects::ParticleActorHandsAmong(m_world->Actors(), HeroSlot);
    EXPECT_EQ(hands(CasterSlot, 0), std::optional(glm::vec3(1000.0f, 2020.0f, 50.0f)));
    EXPECT_EQ(hands(CasterSlot, 2), std::optional(glm::vec3(1000.0f, 2020.0f, 50.0f)));
    EXPECT_EQ(hands(401, 0), std::nullopt);
}
} // namespace
