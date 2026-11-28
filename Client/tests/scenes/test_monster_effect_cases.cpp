#include "Monsters/ModelEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRetainedMath.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Terrain/DynamicWorldLight.h"
#include "Assets/FaunaModels.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldState.h"
#include "Math/Ftol.h"
#include "Terrain/GroundMarks.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainLighting.h"
#include "Audio/SoundId.h"
#include "CastleSiege/CastleSiegeState.h"

#include <glm/ext/vector_float3.hpp>

#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

// The 0x505de0 cases and the 0x5085db helpers of a character other than 0x20a (Monsters::StepTheModelEffects).
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Tests::AfterDraws;
using Client::Tests::FirstSeedWhere;

constexpr std::uint32_t Seed = 0x1357u;
constexpr float Clock = 12345.0f;

class ModelEffectsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_skeleton.bones.assign(50, Client::Models::SkeletonBone{.dummy = true, .parent = -1, .actions = {}});
        m_skeleton.actions.assign(
            12, Client::Models::ActionInfo{.repeats = true, .speed = 0.25f, .keyCount = 10, .lockPositions = false});
        m_actor.alive = true;
        m_actor.scale = 1.0f;
        m_actor.position = {1000.0f, 2000.0f, 50.0f};
        m_actor.light = {0.5f, 0.25f, 0.125f};
    }

    Client::Core::Expected<void, std::string> Step(std::uint16_t type, std::int32_t map, CrtRandom& random)
    {
        m_actor.type = type;
        Client::Monsters::ModelEffectsPass pass{.actor = m_actor,
                                                .slot = 3,
                                                .world = *m_world,
                                                .random = random,
                                                .skeleton = m_skeleton,
                                                .particles = &m_world->Particles(),
                                                .sprites = &m_sprites,
                                                .worldClock = m_clock,
                                                .map = map,
                                                .groundMarks = m_groundMarks,
                                                .dynamicLighting = &m_light.Lighting(),
                                                .faunaModels = &m_faunaModels};
        return Client::Monsters::StepTheModelEffects(pass);
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

    // Monster45 (model 0x17e) has 88 bones; each at identity shifted by BoneOffset, so the 0x4ec110 point with no
    // offset is the position plus that.
    void PoseEveryBone()
    {
        constexpr std::size_t MonsterBones = 88;
        m_actor.bodyPose.assign(MonsterBones, Client::Math::BoneTransform(1.0f));
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

    // The dynamic light's red at the character's cell; 0x5f9780 keeps it from going below 0.
    float& RedAtTheActor()
    {
        const auto x = static_cast<std::uint8_t>(m_actor.position.x / 100.0f);
        const auto y = static_cast<std::uint8_t>(m_actor.position.y / 100.0f);
        return m_light.Lighting().cells[Client::Data::Terrain::CellIndex(x, y)][0];
    }

    // 0x53fe70(0x5de, …) in a twin pool.
    static void ReferenceFlame(Client::Particles::Particles& reference, CrtRandom& replay, const float& clock,
                               std::int32_t subtype, const glm::vec3& point, const glm::vec3& angles,
                               const glm::vec3& light, float scale)
    {
        Client::Particles::PortableParticleTrigonometry trigonometry;
        const auto made = reference.CreateChecked(0x5de, point, angles, light, subtype, scale, std::cref(clock), replay,
                                                  0, trigonometry);
        ASSERT_TRUE(made.HasValue());
    }

    static void Crowd(Client::Particles::Particles& pool, std::size_t free)
    {
        const auto all = pool.All();
        for (std::size_t index = free; index < all.size(); ++index)
            all[index] = Client::Particles::Particle{.alive = true};
    }

    void ExpectTheSameParticles(const Client::Particles::Particles& reference)
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

    std::unique_ptr<Client::Kernel::WorldState> m_world = std::make_unique<Client::Kernel::WorldState>();
    Client::Particles::FrameSprites m_sprites;
    Client::Terrain::GroundMarkTable m_marks{};
    Client::Terrain::GroundMarkTable* m_groundMarks = &m_marks;
    Client::Terrain::DynamicWorldLight m_light;
    Client::Assets::FaunaModels m_faunaModels;
    Client::Models::Skeleton m_skeleton;
    ActorState m_actor;
    float m_clock = Clock;
};

// 0x506cac: the unconditional & 7; a model with no case or helper draws nothing more.
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

// 0x5073c6, 0x5073ed, 0x507ab7, 0x507ade and 0x507e63: U or V from the clock, with no more rand().
TEST_F(ModelEffectsTest, TheScrollingModelsWriteTheirTextureOffsets)
{
    const std::int32_t clock = *Client::Math::OriginalFtolLow(static_cast<double>(Clock));
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

// 0x5084b3: 0x152 (Monster01, Lorencia) only in its frame windows, with rand() % 2 == 0, the 0x5f2/0.
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
            EXPECT_EQ(Alive(Client::Particles::BreathParticle, 0), breathes ? 1 : 0);
            if (!breathes)
            {
                EXPECT_EQ(random.State(), AfterDraws(breath, 1).State());
            }
        }
    }
}

// 0x5079c0: 0x177 raises +0x68 in actions 3 and 4 up to 1 and lowers it in the others down to 0.
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

// 0x507e8a and 0x507eb7: rand() & 63 or & 255 == 0 requests the sound (CL10).
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
        EXPECT_EQ(m_world->Runtime().lastSoundId, Client::Audio::SoundId{0xa1});
        Clear();
        CrtRandom other(lucky);
        ASSERT_TRUE(Step(0x1d3, map, other).HasValue());
        EXPECT_EQ(m_world->Runtime().lastSoundId, Client::Audio::SoundId{0xa0});
        EXPECT_EQ(other.State(), AfterDraws(lucky, 2).State());
    }
}

// 0x507dab: 0x15d at level 2 makes ten 0x5d9/0 on rand() bones; at any other level, only +0x68.
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
        EXPECT_EQ(Alive(Client::Particles::FireParticle, 0), 10);
    }
}

// 0x507b05: 0x172 in action 3 makes the 0x5ca/0 and 0x5d9/0 of bone 33; in action 6 before frame 12, twenty 0x5d9/0.
TEST_F(ModelEffectsTest, TheWizardSparksByItsAction)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Clear();
        m_actor.animation.action = 3;
        CrtRandom random(Seed);
        ASSERT_TRUE(Step(0x172, map, random).HasValue());
        EXPECT_EQ(Alive(Client::Particles::ThunderParticle, 0), 1);
        EXPECT_EQ(Alive(Client::Particles::FireParticle, 0), 1);
        Clear();
        m_actor.animation.action = 6;
        m_actor.animation.frame = 11.5f;
        ASSERT_TRUE(Step(0x172, map, random).HasValue());
        EXPECT_EQ(Alive(Client::Particles::FireParticle, 0), 20);
        Clear();
        m_actor.animation.frame = 12.0f;
        CrtRandom late(Seed);
        ASSERT_TRUE(Step(0x172, map, late).HasValue());
        EXPECT_EQ(late.State(), AfterDraws(Seed, 1).State());
    }
}

// 0x507ee6: 0x1ce in action 0 at frame 5: sound 0xa0, +0x68 at s, +0x64 at 4 and four 0x617/0 joints with their
// 0x5c5/0; with +0x7c & 0x10000000, nothing.
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
        EXPECT_EQ(m_world->Runtime().lastSoundId, Client::Audio::SoundId{0xa0});
        EXPECT_EQ(m_actor.blendMeshLight, Brightness(Seed));
        EXPECT_EQ(m_actor.dword64, 4);
        EXPECT_EQ(Joints(0x617), 4);
        EXPECT_EQ(Alive(Client::Particles::FairySparkParticle, 0), 4);
        Clear();
        m_actor.flags7c = 0x10000000u;
        m_actor.dword64 = 0;
        CrtRandom quiet(Seed);
        ASSERT_TRUE(Step(0x1ce, map, quiet).HasValue());
        EXPECT_EQ(m_actor.dword64, 0);
        EXPECT_EQ(quiet.State(), AfterDraws(Seed, 1).State());
    }
}

// 0x508093: 0x1f5 in action 0 makes four 0x617/0 joints; outside action 0, nothing.
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

// 0x50826d: 0x155 with rand() % 4 == 0 makes the 0x5f2/0 in Devias and the 0x5f3/0 elsewhere; 0x166 also leaves its
// 0x5ac sprite; while dying, nothing.
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
        EXPECT_EQ(Alive(Client::Particles::BreathParticle, 0), map == 2 ? 1 : 0);
        EXPECT_EQ(Alive(Client::Particles::DustParticle, 0), map == 2 ? 0 : 1);
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

// 0x505c50: 0x157 in action 6 with frame in [8, 9), after the rand() & 7, loops twenty times: own light to 1, three
// rand() around the position ((& 63) − 32, (& 63) − 32, (& 31) − 16) and the 0x5f3/1 dust (0x505d2c, born like 5f6/1:
// six rand()). Positions, scales and angles come from burst.py (0x505c50 and 0x54271f on a separate x87), not from
// CreateChecked.
TEST_F(ModelEffectsTest, TheDyingBurstLeavesTwentyDusts)
{
    struct Dust
    {
        glm::vec3 position;
        std::uint32_t scale;
        glm::vec3 angles;
    };
    constexpr std::array<Dust, 20> Dusts{{
        {{1008, 2035, 66}, 0x3f1eb852u, {294, 0, 123}}, {{999, 2004, 62}, 0x3eccccccu, {56, 0, 253}},
        {{970, 1975, 66}, 0x3f0a3d70u, {142, 0, 187}},  {{981, 1985, 58}, 0x3ee66666u, {38, 0, 99}},
        {{1018, 1998, 37}, 0x3ee147aeu, {289, 0, 44}},  {{973, 2002, 69}, 0x3f147ae1u, {15, 0, 256}},
        {{981, 1980, 62}, 0x3ef5c28fu, {114, 0, 91}},   {{1004, 1986, 60}, 0x3f0ccccdu, {328, 0, 298}},
        {{1000, 1969, 42}, 0x3ee66666u, {353, 0, 171}}, {{978, 2020, 42}, 0x3ec7ae14u, {166, 0, 16}},
        {{970, 2010, 55}, 0x3eae147bu, {145, 0, 245}},  {{996, 1980, 40}, 0x3f1eb852u, {300, 0, 133}},
        {{982, 1995, 56}, 0x3ee66666u, {142, 0, 311}},  {{985, 2027, 38}, 0x3edc28f5u, {213, 0, 351}},
        {{1015, 1990, 45}, 0x3eb33333u, {204, 0, 97}},  {{981, 2026, 41}, 0x3ed70a3du, {182, 0, 153}},
        {{1009, 2022, 41}, 0x3ee147aeu, {289, 0, 30}},  {{995, 2010, 28}, 0x3f0f5c29u, {327, 0, 178}},
        {{1015, 2016, 52}, 0x3ea8f5c2u, {198, 0, 2}},   {{1005, 1967, 52}, 0x3f2147aeu, {152, 0, 131}},
    }};
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Clear();
        m_actor.animation.action = 6;
        m_actor.animation.frame = 8.0f;
        m_actor.light = {};
        CrtRandom random(Seed);
        const auto stepped = Step(0x157, map, random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(m_actor.light, glm::vec3(1.0f));
        EXPECT_EQ(Alive(Client::Particles::DustParticle, 1), 20);
        // The rand() & 7 plus twenty loops of nine: 181.
        EXPECT_EQ(random.State(), 0xd8d78c92u);
        for (std::size_t slot = 0; slot < Dusts.size(); ++slot)
        {
            const Client::Particles::Particle& made = m_world->Particles().All()[slot];
            ASSERT_TRUE(made.alive) << slot;
            EXPECT_EQ(made.type, Client::Particles::DustParticle);
            EXPECT_EQ(made.subtype, 1);
            EXPECT_EQ(made.life, 40);
            EXPECT_EQ(made.position, Dusts[slot].position) << slot;
            EXPECT_EQ(std::bit_cast<std::uint32_t>(made.scale), Dusts[slot].scale) << slot;
            EXPECT_EQ(made.angles, Dusts[slot].angles) << slot;
            EXPECT_EQ(made.light, glm::vec3(1.0f));
            EXPECT_EQ(made.velocity, glm::vec3(0.0f));
            EXPECT_EQ(made.owner, 0u);
        }
        m_actor.animation.frame = 9.0f;
        CrtRandom late(Seed);
        EXPECT_TRUE(Step(0x157, map, late).HasValue());
        EXPECT_EQ(late.State(), AfterDraws(Seed, 1).State());
    }
}

// 0x505c50 with the pool full: 0x53fe70 draws nothing, but the twenty loops still set the light and draw their three
// rand() (1 + 20 · 3 = 61, burst.py). With a NaN frame, the fcomp at 0x505c6d sets C0 to 1 and 0x505c78 exits.
TEST_F(ModelEffectsTest, TheDyingBurstScattersWithAFullPoolAndSkipsANaNFrame)
{
    Clear();
    for (auto& particle : m_world->Particles().All())
        particle.alive = true;
    m_actor.animation.action = 6;
    m_actor.animation.frame = 8.5f;
    m_actor.light = {};
    CrtRandom random(Seed);
    ASSERT_TRUE(Step(0x157, 0, random).HasValue());
    EXPECT_EQ(m_actor.light, glm::vec3(1.0f));
    EXPECT_EQ(random.State(), 0x90b0001au);
    EXPECT_TRUE(std::ranges::all_of(m_world->Particles().All(),
                                    [](const Client::Particles::Particle& particle) { return particle.type == 0; }));

    Clear();
    m_actor.animation.frame = std::numeric_limits<float>::quiet_NaN();
    m_actor.light = {};
    CrtRandom nan(Seed);
    ASSERT_TRUE(Step(0x157, 0, nan).HasValue());
    EXPECT_EQ(nan.State(), AfterDraws(Seed, 1).State());
    EXPECT_EQ(m_actor.light, glm::vec3(0.0f));
    EXPECT_EQ(Alive(Client::Particles::DustParticle, 1), 0);
}

// 0x507006: 0x185 creates its fourteen 0x614/7 joints (0x5315b7, no rand()).
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

// 0x5074bb..0x50775c: 0x17e with +0x376 == 0x3f, after 0x505bc0(8, 9). The 35 bone points of 0x6c882c; one 0x5de/2
// per pair of 0x6c8850 and 0x6c8870 (scale 1 up to byte 0x16, 0.5 from there) and of 0x6c8890 and 0x6c8898 (0.6f), at
// the even byte's point, looking from the odd one's with the angles the previous one left and the 0x4e9810 distance in
// the light's x; with ftol(clock) even, the /2 of point 30 from point 0 with 1.3f and the /3 of point 1 with 0.5f; at
// the end, the light (−1.3) × 3. The y and z of the requested light (the original does not write them) change nothing.
// With five free slots, the first five, and the rest with no rand().
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
             {std::pair{12345.0f, Client::Particles::ParticleCount},
              std::pair{12346.0f, Client::Particles::ParticleCount}, std::pair{12346.0f, std::size_t{5}}})
        {
            SCOPED_TRACE(testing::Message() << map << " " << clock << " " << free);
            const bool even = *Client::Math::OriginalFtolLow(static_cast<double>(clock)) % 2 == 0;
            const bool crowded = free < Client::Particles::ParticleCount;
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
            // 0x50772c..0x507754: the character's cell (10, 20) with weight 1: 2 − 1.3f = 0x3f333334.
            EXPECT_EQ(std::bit_cast<std::uint32_t>(RedAtTheActor()), 0x3f333334u);

            std::array<glm::vec3, Bones.size()> points{};
            for (std::size_t index = 0; index < Bones.size(); ++index)
                points[index] = BoneAt(Bones[index]);
            auto reference = std::make_unique<Client::Particles::Particles>();
            Crowd(*reference, free);
            CrtRandom replay = AfterDraws(Seed, 1);
            glm::vec3 angles(0.0f);
            glm::vec3 light(0.0f, 7.0f, -7.0f);
            const auto flame = [&](std::size_t point, std::size_t from, float scale)
            {
                light.x = static_cast<float>(Client::Math::MoveToward(points[from], angles, points[point], 360.0f));
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

// 0x507761..0x507897: 0x17e without the 0x3f. The light (L; L · 0.5; L · 0.5) with L = sin(clock · 0.002f) · 0.3f +
// 0.7f, the 0x5de/1 of 0.2f at bone 62 looking from 55 and at 77 from 70 (the angles carry from one to the other), then
// 0x505d50 (in action 2, its two rand() and the 0x5f3/0 dust) and 0x505c50 (in action 6 with frame in [8, 9), its
// twenty 0x5f3/1, 0x505d2c).
TEST_F(ModelEffectsTest, TheMonsterWithoutFlamesBurnsItsHands)
{
    PoseEveryBone();
    m_actor.word376 = 0;
    m_actor.angles = {0.0f, 0.0f, 45.0f};
    const auto sine = Client::Math::Detail::SineProductPC53(static_cast<double>(Clock) *
                                                                static_cast<double>(std::bit_cast<float>(0x3b03126fu)),
                                                            std::bit_cast<float>(0x3e99999au));
    ASSERT_TRUE(sine.has_value());
    const double glow = *sine + static_cast<double>(std::bit_cast<float>(0x3f333333u));
    const glm::vec3 light{static_cast<float>(glow), static_cast<float>(glow * 0.5), static_cast<float>(glow * 0.5)};
    // With the clock at 12345, the bits from a separate x87 emulation (53-bit rounding and an 80-digit sine).
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
        EXPECT_EQ(Alive(Client::Particles::DustParticle, 0), 1);

        auto reference = std::make_unique<Client::Particles::Particles>();
        CrtRandom replay = AfterDraws(Seed, 1);
        glm::vec3 angles(0.0f);
        for (const auto& [from, to] : {std::pair<std::size_t, std::size_t>{55, 62}, {70, 77}})
        {
            static_cast<void>(Client::Math::MoveToward(BoneAt(from), angles, BoneAt(to), 360.0f));
            ReferenceFlame(*reference, replay, Clock, 1, BoneAt(to), angles, light, std::bit_cast<float>(0x3e4ccccdu));
        }
        const float x = static_cast<float>(static_cast<double>(replay.Next() % 200) +
                                           static_cast<double>(m_actor.position.x) - 100.0);
        const float y = static_cast<float>(static_cast<double>(replay.Next() % 200) +
                                           static_cast<double>(m_actor.position.y) - 100.0);
        const auto dust = reference->CreateChecked(Client::Particles::DustParticle, {x, y, m_actor.position.z},
                                                   m_actor.angles, m_actor.light, 0, 1.0f, Clock, replay, 0);
        ASSERT_TRUE(dust.HasValue());
        EXPECT_EQ(random.State(), replay.State());
        ExpectTheSameParticles(*reference);

        Clear();
        m_actor.animation.action = 6;
        m_actor.animation.frame = 8.0f;
        CrtRandom burst(Seed);
        const auto burnt = Step(0x17e, map, burst);
        ASSERT_TRUE(burnt.HasValue()) << burnt.Error();
        EXPECT_EQ(Alive(0x5de, 1), 2);
        EXPECT_EQ(Alive(Client::Particles::DustParticle, 1), 20);
    }
}

// With the particle table full, 0x53fe70 creates nothing and draws no rand(), and 0x17e goes on: with the 0x3f, the
// light; without it, the two rand() of 0x505d50 in action 2 and the sixty of 0x505c50 in action 6, which no longer
// cuts.
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
            // 1 − 1.3f is negative: 0x5f9780 leaves the cell at 0.
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

// 0x507414: 0x182 with rand() % 5 == 0 leaves the 0x5e3 ground mark.
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
        m_marks = {};
        CrtRandom random(mark);
        ASSERT_TRUE(Step(0x182, map, random).HasValue());
        EXPECT_TRUE(m_marks[0].live);
        EXPECT_EQ(m_marks[0].type, 0x5e3);
    }
}

// Without the marks table, the 0x182 that marks stops at 0x507486.
TEST_F(ModelEffectsTest, WithoutTheMarksTableTheBleedingMonsterStops)
{
    const std::uint32_t mark = FirstSeedWhere(
        [](CrtRandom& random)
        {
            static_cast<void>(random.Next());
            return random.Next() % 5 == 0;
        });
    m_groundMarks = nullptr;
    CrtRandom random(mark);

    const auto stepped = Step(0x182, 0, random);

    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x507486"), std::string::npos) << stepped.Error();
}

// 0x43bdd0: 0x191 and 0x196 pulse +0x68; 0x193 to 0x195, nothing; 0xc5 outside action 0 sets +0x64 to 1; in action 0
// it completes two 0x5f3/0 and C8/C9, and both branches reach the pulse.
TEST_F(ModelEffectsTest, TheKalimaHelperPulsesAndCompletesItsStoneEffects)
{
    const auto pulse = Client::Math::Detail::SineProductPC53(static_cast<double>(Clock) *
                                                                 static_cast<double>(std::bit_cast<float>(0x3a83126fu)),
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
        EXPECT_EQ(Alive(Client::Particles::DustParticle, 0), 2);
    }
}

// 0x43be8c..43bed9: the fragments use the actor's position, not the random one of the two dusts.
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
        const auto pulse = Client::Math::Detail::SineSumPC53(
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

// 0x43befb and 0x43bfa8: 0x192 and 0x197 (except 0x197 in action 6, which returns with no rand(): 0x43bfaf) draw
// rand() % 2; at 0, the 0x5f2/0x11 smoke at point (2, 30, 0) of bone 6 (+0x120, 0x192) or 31 (+0x5d0, 0x197) by
// 0x4ec110, with the object's angles (+0x1c), scale 1 and no owner (0x43bf75, 0x43c04b). The light: (1; 0.2f; 0) for
// 0x192; for 0x197, (1; 0; 0) with +0x4 at 9 and (0; 0.3f; 1) otherwise. With the table full, no smoke rand().
TEST_F(ModelEffectsTest, TheKalimaSmokersBreatheFromTheirBone)
{
    m_actor.bodyPose.assign(40, Client::Math::BoneTransform(1.0f));
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
            auto reference = std::make_unique<Client::Particles::Particles>();
            CrtRandom expected = AfterDraws(smoky, 2);
            const glm::vec3 point = m_actor.position + glm::vec3(2.0f, 30.0f, value.bone);
            const auto made =
                reference->CreateChecked(0x5f2, point, m_actor.angles, value.light, 0x11, 1.0f, Clock, expected, 0);
            ASSERT_TRUE(made.HasValue());
            ASSERT_TRUE(made.Value().has_value());
            EXPECT_EQ(random.State(), expected.State());
            ASSERT_EQ(Alive(0x5f2, 0x11), 1);
            const Client::Particles::Particle& want = reference->All()[*made.Value()];
            const auto all = m_world->Particles().All();
            const auto got =
                std::ranges::find_if(all, [](const Client::Particles::Particle& particle) { return particle.alive; });
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
        // 0x197 in action 6: only the rand() of 0x506cac.
        Clear();
        m_actor.animation.action = 6;
        CrtRandom still(smoky);
        ASSERT_TRUE(Step(0x197, map, still).HasValue());
        EXPECT_EQ(still.State(), AfterDraws(smoky, 1).State());
        EXPECT_EQ(Alive(0x5f2, 0x11), 0);
    }
}

// 0x42de90: in map 30, 0x19b sets the z angle to 0, 0x1a1 to 45 and 0x1fc sets +0x58 to −2 (or −1 with the siege and
// 0x20000); in the other maps, nothing.
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
        m_world->Runtime().castleSiege.siegeActive = true;
        m_world->Runtime().castleSiege.castleFlags7239e4 = 0x20000;
        ASSERT_TRUE(Step(0x1fc, map, random).HasValue());
        EXPECT_EQ(m_actor.dword58, castle ? -1 : 7);
        m_world->Runtime().castleSiege.siegeActive = false;
        EXPECT_EQ(random.State(), AfterDraws(Seed, 5).State());
    }
}

// 0x42f550 and 0x431470: 0x1b2 draws its rand() % 360 and cuts at the named bones (in 35, at the first one); 0x1b1 in
// action 6 does nothing; 0x1ad with rand() % 10 == 0 raises its dust; 0x1b4 at rest poses without cutting, and in
// action 2 its dust reads a light it does not write.
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
        EXPECT_EQ(Alive(Client::Particles::DustParticle, 0), 1);
        m_actor.animation.action = 0;
        m_actor.animation.frame = 3.0f;
        m_faunaModels.RecordAnimation(0x1b4, 6.25f, 7);
        CrtRandom posed(Seed);
        const auto stepped = Step(0x1b4, map, posed);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_FLOAT_EQ(m_faunaModels.PoseFrame(0x1b4), 2.75f);
        EXPECT_EQ(m_faunaModels.PoseKey(0x1b4), 2);
        EXPECT_EQ(posed.State(), AfterDraws(Seed, 1).State());
        m_actor.animation.action = 2;
        CrtRandom undefined(dust);
        EXPECT_TRUE(CutAt(Step(0x1b4, map, undefined), "0x431e86"));
    }
}

// 0x431470 with 0x1b5: with the Crywolf stage at 4 and rand() % 100 == 0, action 3 and +0x84 to 1; without the stage,
// nothing.
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

// 0x53fe83 looks for a dead slot before reading the light. The dust of a path with an indeterminate light does not read
// it if the pool is really full; the caller's draw already happened.
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

// 0x441210 and 0x426ce0: the lights of 0x1a4 and 0x1bb, with no rand().
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
    EXPECT_EQ(random.State(), 0x1b8052fbu); // brightness, plus three from the only accepted constructor.
}
} // namespace
