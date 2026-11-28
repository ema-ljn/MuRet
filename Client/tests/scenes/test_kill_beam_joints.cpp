#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/JointRequest.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainLighting.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

// The killer's 618/0 and 618/1: 0x530860 (0x532125, 0x5322df, 0x5322f8) and their step in 0x535840
// (0x538313..0x5386a4). 06, "El paso del ataque".
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::JointState;

constexpr std::uint32_t Seed = 0x2b1d3e5u;

struct BeamWorld
{
    BeamWorld()
    {
        killer.alive = true;
        killer.position = {1000.0f, 1000.0f, 100.0f};
        glm::vec3 from{1300.0f, 1000.0f, 100.0f};
        glm::vec3 to = from;
        glm::vec3 angles{0.0f, 0.0f, 0.0f};
        for (const std::int32_t subtype : {0, 1})
        {
            const Client::Kernel::JointRequest request{.type = Client::Joints::KillBeamJoint,
                                                       .firstPosition = from,
                                                       .secondPosition = to,
                                                       .angles = angles,
                                                       .subtype = subtype,
                                                       .ownerContext = &killer,
                                                       .scale = 20.0f,
                                                       .key = -1,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
            created = created && Client::Joints::CreateFaunaJoint(joints, request, random).HasValue();
        }
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Joints::StepJoints(joints, random, nullptr, &particles, 0.0f, nullptr, lighting.get(),
                                          [this](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); });
    }

    ActorState killer;
    std::vector<JointState> joints = std::vector<JointState>(4);
    CrtRandom random{Seed};
    Client::Particles::Particles particles;
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    std::vector<std::int32_t> sounds;
    bool created = true;
};

int AliveLightning(const Client::Particles::Particles& particles, std::int32_t subtype)
{
    int found = 0;
    for (const Client::Particles::Particle& particle : particles.All())
    {
        if (particle.alive && particle.type == Client::Particles::LightningParticle && particle.subtype == subtype)
            ++found;
    }
    return found;
}

// 0x532125..0x5322f8: the width, speed 3, life 120, 8 points and each subtype's light, without rand(). The first step
// (life 120, at 100 or more) moves 3 with the earlier angle matrix, turns the heading 10 (the /0) or −10 (the /1),
// rises 6 or 5, draws rand() & 3 for the dynamic light, leaves a 0x5d5/0 and appends the tail.
TEST(KillBeamJointsTest, TheBeamsSpinWhileTheirLifeIsAboveOneHundred)
{
    BeamWorld tested;
    ASSERT_TRUE(tested.created);
    EXPECT_EQ(tested.random.State(), CrtRandom(Seed).State());
    EXPECT_EQ(tested.joints[0].light, (glm::vec3{std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3e99999au),
                                                 std::bit_cast<float>(0x3e4ccccdu)}));
    EXPECT_EQ(tested.joints[1].light,
              (glm::vec3{std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu), 0.5f}));
    const JointState first = tested.joints[0];
    const JointState second = tested.joints[1];

    ASSERT_TRUE(tested.Step().HasValue());

    CrtRandom expected(Seed);
    static_cast<void>(expected.Next());
    static_cast<void>(expected.Next());
    EXPECT_EQ(tested.random.State(), expected.State());
    for (const auto& [beam, before, turn, rise] :
         {std::tuple{&tested.joints[0], &first, 10.0f, 6.0f}, std::tuple{&tested.joints[1], &second, -10.0f, 5.0f}})
    {
        SCOPED_TRACE(beam->subtype);
        const glm::vec3 moved = Client::Math::OriginalVectorRotate(glm::vec3(0.0f, -3.0f, 0.0f),
                                                                   Client::Math::OriginalAngleMatrix(before->angles));
        EXPECT_EQ(beam->position.x, before->position.x + moved.x);
        EXPECT_EQ(beam->position.y, before->position.y + moved.y);
        EXPECT_EQ(beam->position.z, before->position.z + moved.z + rise);
        EXPECT_EQ(beam->angles.z, turn);
        EXPECT_EQ(beam->speed, 3.0f);
        EXPECT_EQ(beam->life, 119);
        EXPECT_TRUE(beam->alive);
    }
    EXPECT_EQ(AliveLightning(tested.particles, Client::Particles::LightningTrailSubtype), 2);
    EXPECT_TRUE(tested.sounds.empty());
}

// 0x538475..0x5385cd: below life 100, the speed rises by 5 up to 30 and 0x4e9810 turns them toward the killer (its
// position, 120 higher). At 35 or less they die, leave the 0x5d5/1 and request sound 0x23. One rand() per live joint
// and per step, and nothing more.
TEST(KillBeamJointsTest, TheBeamsComeBackToTheKillerAndSound)
{
    BeamWorld tested;
    ASSERT_TRUE(tested.created);
    int draws = 0;
    int steps = 0;
    while ((tested.joints[0].alive || tested.joints[1].alive) && steps < 130)
    {
        SCOPED_TRACE(steps);
        const int alive = (tested.joints[0].alive ? 1 : 0) + (tested.joints[1].alive ? 1 : 0);
        ASSERT_TRUE(tested.Step().HasValue());
        draws += alive;
        ++steps;
    }
    CrtRandom expected(Seed);
    for (int draw = 0; draw < draws; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_LT(steps, 120);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x23, 0x23}));
    EXPECT_EQ(AliveLightning(tested.particles, Client::Particles::LightningHitSubtype), 2);
    EXPECT_EQ(tested.joints[0].speed, 30.0f);
}

// 0x538501: without the owner character, the step stops on turning back.
TEST(KillBeamJointsTest, AnOwnerlessBeamStopsWhenItTurnsBack)
{
    BeamWorld tested;
    ASSERT_TRUE(tested.created);
    tested.joints[0].ownerContext = nullptr;
    tested.joints[0].life = 99;
    const auto stepped = tested.Step();
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x538501"), std::string::npos);
}
} // namespace
