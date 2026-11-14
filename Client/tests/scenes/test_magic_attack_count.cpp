#include "Scenes/MagicAttackCount.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Protocol/Viewport.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HandShine.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"

#include "support/PlayerActors.h"
#include "support/RecordingSink.h"

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

// Scenes::ApplyMagicAttackCount: el 1A, la skill que pega a varios (0x4cedd0). Las claves, las celdas y la vida son
// inventadas; lo esperado sale del ASM (0x4cedd0, 0x53d810, 0x53fdb0, 0x548e40).
namespace
{
using Client::Scenes::ActorState;
using Client::World::HandShineParticle;

constexpr std::uint16_t CasterKey = 0x1234;
constexpr std::size_t CasterSlot = 12;
constexpr std::uint16_t PlayerKey = 0x0777;
constexpr std::size_t HeroSlot = 30;
constexpr std::uint16_t FirstKey = 0x0101;
constexpr std::size_t FirstSlot = 20;
constexpr std::uint16_t SecondKey = 0x0102;
constexpr std::size_t SecondSlot = 21;
constexpr std::uint8_t Magic = 0x29;
// Un modelo de monstruo (0x152 + 1, Monster02.bmd), con las acciones hasta la 0x96.
constexpr std::uint16_t MonsterModel = 0x153;
constexpr std::size_t MonsterActionCount = 0x97;
// 0x40b3f0 y 0x40b4d0: lo que gasta cada cierre.
constexpr std::uint32_t SkillCloseRandoms = 1;
constexpr std::uint32_t OwnRecordCloseRandoms = 0xf08;

class PlayerModels final : public Client::Scenes::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == Client::Data::Models::PlayerSkeletonModel ? &player : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    Client::Render::Models::Skeleton player = Client::Tests::PlayerActionsSkeleton();
};

// Lo que 0x612d90 deja en WorldState::FaunaResources para un monstruo: solo sus acciones.
class MonsterModels final : public Client::Scenes::IModelLibrary
{
public:
    MonsterModels()
    {
        monster.actions.resize(MonsterActionCount);
    }
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == MonsterModel ? &monster : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    Client::Render::Models::Skeleton monster;
};

// Un hueso que solo traslada a `at`.
Client::Render::Models::BoneTransform BoneAt(const glm::vec3& at)
{
    Client::Render::Models::BoneTransform bone(1.0f);
    bone[3] = at;
    return bone;
}

// La semilla con la que el rand() de cada blanco (después del de +0x38e) sale par o impar como `evens`, con el rand()
// del sonido del golpe de un jugador de clase 0 (0x4f54d7) cuando sale par.
std::uint32_t SeedFor(std::initializer_list<bool> evens)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        Client::Render::Models::CrtRandom random(seed);
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
                                                            .log = m_log};
        return Client::Scenes::ApplyMagicAttackCount(
            Client::Protocol::Viewport::MagicAttackCount{.key = caster, .magic = Magic, .targets = std::move(targets)},
            PlayerKey, resources);
    }

    std::vector<const Client::World::Particle*> Shines() const
    {
        std::vector<const Client::World::Particle*> shines;
        for (const auto& particle : m_world->Particles().All())
            if (particle.alive && particle.type == HandShineParticle)
                shines.push_back(&particle);
        return shines;
    }

    std::vector<const Client::Scenes::DamageNumberState*> Numbers() const
    {
        std::vector<const Client::Scenes::DamageNumberState*> numbers;
        for (const auto& number : m_world->DrawTail().damage)
            if (number.alive)
                numbers.push_back(&number);
        return numbers;
    }

    // Antes del mundo: su FaunaResources la presta y se destruye después.
    MonsterModels m_fauna;
    std::unique_ptr<Client::Scenes::WorldState> m_world = std::make_unique<Client::Scenes::WorldState>();
    Client::Scenes::SelectionState m_selection{*m_world};
    Client::World::GameCamera m_camera;
    Client::World::GameCameraInput m_input;
    Client::Render::Models::ClothWorld m_cloth;
    PlayerModels m_models;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
};

// 0x4cedec..0x4cefe4 sin blancos: [0x5767964] con el lugar, las dos 5fd de 0x53d810 (nacen en el hueso +0x2bd del que
// pega, con sus ángulos y la luz (1; 0,5; 0,2), y 0x53fdb0 las lleva al hueso de la mano del HÉROE sobre los huesos del
// que pega: Q-C854), el sonido 0x76, la acción 0x96, la skill en +0x38e y +0x381 en 1. Un solo rand(): el cierre de
// +0x38e.
TEST_F(MagicAttackCountTest, TheCasterRaisesTheShineAndKeepsTheSkill)
{
    const auto seed = m_cloth.random.State();
    ASSERT_TRUE(Apply({}).HasValue());
    EXPECT_EQ(m_world->Runtime().lastAttacker, static_cast<std::int32_t>(CasterSlot));
    const auto shines = Shines();
    ASSERT_EQ(shines.size(), 2u);
    for (std::size_t index = 0; index < shines.size(); ++index)
    {
        const Client::World::Particle& shine = *shines[index];
        EXPECT_EQ(shine.subtype, static_cast<std::int32_t>(index * 2));
        EXPECT_EQ(shine.life, 36);
        EXPECT_EQ(shine.start, glm::vec3(1010.0f, 2000.0f, 50.0f));
        EXPECT_EQ(shine.position, glm::vec3(1000.0f, 2020.0f, 50.0f));
        EXPECT_EQ(shine.angles, glm::vec3(45.0f, 0.0f, 90.0f));
        EXPECT_EQ(shine.light, glm::vec3(1.0f, 0.5f, 0.2f));
        EXPECT_EQ(shine.typedOwner,
                  (Client::World::ParticleOwner{Client::World::ParticleOwnerKind::Actor, CasterSlot}));
    }
    EXPECT_EQ(m_world->Runtime().lastSoundId, std::optional<std::int32_t>{0x76});
    EXPECT_EQ(Caster().animation.action, 0x96);
    EXPECT_EQ(Caster().byte38e, Magic);
    EXPECT_EQ(Caster().byte38eAccesses, 0);
    EXPECT_EQ(Caster().byte381, 1);
    Client::Render::Models::CrtRandom expected(seed);
    expected.Discard(SkillCloseRandoms);
    EXPECT_EQ(m_cloth.random.State(), expected.State());
}

// 0x4cf009..0x4cf084: cada blanco por su clave; con rand() par, el golpe de 0x4f53a0 (la acción 0xc2 de un jugador y
// el rand() de su sonido); con +0x384, el número de 0x548e40 en su posición con z + 140, amarillo naranja, de 15 y que
// sube; sin +0x384, ni golpe ni número.
TEST_F(MagicAttackCountTest, EachTargetIsShockedOnAnEvenDrawAndShowsItsDamage)
{
    m_cloth.random = Client::Render::Models::CrtRandom(SeedFor({true, false}));
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
    m_cloth.random = Client::Render::Models::CrtRandom(SeedFor({true}));
    Target(FirstSlot).damage = 0;
    Target(FirstSlot).animation.action = 0;
    ASSERT_TRUE(Apply({FirstKey}).HasValue());
    EXPECT_EQ(Target(FirstSlot).animation.action, 0);
    EXPECT_TRUE(Numbers().empty());
}

// 0x4cf089..0x4cf17d: el blanco con la clave del jugador baja la vida del registro propio (abierto: sus 3.848 rand()
// al cerrarse), hasta 0 con un golpe mayor; el héroe es el blanco por su clave, no por ser el héroe.
TEST_F(MagicAttackCountTest, ThePlayerLosesItsLifeDownToZero)
{
    const std::uint32_t seed = SeedFor({false});
    m_cloth.random = Client::Render::Models::CrtRandom(seed);
    ASSERT_TRUE(Apply({PlayerKey}).HasValue());
    EXPECT_EQ(m_world->OwnCharacter().life, 100 - 0x30);
    EXPECT_EQ(m_world->Runtime().ownRecordDepth, 0);
    Client::Render::Models::CrtRandom expected(seed);
    expected.Discard(SkillCloseRandoms + 1 + OwnRecordCloseRandoms);
    EXPECT_EQ(m_cloth.random.State(), expected.State());

    m_world->OwnCharacter().life = 0x20;
    m_cloth.random = Client::Render::Models::CrtRandom(seed);
    ASSERT_TRUE(Apply({PlayerKey}).HasValue());
    EXPECT_EQ(m_world->OwnCharacter().life, 0);
}

// 0x5110d0 sin nadie con la clave: el 400 fantasma (Q-C778, Q-C1125) también pega, en Lorencia y en Devias. El brillo,
// el sonido, la skill en su +0x38e (con el rand() de su cierre), +0x381 y los blancos van como con cualquiera; la
// acción 0x96 lee el tipo que haya en el 400, memoria que MuRet no modela, y se descarta con un aviso, sin cortar la
// sesión.
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
        // El blanco con rand() par: el golpe de 0x4f53a0 y el rand() de su sonido.
        const std::uint32_t seed = SeedFor({true});
        m_cloth.random = Client::Render::Models::CrtRandom(seed);
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

// Un monstruo que pega: 0x4ea030 lee las acciones de su modelo, el que cargó su alta en WorldState::FaunaResources
// (0x612d90), no Player.bmd. El GS 0.97 manda el 1A también por un monstruo (user.cpp:11887). En Lorencia y en Devias.
TEST_F(MagicAttackCountTest, AMonsterCasterTakesTheActionOfItsModel)
{
    m_world->FaunaResources().Bind(&m_fauna);
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        Caster().type = MonsterModel;
        Caster().animation = {};
        Caster().byte381 = 0;
        Target(FirstSlot).damage = 0;
        // El blanco con rand() impar: sin golpe; sin +0x384, sin número.
        const std::uint32_t seed = SeedFor({false});
        m_cloth.random = Client::Render::Models::CrtRandom(seed);
        const auto applied = Apply({FirstKey});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Caster().animation.action, 0x96);
        EXPECT_EQ(Caster().byte38e, Magic);
        EXPECT_EQ(Caster().byte381, 1);
        EXPECT_EQ(m_cloth.random.State(),
                  Client::Tests::AfterDraws(seed, static_cast<int>(SkillCloseRandoms) + 1).State());
    }
    // Sin el modelo cargado, su registro de 0x5769ad8 está en 0 (+0x26 sin acciones): 0x4ea030 sale en 0x4ea056 sin
    // tocar la acción (revisión B del hito 2, B3) y el 1A sigue.
    m_world->FaunaResources().Bind(nullptr);
    Caster().animation = {};
    const auto before = Caster().animation.action;
    const auto unloaded = Apply({});
    ASSERT_TRUE(unloaded.HasValue()) << unloaded.Error();
    EXPECT_EQ(Caster().animation.action, before);
    EXPECT_EQ(Caster().byte38e, Magic);
}

// El paso de las partículas pide la mano con ParticleActorHandsAmong: la del héroe sobre los huesos del dueño.
TEST_F(MagicAttackCountTest, TheStepFindsTheHandOfTheOwner)
{
    const auto hands = Client::Scenes::ParticleActorHandsAmong(m_world->Actors(), HeroSlot);
    EXPECT_EQ(hands(CasterSlot, 0), std::optional(glm::vec3(1000.0f, 2020.0f, 50.0f)));
    EXPECT_EQ(hands(CasterSlot, 2), std::optional(glm::vec3(1000.0f, 2020.0f, 50.0f)));
    EXPECT_EQ(hands(401, 0), std::nullopt);
}
} // namespace
