#include "Scenes/ModelEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GroundMarks.h"
#include "World/OriginalFtol.h"
#include "World/Particles.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

// Los casos de 0x505de0 y los helpers de 0x5085db de un personaje que no es el 0x20a (Scenes::StepTheModelEffects):
// lo que escriben, sus rand() en orden, sus partículas, joints, sprites y luces, y dónde cortan.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Tests::AfterDraws;
using Client::Tests::FirstSeedWhere;

constexpr std::uint32_t Seed = 0x1357u;
constexpr float Clock = 12345.0f;

class ModelEffectsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_skeleton.bones.assign(50, Client::Render::Models::SkeletonBone{.dummy = true, .parent = -1, .actions = {}});
        m_skeleton.actions.assign(12, Client::Render::Models::ActionInfo{
                                          .repeats = true, .speed = 0.25f, .keyCount = 10, .lockPositions = false});
        m_actor.alive = true;
        m_actor.scale = 1.0f;
        m_actor.position = {1000.0f, 2000.0f, 50.0f};
        m_actor.light = {0.5f, 0.25f, 0.125f};
    }

    Client::Core::Expected<void, std::string> Step(std::uint16_t type, std::int32_t map, CrtRandom& random)
    {
        m_actor.type = type;
        Client::Scenes::ModelEffectsPass pass{.actor = m_actor,
                                              .slot = 3,
                                              .world = *m_world,
                                              .random = random,
                                              .skeleton = m_skeleton,
                                              .particles = &m_world->Particles(),
                                              .sprites = &m_sprites,
                                              .worldClock = Clock,
                                              .map = map};
        return Client::Scenes::StepTheModelEffects(pass);
    }

    int Alive(std::int32_t type, std::int32_t subtype)
    {
        int count = 0;
        for (const auto& particle : m_world->Particles().All())
            count += particle.alive && particle.type == type && particle.subtype == subtype ? 1 : 0;
        return count;
    }

    int Joints(std::int32_t type)
    {
        int count = 0;
        for (const auto& joint : m_world->DrawTail().joints)
            count += joint.alive && joint.type == type ? 1 : 0;
        return count;
    }

    void Clear()
    {
        for (auto& particle : m_world->Particles().All())
            particle.alive = false;
        for (auto& joint : m_world->DrawTail().joints)
            joint.alive = false;
        m_sprites.Clear();
        m_world->Runtime().lastSoundId.reset();
    }

    static float Brightness(std::uint32_t seed)
    {
        CrtRandom random(seed);
        return static_cast<float>(static_cast<double>(random.Next() % 8 + 2) * static_cast<double>(0.1f));
    }

    static bool CutAt(const Client::Core::Expected<void, std::string>& result, const char* address)
    {
        return !result.HasValue() && result.Error().find(address) != std::string::npos;
    }

    std::unique_ptr<Client::Scenes::WorldState> m_world = std::make_unique<Client::Scenes::WorldState>();
    Client::World::FrameSprites m_sprites;
    Client::Render::Models::Skeleton m_skeleton;
    ActorState m_actor;
};

// 0x506cac: el & 7 sin condición; un modelo sin caso ni helper no saca más.
TEST_F(ModelEffectsTest, AModelWithoutEffectsOnlyDrawsTheBrightness)
{
    for (const std::int32_t map : {0, 2, 30})
    {
        SCOPED_TRACE(map);
        for (const std::uint16_t type : {std::uint16_t{0x153}, std::uint16_t{0x183}, std::uint16_t{0x1ac}})
        {
            CrtRandom random(Seed);
            ASSERT_TRUE(Step(type, map, random).HasValue());
            EXPECT_EQ(random.State(), AfterDraws(Seed, 1).State());
        }
    }
}

// 0x5073c6, 0x5073ed, 0x507ab7, 0x507ade y 0x507e63: la U o la V del reloj, sin más rand().
TEST_F(ModelEffectsTest, TheScrollingModelsWriteTheirTextureOffsets)
{
    const std::int32_t clock = *Client::World::OriginalFtolLow(static_cast<double>(Clock));
    const auto scroll = [clock](std::int32_t modulus, std::uint32_t bits) {
        return static_cast<float>(static_cast<double>(clock % modulus) *
                                  static_cast<double>(std::bit_cast<float>(bits)));
    };
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        CrtRandom random(Seed);
        ASSERT_TRUE(Step(0x186, map, random).HasValue());
        EXPECT_EQ(m_actor.textureU, scroll(10000, 0xb9d1b717u));
        ASSERT_TRUE(Step(0x189, map, random).HasValue());
        EXPECT_EQ(m_actor.textureV, scroll(10000, 0x38d1b717u));
        ASSERT_TRUE(Step(0x16c, map, random).HasValue());
        EXPECT_EQ(m_actor.textureU, scroll(10000, 0xb8d1b717u));
        ASSERT_TRUE(Step(0x16d, map, random).HasValue());
        EXPECT_EQ(m_actor.textureV, scroll(1000, 0xba83126fu));
        ASSERT_TRUE(Step(0x161, map, random).HasValue());
        EXPECT_EQ(m_actor.textureV, scroll(2000, 0xba03126fu));
        EXPECT_EQ(random.State(), AfterDraws(Seed, 5).State());
    }
}

// 0x5084b3: el 0x152 (Monster01, de Lorencia) solo en sus ventanas de frame, con rand() % 2 en 0, el 0x5f2/0.
TEST_F(ModelEffectsTest, TheBullBreathesOnlyInItsFrameWindows)
{
    const std::uint32_t breath = FirstSeedWhere(
        [](CrtRandom& random)
        {
            static_cast<void>(random.Next());
            return random.Next() % 2 == 0;
        });
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        for (const auto& [action, frame, breathes] :
             {std::tuple{0, 15.0f, true}, std::tuple{0, 14.9f, false}, std::tuple{1, 25.0f, true},
              std::tuple{1, 19.0f, false}, std::tuple{2, 2.5f, true}, std::tuple{2, 4.0f, false},
              std::tuple{2, 6.0f, true}, std::tuple{3, 16.0f, false}})
        {
            SCOPED_TRACE(frame);
            Clear();
            m_actor.animation.action = static_cast<std::uint8_t>(action);
            m_actor.animation.frame = frame;
            CrtRandom random(breath);
            ASSERT_TRUE(Step(0x152, map, random).HasValue());
            EXPECT_EQ(Alive(Client::World::BreathParticle, 0), breathes ? 1 : 0);
            if (!breathes)
            {
                EXPECT_EQ(random.State(), AfterDraws(breath, 1).State());
            }
        }
    }
}

// 0x5079c0: el 0x177 sube +0x68 en la 3 y la 4 con tope 1 y lo baja en las demás con piso 0.
TEST_F(ModelEffectsTest, TheGhostFadesInWhileItAttacks)
{
    for (const std::int32_t map : {0, 2})
    {
        CrtRandom random(Seed);
        m_actor.animation.action = 3;
        m_actor.blendMeshLight = 0.5f;
        ASSERT_TRUE(Step(0x177, map, random).HasValue());
        EXPECT_EQ(m_actor.blendMeshLight, static_cast<float>(0.5 + static_cast<double>(0.1f)));
        m_actor.blendMeshLight = 0.95f;
        ASSERT_TRUE(Step(0x177, map, random).HasValue());
        EXPECT_EQ(m_actor.blendMeshLight, 1.0f);
        m_actor.animation.action = 0;
        m_actor.blendMeshLight = 0.05f;
        ASSERT_TRUE(Step(0x177, map, random).HasValue());
        EXPECT_EQ(m_actor.blendMeshLight, 0.0f);
        EXPECT_EQ(random.State(), AfterDraws(Seed, 3).State());
    }
}

// 0x507e8a y 0x507eb7: rand() & 63 o & 255 en 0 pide el sonido (CL10).
TEST_F(ModelEffectsTest, TheTownSoundsComeFromTheirOwnRandom)
{
    const std::uint32_t lucky = FirstSeedWhere(
        [](CrtRandom& random)
        {
            static_cast<void>(random.Next());
            return random.Next() % 256 == 0;
        });
    for (const std::int32_t map : {0, 2})
    {
        Clear();
        CrtRandom random(lucky);
        ASSERT_TRUE(Step(0x1d8, map, random).HasValue());
        EXPECT_EQ(m_world->Runtime().lastSoundId, std::optional<std::int32_t>{0xa1});
        Clear();
        CrtRandom other(lucky);
        ASSERT_TRUE(Step(0x1d3, map, other).HasValue());
        EXPECT_EQ(m_world->Runtime().lastSoundId, std::optional<std::int32_t>{0xa0});
        EXPECT_EQ(other.State(), AfterDraws(lucky, 2).State());
    }
}

// 0x507dab: el 0x15d con el nivel 2, diez 0x5d9/0 en huesos de rand(); con otro nivel, solo +0x68.
TEST_F(ModelEffectsTest, TheSecondLevelBurnsTenTimes)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Clear();
        m_actor.level = 0;
        CrtRandom random(Seed);
        ASSERT_TRUE(Step(0x15d, map, random).HasValue());
        CrtRandom drawn(Seed);
        static_cast<void>(drawn.Next());
        EXPECT_EQ(m_actor.blendMeshLight,
                  static_cast<float>(static_cast<double>(drawn.Next() % 10) * static_cast<double>(0.1f)));
        EXPECT_EQ(random.State(), AfterDraws(Seed, 2).State());
        m_actor.level = 2;
        ASSERT_TRUE(Step(0x15d, map, random).HasValue());
        EXPECT_EQ(Alive(Client::World::FireParticle, 0), 10);
    }
}

// 0x507b05: el 0x172 en la 3, el 0x5ca/0 y el 0x5d9/0 del hueso 33; en la 6 antes del frame 12, veinte 0x5d9/0.
TEST_F(ModelEffectsTest, TheWizardSparksByItsAction)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Clear();
        m_actor.animation.action = 3;
        CrtRandom random(Seed);
        ASSERT_TRUE(Step(0x172, map, random).HasValue());
        EXPECT_EQ(Alive(Client::World::ThunderParticle, 0), 1);
        EXPECT_EQ(Alive(Client::World::FireParticle, 0), 1);
        Clear();
        m_actor.animation.action = 6;
        m_actor.animation.frame = 11.5f;
        ASSERT_TRUE(Step(0x172, map, random).HasValue());
        EXPECT_EQ(Alive(Client::World::FireParticle, 0), 20);
        Clear();
        m_actor.animation.frame = 12.0f;
        CrtRandom late(Seed);
        ASSERT_TRUE(Step(0x172, map, late).HasValue());
        EXPECT_EQ(late.State(), AfterDraws(Seed, 1).State());
    }
}

// 0x507ee6: el 0x1ce en la 0 con el frame en 5, el sonido 0xa0, +0x68 en s, +0x64 en 4 y cuatro joints 0x617/0 con su
// 0x5c5/0; con +0x7c & 0x10000000, nada.
TEST_F(ModelEffectsTest, TheFountainSparksAtTheStartOfItsRest)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Clear();
        m_actor.animation.action = 0;
        m_actor.animation.frame = 5.0f;
        m_actor.flags7c = 0;
        CrtRandom random(Seed);
        ASSERT_TRUE(Step(0x1ce, map, random).HasValue());
        EXPECT_EQ(m_world->Runtime().lastSoundId, std::optional<std::int32_t>{0xa0});
        EXPECT_EQ(m_actor.blendMeshLight, Brightness(Seed));
        EXPECT_EQ(m_actor.dword64, 4);
        EXPECT_EQ(Joints(0x617), 4);
        EXPECT_EQ(Alive(Client::World::FairySparkParticle, 0), 4);
        Clear();
        m_actor.flags7c = 0x10000000u;
        m_actor.dword64 = 0;
        CrtRandom quiet(Seed);
        ASSERT_TRUE(Step(0x1ce, map, quiet).HasValue());
        EXPECT_EQ(m_actor.dword64, 0);
        EXPECT_EQ(quiet.State(), AfterDraws(Seed, 1).State());
    }
}

// 0x508093: el 0x1f5 en la 0, cuatro joints 0x617/0; fuera de la 0, nada.
TEST_F(ModelEffectsTest, TheTraderSparksWhileItRests)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Clear();
        m_actor.animation.action = 0;
        CrtRandom random(Seed);
        ASSERT_TRUE(Step(0x1f5, map, random).HasValue());
        EXPECT_EQ(Joints(0x617), 4);
        Clear();
        m_actor.animation.action = 1;
        CrtRandom other(Seed);
        ASSERT_TRUE(Step(0x1f5, map, other).HasValue());
        EXPECT_EQ(Joints(0x617), 0);
        EXPECT_EQ(other.State(), AfterDraws(Seed, 1).State());
    }
}

// 0x50826d: el 0x155 con rand() % 4 en 0, el 0x5f2/0 en Devias y el 0x5f3/0 en los demás; el 0x166 deja además su
// sprite 0x5ac; muriéndose, nada.
TEST_F(ModelEffectsTest, TheWalkersKickDustByTheirMap)
{
    const std::uint32_t kick = FirstSeedWhere(
        [](CrtRandom& random)
        {
            static_cast<void>(random.Next());
            return random.Next() % 4 == 0;
        });
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        Clear();
        m_actor.dying = 0;
        CrtRandom random(kick);
        ASSERT_TRUE(Step(0x155, map, random).HasValue());
        EXPECT_EQ(Alive(Client::World::BreathParticle, 0), map == 2 ? 1 : 0);
        EXPECT_EQ(Alive(Client::World::DustParticle, 0), map == 2 ? 0 : 1);
        Clear();
        CrtRandom glowing(kick);
        ASSERT_TRUE(Step(0x166, map, glowing).HasValue());
        EXPECT_TRUE(m_sprites.Any());
        Clear();
        m_actor.dying = 1;
        CrtRandom dying(kick);
        ASSERT_TRUE(Step(0x158, map, dying).HasValue());
        EXPECT_EQ(dying.State(), AfterDraws(kick, 1).State());
    }
    m_actor.dying = 0;
}

// 0x505c50: el 0x157 en la 6 con el frame en [8, 9) pone la luz propia en 1 y, después de sus tres rand(), corta en el
// 0x5f3/1, que el pool no conoce.
TEST_F(ModelEffectsTest, TheDyingBurstCutsAtItsUnknownDust)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_actor.animation.action = 6;
        m_actor.animation.frame = 8.0f;
        m_actor.light = {};
        CrtRandom random(Seed);
        EXPECT_TRUE(CutAt(Step(0x157, map, random), "0x505d2c"));
        EXPECT_EQ(m_actor.light, glm::vec3(1.0f));
        EXPECT_EQ(random.State(), AfterDraws(Seed, 4).State());
        m_actor.animation.frame = 9.0f;
        CrtRandom late(Seed);
        EXPECT_TRUE(Step(0x157, map, late).HasValue());
    }
}

// 0x507006 y 0x50749c: el 0x185 corta en su primer joint 0x614/7 y el 0x17e en su primer 0x5de, después del & 7.
TEST_F(ModelEffectsTest, TheUnknownJointsAndParticlesCut)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        CrtRandom random(Seed);
        EXPECT_TRUE(CutAt(Step(0x185, map, random), "0x50706f"));
        EXPECT_EQ(random.State(), AfterDraws(Seed, 1).State());
        m_actor.word376 = 0x3f;
        CrtRandom other(Seed);
        EXPECT_TRUE(CutAt(Step(0x17e, map, other), "0x50758a"));
        m_actor.word376 = 0;
        EXPECT_TRUE(CutAt(Step(0x17e, map, other), "0x50780d"));
    }
}

// 0x507414: el 0x182 con rand() % 5 en 0 deja la marca 0x5e3 del suelo.
TEST_F(ModelEffectsTest, TheBleedingMonsterMarksTheGround)
{
    const std::uint32_t mark = FirstSeedWhere(
        [](CrtRandom& random)
        {
            static_cast<void>(random.Next());
            return random.Next() % 5 == 0;
        });
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().groundMarks = {};
        CrtRandom random(mark);
        ASSERT_TRUE(Step(0x182, map, random).HasValue());
        EXPECT_TRUE(m_world->Runtime().groundMarks[0].live);
        EXPECT_EQ(m_world->Runtime().groundMarks[0].type, 0x5e3);
    }
}

// 0x43bdd0: el 0x191 y el 0x196 laten +0x68; del 0x193 al 0x195, nada; el 0xc5 fuera de la 0 pone +0x64 en 1; en la
// 0, después de sus dos 0x5f3/0, corta en el efecto 0xc8.
TEST_F(ModelEffectsTest, TheKalimaHelperPulsesAndCutsAtItsEffect)
{
    const auto pulse = Client::Render::Models::Detail::SineProductPC53(
        static_cast<double>(Clock) * static_cast<double>(std::bit_cast<float>(0x3a83126fu)),
        std::bit_cast<float>(0x3f333333u));
    ASSERT_TRUE(pulse.has_value());
    for (const std::int32_t map : {0, 2, 24})
    {
        SCOPED_TRACE(map);
        CrtRandom random(Seed);
        ASSERT_TRUE(Step(0x191, map, random).HasValue());
        EXPECT_EQ(m_actor.blendMeshLight,
                  static_cast<float>(*pulse + static_cast<double>(std::bit_cast<float>(0x3e99999au))));
        ASSERT_TRUE(Step(0x194, map, random).HasValue());
        EXPECT_EQ(random.State(), AfterDraws(Seed, 2).State());
        m_actor.animation.action = 1;
        m_actor.dword64 = 0;
        ASSERT_TRUE(Step(0xc5, map, random).HasValue());
        EXPECT_EQ(m_actor.dword64, 1);
        Clear();
        m_actor.animation.action = 0;
        CrtRandom gate(Seed);
        EXPECT_TRUE(CutAt(Step(0xc5, map, gate), "0x43bea5"));
        EXPECT_EQ(Alive(Client::World::DustParticle, 0), 2);
    }
}

// 0x42de90: en el mapa 30 el 0x19b deja el ángulo z en 0, el 0x1a1 en 45 y el 0x1fc +0x58 en −2 (o −1 con el asedio y
// 0x20000); en los otros mapas, nada.
TEST_F(ModelEffectsTest, TheCastleHelperOnlyActsInTheCastle)
{
    for (const std::int32_t map : {0, 2, 30})
    {
        SCOPED_TRACE(map);
        const bool castle = map == 30;
        m_actor.angles = {1.0f, 2.0f, 3.0f};
        CrtRandom random(Seed);
        ASSERT_TRUE(Step(0x19b, map, random).HasValue());
        EXPECT_EQ(m_actor.angles.z, castle ? 0.0f : 3.0f);
        ASSERT_TRUE(Step(0x1a1, map, random).HasValue());
        EXPECT_EQ(m_actor.angles.z, castle ? 45.0f : 3.0f);
        ASSERT_TRUE(Step(0x19c, map, random).HasValue());
        m_actor.dword58 = 7;
        ASSERT_TRUE(Step(0x1fc, map, random).HasValue());
        EXPECT_EQ(m_actor.dword58, castle ? -2 : 7);
        m_world->Runtime().siegeActive = true;
        m_world->Runtime().castleFlags7239e4 = 0x20000;
        ASSERT_TRUE(Step(0x1fc, map, random).HasValue());
        EXPECT_EQ(m_actor.dword58, castle ? -1 : 7);
        m_world->Runtime().siegeActive = false;
        EXPECT_EQ(random.State(), AfterDraws(Seed, 5).State());
    }
}

// 0x42f550 y 0x431470: el 0x1b2 saca su rand() % 360 y corta en los huesos por nombre (en el 35, en el primero); el
// 0x1b1 en la 6 no hace nada; el 0x1ad con rand() % 10 en 0 levanta su polvo; el 0x1b4 quieto pose sin cortar y en la
// 2 su polvo lee una luz que no escribe.
TEST_F(ModelEffectsTest, TheCrywolfHelpersCutOnlyAtTheirEffects)
{
    const std::uint32_t dust = FirstSeedWhere(
        [](CrtRandom& random)
        {
            static_cast<void>(random.Next());
            return random.Next() % 10 == 0;
        });
    for (const std::int32_t map : {0, 2, 34, 35})
    {
        SCOPED_TRACE(map);
        CrtRandom random(Seed);
        EXPECT_TRUE(CutAt(Step(0x1b2, map, random), map == 35 ? "0x42f6ce" : "0x431fa4"));
        EXPECT_EQ(random.State(), AfterDraws(Seed, 2).State());
        m_actor.animation.action = 6;
        CrtRandom quiet(Seed);
        ASSERT_TRUE(Step(0x1b1, map, quiet).HasValue());
        EXPECT_EQ(quiet.State(), AfterDraws(Seed, 1).State());
        Clear();
        m_actor.animation.action = 2;
        CrtRandom kicked(dust);
        ASSERT_TRUE(Step(0x1ad, map, kicked).HasValue());
        EXPECT_EQ(Alive(Client::World::DustParticle, 0), 1);
        m_actor.animation.action = 0;
        m_actor.animation.frame = 3.0f;
        CrtRandom posed(Seed);
        const auto stepped = Step(0x1b4, map, posed);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(posed.State(), AfterDraws(Seed, 1).State());
        m_actor.animation.action = 2;
        CrtRandom undefined(dust);
        EXPECT_TRUE(CutAt(Step(0x1b4, map, undefined), "0x431e86"));
    }
}

// 0x431470 con el 0x1b5: con la etapa de Crywolf en 4 y rand() % 100 en 0, la acción 3 y +0x84 en 1; sin la etapa,
// nada.
TEST_F(ModelEffectsTest, TheCrywolfStageCallsTheBalgass)
{
    const std::uint32_t call = FirstSeedWhere(
        [](CrtRandom& random)
        {
            static_cast<void>(random.Next());
            return random.Next() % 100 == 0;
        });
    for (const std::int32_t map : {0, 2, 34})
    {
        SCOPED_TRACE(map);
        m_actor.animation = {};
        m_actor.movementPhase = 0.0f;
        m_world->Runtime().crywolf.stage = 0;
        CrtRandom random(call);
        ASSERT_TRUE(Step(0x1b5, map, random).HasValue());
        EXPECT_EQ(random.State(), AfterDraws(call, 1).State());
        m_world->Runtime().crywolf.stage = 4;
        CrtRandom staged(call);
        ASSERT_TRUE(Step(0x1b5, map, staged).HasValue());
        EXPECT_EQ(m_actor.animation.action, 3);
        EXPECT_EQ(m_actor.movementPhase, 1.0f);
    }
    m_world->Runtime().crywolf.stage = 0;
}

// 0x441210 y 0x426ce0: las luces del 0x1a4 y del 0x1bb, sin rand().
TEST_F(ModelEffectsTest, TheGlowingModelsOnlyLight)
{
    for (const std::int32_t map : {0, 2})
    {
        CrtRandom random(Seed);
        ASSERT_TRUE(Step(0x1a4, map, random).HasValue());
        ASSERT_TRUE(Step(0x1bb, map, random).HasValue());
        EXPECT_EQ(random.State(), AfterDraws(Seed, 2).State());
    }
}
} // namespace
