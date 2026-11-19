#include "Scenes/ModelEffects.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/DynamicWorldLight.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "World/GroundMarks.h"
#include "World/ModelEffects.h"
#include "World/OriginalFtol.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"

#include <glm/ext/vector_float3.hpp>

#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

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
                                              .worldClock = m_clock,
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

    // Los 88 huesos de Monster45 (el modelo 0x17e) en la identidad, cada uno corrido a BoneOffset: el punto de
    // 0x4ec110 sin offset es la posición más eso.
    void PoseEveryBone()
    {
        constexpr std::size_t MonsterBones = 88;
        m_actor.bodyPose.assign(MonsterBones, Client::Render::Models::BoneTransform(1.0f));
        for (std::size_t bone = 0; bone < m_actor.bodyPose.size(); ++bone)
            m_actor.bodyPose[bone][3] = BoneOffset(bone);
    }

    static glm::vec3 BoneOffset(std::size_t bone)
    {
        const auto at = static_cast<float>(bone);
        return {2.0f * at, 90.0f - at, static_cast<float>(bone % 7) * 3.0f};
    }

    glm::vec3 BoneAt(std::size_t bone) const
    {
        return m_actor.position + BoneOffset(bone);
    }

    // El rojo de la luz dinámica en la celda del personaje (x · 0,01; y · 0,01); 0x5f9780 no la deja bajar de 0.
    float& RedAtTheActor()
    {
        const auto x = static_cast<std::uint8_t>(m_actor.position.x / 100.0f);
        const auto y = static_cast<std::uint8_t>(m_actor.position.y / 100.0f);
        return m_world->DynamicLighting().Lighting().cells[Client::Data::Terrain::CellIndex(x, y)][0];
    }

    // 0x53fe70(0x5de, …) en un pool gemelo, con el reloj `clock` y el rand() de `replay`.
    static void ReferenceFlame(Client::World::Particles& reference, CrtRandom& replay, const float& clock,
                               std::int32_t subtype, const glm::vec3& point, const glm::vec3& angles,
                               const glm::vec3& light, float scale)
    {
        Client::World::PortableParticleTrigonometry trigonometry;
        const auto made = reference.CreateChecked(0x5de, point, angles, light, subtype, scale, std::cref(clock), replay,
                                                  0, trigonometry);
        ASSERT_TRUE(made.HasValue());
    }

    // Deja libres sólo los primeros `free` lugares de `pool`; los demás, ocupados por una partícula sin tipo.
    static void Crowd(Client::World::Particles& pool, std::size_t free)
    {
        const auto all = pool.All();
        for (std::size_t index = free; index < all.size(); ++index)
            all[index] = Client::World::Particle{.alive = true};
    }

    // Las partículas del mundo, lugar por lugar, contra las del pool gemelo.
    void ExpectTheSameParticles(const Client::World::Particles& reference)
    {
        const auto got = m_world->Particles().All();
        const auto want = reference.All();
        for (std::size_t index = 0; index < got.size(); ++index)
        {
            SCOPED_TRACE(index);
            ASSERT_EQ(got[index].alive, want[index].alive);
            if (!want[index].alive)
                continue;
            EXPECT_EQ(got[index].type, want[index].type);
            EXPECT_EQ(got[index].subtype, want[index].subtype);
            EXPECT_EQ(got[index].position, want[index].position);
            EXPECT_EQ(got[index].start, want[index].start);
            EXPECT_EQ(got[index].angles, want[index].angles);
            EXPECT_EQ(got[index].light, want[index].light);
            EXPECT_EQ(got[index].scale, want[index].scale);
            EXPECT_EQ(got[index].life, want[index].life);
            EXPECT_EQ(got[index].velocity, want[index].velocity);
            EXPECT_EQ(got[index].rotation, want[index].rotation);
            EXPECT_EQ(got[index].owner, want[index].owner);
        }
    }

    std::unique_ptr<Client::Scenes::WorldState> m_world = std::make_unique<Client::Scenes::WorldState>();
    Client::World::FrameSprites m_sprites;
    Client::Render::Models::Skeleton m_skeleton;
    ActorState m_actor;
    float m_clock = Clock;
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

// 0x507006: el 0x185 crea sus catorce joints 0x614/7 (0x5315b7, sin rand()).
TEST_F(ModelEffectsTest, TheBoneJointsAreCreated)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Clear();
        CrtRandom random(Seed);
        ASSERT_TRUE(Step(0x185, map, random).HasValue());
        EXPECT_EQ(Joints(0x614), 14);
        EXPECT_EQ(random.State(), AfterDraws(Seed, 1).State());
    }
}

// 0x5074bb..0x50775c: el 0x17e con +0x376 en 0x3f, después de 0x505bc0(8, 9). Los 35 puntos de los huesos de
// 0x6c882c; una 0x5de/2 por par de 0x6c8850 y de 0x6c8870 (escala 1 hasta el byte 0x16 y 0,5 desde ahí) y de 0x6c8890
// y de 0x6c8898 (0,6f), en el punto del byte par, mirada desde el del impar con los ángulos que dejó la anterior y la
// distancia de 0x4e9810 en la x de la luz; con ftol(reloj) par, la /2 del punto 30 desde el 0 con 1,3f y la /3 del 1
// con 0,5f; al final, la luz (−1,3) × 3. Lugar por lugar, lo mismo que un pool gemelo con esos pedidos y el mismo
// rand(); la y y la z de la luz del pedido (que el original no escribe) no cambian nada. Con cinco lugares libres, las
// cinco primeras y las demás sin rand().
TEST_F(ModelEffectsTest, TheFlamingMonsterBurnsAlongItsLimbs)
{
    // 0x6c882c..0x6c889f, byte a byte.
    constexpr std::array<std::size_t, 35> Bones{5,  6,  33, 53, 35, 49, 50, 45, 46, 41, 42, 37, 38, 11, 31, 13, 27, 28,
                                                23, 24, 19, 20, 15, 16, 54, 55, 62, 69, 70, 77, 2,  79, 81, 84, 86};
    constexpr std::array<std::size_t, 30> First{0, 2, 2,  3, 2,  4,  4,  5, 5, 6, 4, 7,  7, 8,  4,
                                                9, 9, 10, 4, 11, 11, 12, 6, 5, 8, 7, 10, 9, 12, 11};
    constexpr std::array<std::size_t, 30> Second{0,  13, 13, 14, 13, 15, 15, 16, 16, 17, 15, 18, 18, 19, 15,
                                                 20, 20, 21, 15, 22, 22, 23, 17, 16, 19, 18, 21, 20, 23, 22};
    constexpr std::array<std::size_t, 8> Third{29, 28, 28, 27, 34, 33, 33, 30};
    constexpr std::array<std::size_t, 8> Fourth{26, 25, 25, 24, 32, 31, 31, 30};
    constexpr std::size_t SmallFrom = 0x16;
    const float tail = std::bit_cast<float>(0x3f19999au);
    const float big = std::bit_cast<float>(0x3fa66666u);
    PoseEveryBone();
    m_actor.word376 = 0x3f;
    for (const std::int32_t map : {0, 2})
    {
        for (const auto& [clock, free] :
             {std::pair{12345.0f, Client::World::ParticleCount}, std::pair{12346.0f, Client::World::ParticleCount},
              std::pair{12346.0f, std::size_t{5}}})
        {
            SCOPED_TRACE(testing::Message() << map << " " << clock << " " << free);
            const bool even = *Client::World::OriginalFtolLow(static_cast<double>(clock)) % 2 == 0;
            const bool crowded = free < Client::World::ParticleCount;
            Clear();
            Crowd(m_world->Particles(), free);
            m_clock = clock;
            RedAtTheActor() = 2.0f;
            CrtRandom random(Seed);
            const auto stepped = Step(0x17e, map, random);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(m_actor.bonePoint4c, BoneAt(8));
            EXPECT_EQ(m_actor.effectOrigin, BoneAt(9));
            EXPECT_EQ(Alive(0x5de, 2), crowded ? 5 : (even ? 39 : 38));
            EXPECT_EQ(Alive(0x5de, 3), !crowded && even ? 1 : 0);
            // 0x50772c..0x507754: la celda (10, 20) del personaje con peso 1: 2 − 1,3f = 0x3f333334.
            EXPECT_EQ(std::bit_cast<std::uint32_t>(RedAtTheActor()), 0x3f333334u);

            std::array<glm::vec3, Bones.size()> points{};
            for (std::size_t index = 0; index < Bones.size(); ++index)
                points[index] = BoneAt(Bones[index]);
            auto reference = std::make_unique<Client::World::Particles>();
            Crowd(*reference, free);
            CrtRandom replay = AfterDraws(Seed, 1);
            glm::vec3 angles(0.0f);
            glm::vec3 light(0.0f, 7.0f, -7.0f);
            const auto flame = [&](std::size_t point, std::size_t from, float scale)
            {
                light.x =
                    static_cast<float>(Client::Render::Models::MoveToward(points[from], angles, points[point], 360.0f));
                ReferenceFlame(*reference, replay, clock, 2, points[point], angles, light, scale);
            };
            for (std::size_t byte = 0; byte < First.size(); byte += 2)
            {
                const float scale = byte < SmallFrom ? 1.0f : 0.5f;
                flame(First[byte], First[byte + 1], scale);
                flame(Second[byte], Second[byte + 1], scale);
            }
            for (std::size_t byte = 0; byte < Third.size(); byte += 2)
            {
                flame(Third[byte], Third[byte + 1], tail);
                flame(Fourth[byte], Fourth[byte + 1], tail);
            }
            if (even)
            {
                flame(30, 0, big);
                ReferenceFlame(*reference, replay, clock, 3, points[1], angles, light, 0.5f);
            }
            EXPECT_EQ(random.State(), replay.State());
            ExpectTheSameParticles(*reference);
        }
    }
}

// 0x507761..0x507897: el 0x17e sin el 0x3f. La luz (L; L · 0,5; L · 0,5) con L = sen(reloj · 0,002f) · 0,3f + 0,7f, la
// 0x5de/1 de 0,2f en el hueso 62 mirada desde el 55 y en el 77 desde el 70 (los ángulos siguen de una a la otra) y
// después 0x505d50 (en la 2, sus dos rand() y el polvo 0x5f3/0) y 0x505c50 (en la 6 con el frame en [8, 9), que corta
// en su 0x5f3/1, 0x505d2c).
TEST_F(ModelEffectsTest, TheMonsterWithoutFlamesBurnsItsHands)
{
    PoseEveryBone();
    m_actor.word376 = 0;
    m_actor.angles = {0.0f, 0.0f, 45.0f};
    const auto sine = Client::Render::Models::Detail::SineProductPC53(
        static_cast<double>(Clock) * static_cast<double>(std::bit_cast<float>(0x3b03126fu)),
        std::bit_cast<float>(0x3e99999au));
    ASSERT_TRUE(sine.has_value());
    const double glow = *sine + static_cast<double>(std::bit_cast<float>(0x3f333333u));
    const glm::vec3 light{static_cast<float>(glow), static_cast<float>(glow * 0.5), static_cast<float>(glow * 0.5)};
    // Con el reloj en 12345, los bits que da una emulación x87 aparte (redondeo a 53 bits y seno con 80 dígitos).
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light.x), 0x3f124c2eu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light.y), 0x3e924c2eu);
    EXPECT_EQ(light.z, light.y);
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Clear();
        m_actor.animation.action = 2;
        m_actor.animation.frame = 0.0f;
        CrtRandom random(Seed);
        const auto stepped = Step(0x17e, map, random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(m_actor.bonePoint4c, BoneAt(8));
        EXPECT_EQ(m_actor.effectOrigin, BoneAt(9));
        EXPECT_EQ(Alive(0x5de, 1), 2);
        EXPECT_EQ(Alive(Client::World::DustParticle, 0), 1);

        auto reference = std::make_unique<Client::World::Particles>();
        CrtRandom replay = AfterDraws(Seed, 1);
        glm::vec3 angles(0.0f);
        for (const auto& [from, to] : {std::pair<std::size_t, std::size_t>{55, 62}, {70, 77}})
        {
            static_cast<void>(Client::Render::Models::MoveToward(BoneAt(from), angles, BoneAt(to), 360.0f));
            ReferenceFlame(*reference, replay, Clock, 1, BoneAt(to), angles, light, std::bit_cast<float>(0x3e4ccccdu));
        }
        const float x = static_cast<float>(static_cast<double>(replay.Next() % 200) +
                                           static_cast<double>(m_actor.position.x) - 100.0);
        const float y = static_cast<float>(static_cast<double>(replay.Next() % 200) +
                                           static_cast<double>(m_actor.position.y) - 100.0);
        const auto dust = reference->CreateChecked(Client::World::DustParticle, {x, y, m_actor.position.z},
                                                   m_actor.angles, m_actor.light, 0, 1.0f, Clock, replay, 0);
        ASSERT_TRUE(dust.HasValue());
        EXPECT_EQ(random.State(), replay.State());
        ExpectTheSameParticles(*reference);

        Clear();
        m_actor.animation.action = 6;
        m_actor.animation.frame = 8.0f;
        CrtRandom burst(Seed);
        EXPECT_TRUE(CutAt(Step(0x17e, map, burst), "0x505d2c"));
        EXPECT_EQ(Alive(0x5de, 1), 2);
    }
}

// Con la tabla de partículas llena, 0x53fe70 no crea nada ni saca rand() y el 0x17e sigue: con el 0x3f, la luz; sin
// él, los dos rand() de 0x505d50 en la 2 y los sesenta de 0x505c50 en la 6, que ya no corta.
TEST_F(ModelEffectsTest, TheFlamingMonsterGoesOnWithTheParticlesFull)
{
    PoseEveryBone();
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        for (const float clock : {12345.0f, 12346.0f})
        {
            SCOPED_TRACE(clock);
            Clear();
            for (auto& particle : m_world->Particles().All())
                particle.alive = true;
            m_clock = clock;
            m_actor.word376 = 0x3f;
            RedAtTheActor() = 1.0f;
            CrtRandom random(Seed);
            const auto stepped = Step(0x17e, map, random);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(random.State(), AfterDraws(Seed, 1).State());
            // 1 − 1,3f es negativo: 0x5f9780 deja la celda en 0.
            EXPECT_EQ(RedAtTheActor(), 0.0f);
        }
        m_clock = Clock;
        m_actor.word376 = 0;
        m_actor.animation.action = 2;
        m_actor.animation.frame = 0.0f;
        CrtRandom dusty(Seed);
        ASSERT_TRUE(Step(0x17e, map, dusty).HasValue());
        EXPECT_EQ(dusty.State(), AfterDraws(Seed, 3).State());
        m_actor.animation.action = 6;
        m_actor.animation.frame = 8.0f;
        m_actor.light = {};
        CrtRandom bursting(Seed);
        const auto burst = Step(0x17e, map, bursting);
        ASSERT_TRUE(burst.HasValue()) << burst.Error();
        EXPECT_EQ(bursting.State(), AfterDraws(Seed, 1 + 60).State());
        EXPECT_EQ(m_actor.light, glm::vec3(1.0f));
        m_actor.light = {0.5f, 0.25f, 0.125f};
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
// 0 completa dos 0x5f3/0 y C8/C9, y ambas ramas alcanzan el pulso.
TEST_F(ModelEffectsTest, TheKalimaHelperPulsesAndCompletesItsStoneEffects)
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
        ASSERT_TRUE(Step(0xc5, map, gate).HasValue());
        EXPECT_EQ(Alive(Client::World::DustParticle, 0), 2);
    }
}

// 0x43be8c..43bed9: los fragmentos usan la posición del actor, no la aleatoria de los dos polvos.
TEST_F(ModelEffectsTest, TheKalimaHelperKeepsItsStoneOrderAndPulseWhenPoolsFill)
{
    struct Capacity
    {
        std::size_t effects;
        std::size_t particles;
        int draws;
    };
    for (const Capacity capacity :
         {Capacity{2, 2, 21}, Capacity{1, 2, 16}, Capacity{0, 2, 11}, Capacity{2, 0, 13}, Capacity{0, 0, 3}})
    {
        SCOPED_TRACE(capacity.effects);
        SCOPED_TRACE(capacity.particles);
        Clear();
        auto effects = m_world->Map().effects.Effects();
        for (std::size_t index = 0; index < effects.size(); ++index)
            effects[index].alive = index >= capacity.effects;
        auto particles = m_world->Particles().All();
        for (std::size_t index = capacity.particles; index < particles.size(); ++index)
            particles[index].alive = true;
        m_actor.animation.action = 0;
        m_actor.dword64 = 0;
        CrtRandom random(Seed);
        const auto stepped = Step(0xc5, 24, random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(m_actor.dword64, 1);
        const auto pulse = Client::Render::Models::Detail::SineSumPC53(
            static_cast<double>(Clock) * static_cast<double>(std::bit_cast<float>(0x3a83126fu)), 1.0);
        ASSERT_TRUE(pulse.has_value());
        EXPECT_EQ(m_actor.blendMeshLight, static_cast<float>(*pulse * 0.5));
        for (std::size_t index = 0; index < capacity.effects; ++index)
        {
            EXPECT_EQ(effects[index].type, static_cast<int>(0xc8 + index));
            EXPECT_EQ(effects[index].subtype, 0);
            EXPECT_EQ(effects[index].transform.position, m_actor.position);
            EXPECT_EQ(effects[index].light, m_actor.light);
            EXPECT_EQ(effects[index].ownerContext, nullptr);
        }
        EXPECT_EQ(random.State(), AfterDraws(Seed, capacity.draws).State());
    }
}

// 0x43befb y 0x43bfa8: el 0x192 y el 0x197 (salvo el 0x197 en la 6, que vuelve sin rand(): 0x43bfaf) sacan rand() % 2;
// en 0, el humo 0x5f2/0x11 en el punto (2, 30, 0) del hueso 6 (+0x120, el 0x192) o 31 (+0x5d0, el 0x197) por 0x4ec110,
// con los ángulos del objeto (+0x1c), escala 1 y sin dueño (0x43bf75, 0x43c04b). La luz: (1; 0,2f; 0) del 0x192; la
// del 0x197, (1; 0; 0) con +0x4 en 9 y (0; 0,3f; 1) si no. Con la tabla llena, sin los rand() del humo.
TEST_F(ModelEffectsTest, TheKalimaSmokersBreatheFromTheirBone)
{
    m_actor.bodyPose.assign(40, Client::Render::Models::BoneTransform(1.0f));
    for (std::size_t bone = 0; bone < m_actor.bodyPose.size(); ++bone)
        m_actor.bodyPose[bone][3] = glm::vec3(0.0f, 0.0f, static_cast<float>(bone));
    m_actor.angles = {0.0f, 0.0f, 30.0f};
    const std::uint32_t smoky = FirstSeedWhere(
        [](CrtRandom& random)
        {
            static_cast<void>(random.Next());
            return random.Next() % 2 == 0;
        });
    const std::uint32_t clean = FirstSeedWhere(
        [](CrtRandom& random)
        {
            static_cast<void>(random.Next());
            return random.Next() % 2 != 0;
        });
    struct Case
    {
        std::uint16_t type;
        std::uint8_t action;
        std::int32_t subtype;
        float bone;
        glm::vec3 light;
    };
    const float fifth = std::bit_cast<float>(0x3e4ccccdu);
    const float third = std::bit_cast<float>(0x3e99999au);
    for (const std::int32_t map : {0, 24})
    {
        for (const Case& value :
             {Case{0x192, 0, 0, 6.0f, {1.0f, fifth, 0.0f}}, Case{0x192, 6, 9, 6.0f, {1.0f, fifth, 0.0f}},
              Case{0x197, 0, 9, 31.0f, {1.0f, 0.0f, 0.0f}}, Case{0x197, 3, 0, 31.0f, {0.0f, third, 1.0f}}})
        {
            SCOPED_TRACE(testing::Message()
                         << map << " " << value.type << " " << static_cast<int>(value.action) << " " << value.subtype);
            Clear();
            m_actor.animation.action = value.action;
            m_actor.subtype = value.subtype;
            CrtRandom random(smoky);
            const auto stepped = Step(value.type, map, random);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            auto reference = std::make_unique<Client::World::Particles>();
            CrtRandom expected = AfterDraws(smoky, 2);
            const glm::vec3 point = m_actor.position + glm::vec3(2.0f, 30.0f, value.bone);
            const auto made =
                reference->CreateChecked(0x5f2, point, m_actor.angles, value.light, 0x11, 1.0f, Clock, expected, 0);
            ASSERT_TRUE(made.HasValue());
            ASSERT_TRUE(made.Value().has_value());
            EXPECT_EQ(random.State(), expected.State());
            ASSERT_EQ(Alive(0x5f2, 0x11), 1);
            const Client::World::Particle& want = reference->All()[*made.Value()];
            const auto all = m_world->Particles().All();
            const auto got =
                std::ranges::find_if(all, [](const Client::World::Particle& particle) { return particle.alive; });
            ASSERT_NE(got, all.end());
            EXPECT_EQ(got->position, want.position);
            EXPECT_EQ(got->angles, want.angles);
            EXPECT_EQ(got->light, want.light);
            EXPECT_EQ(got->scale, want.scale);
            EXPECT_EQ(got->life, want.life);
            EXPECT_EQ(got->rotation, want.rotation);
            EXPECT_EQ(got->rotationFactor, want.rotationFactor);
            EXPECT_EQ(got->field64, want.field64);
            EXPECT_EQ(got->owner, want.owner);

            Clear();
            CrtRandom dry(clean);
            ASSERT_TRUE(Step(value.type, map, dry).HasValue());
            EXPECT_EQ(dry.State(), AfterDraws(clean, 2).State());
            EXPECT_EQ(Alive(0x5f2, 0x11), 0);

            for (auto& particle : m_world->Particles().All())
                particle.alive = true;
            CrtRandom crowded(smoky);
            ASSERT_TRUE(Step(value.type, map, crowded).HasValue());
            EXPECT_EQ(crowded.State(), AfterDraws(smoky, 2).State());
        }
        // El 0x197 en la 6: sólo el rand() de 0x506cac.
        Clear();
        m_actor.animation.action = 6;
        CrtRandom still(smoky);
        ASSERT_TRUE(Step(0x197, map, still).HasValue());
        EXPECT_EQ(still.State(), AfterDraws(smoky, 1).State());
        EXPECT_EQ(Alive(0x5f2, 0x11), 0);
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
        m_world->FaunaResources().RecordAnimation(0x1b4, 6.25f, 7);
        CrtRandom posed(Seed);
        const auto stepped = Step(0x1b4, map, posed);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_FLOAT_EQ(m_world->FaunaResources().PoseFrame(0x1b4), 2.75f);
        EXPECT_EQ(m_world->FaunaResources().PoseKey(0x1b4), 2);
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

// 0x53fe83 busca un muerto antes de leer la luz. El polvo de un camino con luz indeterminada no la lee si el pool
// está realmente lleno; el sorteo del caller ya ocurrió.
TEST_F(ModelEffectsTest, TheBarrierDustDoesNotReadItsIndeterminateLightWithAFullPool)
{
    const std::uint32_t dust = FirstSeedWhere(
        [](CrtRandom& random)
        {
            static_cast<void>(random.Next());
            return random.Next() % 10 == 0;
        });
    for (auto& particle : m_world->Particles().All())
        particle.alive = true;
    m_actor.animation.action = 2;
    CrtRandom random(dust);
    const auto stepped = Step(0x1ab, 34, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), AfterDraws(dust, 2).State());
}

TEST_F(ModelEffectsTest, TheBalgassLaunchesTheTourProjectileAtTheFrameGate)
{
    const std::uint32_t noCall = FirstSeedWhere(
        [](CrtRandom& random)
        {
            static_cast<void>(random.Next());
            return random.Next() % 100 != 0;
        });
    m_world->Runtime().crywolf.stage = 4;
    m_actor.animation.action = 3;
    m_actor.animation.frame = 5;
    m_actor.movementPhase = 1;
    CrtRandom random(noCall);
    const auto stepped = Step(0x1b5, 34, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    auto effects = m_world->Map().effects.Effects();
    EXPECT_TRUE(effects[0].alive);
    EXPECT_EQ(effects[0].type, 0xed);
    EXPECT_EQ(effects[0].subtype, 1);
    EXPECT_EQ(m_actor.movementPhase, 0);
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
TEST_F(ModelEffectsTest, TheWarningAltarConsumesTheFlagAndRunsWithPartialPools)
{
    m_world->Runtime().crywolf.stage = 4;
    m_world->Runtime().crywolf.ui.byte723bec = 1;
    for (auto& effect : m_world->Map().effects.Effects())
        effect.alive = true;
    m_world->Map().effects.Effects()[0].alive = false;
    for (auto& particle : m_world->Particles().All())
        particle.alive = true;
    m_world->Particles().All()[0].alive = false;
    CrtRandom random(7);
    const auto stepped = Step(0x1ae, 34, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(m_world->Runtime().crywolf.ui.byte723bec, 0);
    const auto& crater = m_world->Map().effects.Effects()[0];
    EXPECT_EQ(crater.type, 0x636);
    EXPECT_EQ(crater.subtype, 2);
    EXPECT_EQ(crater.life, 40);
    EXPECT_EQ(crater.start.x, 3);
    EXPECT_EQ(crater.start.y, 3);
    EXPECT_EQ(crater.light, glm::vec3(1));
    EXPECT_EQ(m_world->Particles().All()[0].type, 0x640);
    EXPECT_EQ(random.State(), 0x1b8052fbu); // brillo, tres del único constructor aceptado.
}
} // namespace
