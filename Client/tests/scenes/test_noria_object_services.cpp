#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/NoriaObjectServices.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/IFishStepServices.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>
#include <vector>

namespace
{
Client::Scenes::FaunaDrawCarry Pose()
{
    Client::Scenes::FaunaDrawCarry pose;
    pose.bones.resize(70, Client::Render::Models::BoneTransform(1.0f));
    for (int bone = 0; bone < 70; ++bone)
        pose.bones[static_cast<std::size_t>(bone)][3] =
            glm::vec4(100.0f + static_cast<float>(bone), 200.0f + static_cast<float>(bone),
                      300.0f + static_cast<float>(bone), 1.0f);
    return pose;
}

TEST(NoriaObjectServicesTest, CommonFlaresReadTheRetainedWorldBonesAndTheCommonRandomLight)
{
    struct Probe
    {
        std::int16_t type;
        std::vector<int> bones;
        float scale;
    };
    for (const Probe& probe :
         {Probe{1, {2, 4, 6}, 0.5f}, Probe{9, {1}, 1.5f}, Probe{0x11, {4, 7, 10, 13}, 1.0f}, Probe{0x23, {3}, 1.5f}})
    {
        auto pose = Pose();
        Client::World::FrameSprites sprites;
        Client::Scenes::GameObjectPass pass;
        pass.sprites = &sprites;
        pass.retainedPose = &pose;
        Client::World::WorldObject object;
        object.type = probe.type;
        const auto stepped = Client::Scenes::StepNoriaObjectFrame(object, 61, pass);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        for (std::size_t index = 0; index < probe.bones.size(); ++index)
        {
            const auto& sprite = sprites.Sprites()[index];
            ASSERT_TRUE(sprite.alive);
            const float bone = static_cast<float>(probe.bones[index]);
            EXPECT_EQ(sprite.texture, 0x5ac);
            EXPECT_EQ(sprite.position, glm::vec3(100 + bone, 200 + bone, 300 + bone));
            EXPECT_FLOAT_EQ(sprite.scale, probe.scale);
            EXPECT_FLOAT_EQ(sprite.light.x, 0.284f);
            EXPECT_FLOAT_EQ(sprite.light.y, 0.497f);
            EXPECT_FLOAT_EQ(sprite.light.z, 0.71f);
        }
        EXPECT_FALSE(sprites.Sprites()[probe.bones.size()].alive);
    }
}

TEST(NoriaObjectServicesTest, TheFairyPublishesItsPairedRingsAndFiveWhiteFlaresBeforeItsGuard)
{
    auto pose = Pose();
    Client::World::FrameSprites sprites;
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Render::Models::CrtRandom random(7);
    Client::Scenes::GameObjectPass pass;
    pass.random = &random;
    pass.sprites = &sprites;
    pass.particles = particles.get();
    pass.retainedPose = &pose;
    pass.worldClock = 1234.75f;
    Client::World::WorldObject object;
    object.type = 0x27;
    const auto stepped = Client::Scenes::StepNoriaObjectFrame(object, 61, pass);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_TRUE(sprites.Sprites()[6].alive);
    EXPECT_FALSE(sprites.Sprites()[7].alive);
    EXPECT_EQ(sprites.Sprites()[0].texture, 0x5d5);
    EXPECT_EQ(sprites.Sprites()[0].position, glm::vec3(157, 257, 357));
    EXPECT_EQ(sprites.Sprites()[0].rotation, 123.0f);
    EXPECT_EQ(sprites.Sprites()[1].rotation, -123.0f);
    for (int index = 2; index < 7; ++index)
    {
        const auto& sprite = sprites.Sprites()[static_cast<std::size_t>(index)];
        EXPECT_EQ(sprite.texture, 0x5ac);
        EXPECT_EQ(sprite.light, glm::vec3(1));
    }
    EXPECT_EQ(random.State(), 0x9974714du); // Cinco guardas de5fc y una del estallido.
}

TEST(NoriaObjectServicesTest, TheFairyInterleavesEachJointWithItsSparkEvenWhenTheParticlePoolIsFull)
{
    auto pose = Pose();
    Client::World::FrameSprites sprites;
    auto particles = std::make_unique<Client::World::Particles>();
    for (auto& particle : particles->All())
        particle.alive = true;
    Client::Render::Models::CrtRandom random(4);
    Client::Scenes::GameObjectPass pass;
    pass.random = &random;
    pass.sprites = &sprites;
    pass.particles = particles.get();
    pass.retainedPose = &pose;
    int calls = 0;
    pass.createJoint = [&](const Client::World::FishEffectRequest& request) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(request.type, 0x617);
        EXPECT_EQ(request.firstPosition, glm::vec3(158, 258, 358));
        EXPECT_EQ(request.secondPosition, request.firstPosition);
        EXPECT_EQ(request.angles.y, 140.0f);
        EXPECT_GE(request.angles.x, 60.0f);
        EXPECT_LT(request.angles.x, 120.0f);
        EXPECT_EQ(request.ownerContext, nullptr);
        EXPECT_EQ(request.scale, 10.0f);
        EXPECT_EQ(request.subtype, 0);
        EXPECT_EQ(request.key, -1);
        EXPECT_EQ(request.target, -1);
        static_cast<void>(random.Next());
        static_cast<void>(random.Next()); // El callee617 toma2rand, intercalados con el caller.
        ++calls;
        return {};
    };
    Client::World::WorldObject object;
    object.type = 0x27;
    const auto stepped = Client::Scenes::StepNoriaObjectFrame(object, 61, pass);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(calls, 8);
    Client::Render::Models::CrtRandom expected(4);
    for (int draw = 0; draw < 38; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State()); // 6 guardas+8*(2caller+2joint),sinRNG de5c5.
}
TEST(NoriaObjectServicesTest, AFailedShinyEmissionKeepsItsGuardAndTheSpritesAlreadyPublished)
{
    auto pose = Pose();
    Client::World::FrameSprites sprites;
    Client::Render::Models::CrtRandom random(7);
    Client::Scenes::GameObjectPass pass;
    pass.random = &random;
    pass.sprites = &sprites;
    pass.retainedPose = &pose;
    Client::World::WorldObject object;
    object.type = 0x27;
    const auto stepped = Client::Scenes::StepNoriaObjectFrame(object, 61, pass);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x53fe70"), std::string::npos);
    EXPECT_EQ(random.State(), 0x1b8052fbu); // R4&31==0, antes del quinto flare.
    EXPECT_TRUE(sprites.Sprites()[5].alive);
    EXPECT_FALSE(sprites.Sprites()[6].alive);
}

TEST(NoriaObjectServicesTest, AFailedLaterJointKeepsTheEarlierSparksAndTheirInterleavedRandom)
{
    auto pose = Pose();
    Client::World::FrameSprites sprites;
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Render::Models::CrtRandom random(4);
    Client::Scenes::GameObjectPass pass;
    pass.random = &random;
    pass.sprites = &sprites;
    pass.particles = particles.get();
    pass.retainedPose = &pose;
    int calls = 0;
    pass.createJoint = [&](const Client::World::FishEffectRequest&) -> Client::Core::Expected<void, std::string>
    {
        if (++calls == 3)
            return Client::Core::Unexpected(std::string("joint failure"));
        static_cast<void>(random.Next());
        static_cast<void>(random.Next());
        return {};
    };
    Client::World::WorldObject object;
    object.type = 0x27;
    const auto stepped = Client::Scenes::StepNoriaObjectFrame(object, 61, pass);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_EQ(stepped.Error(), "joint failure");
    EXPECT_EQ(calls, 3);
    EXPECT_TRUE(particles->All()[0].alive);
    EXPECT_TRUE(particles->All()[1].alive);
    EXPECT_FALSE(particles->All()[2].alive);
    Client::Render::Models::CrtRandom expected(4);
    for (int draw = 0; draw < 26; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
}
} // namespace
