#include "Math/X87.h"
#include "MapScripts/Icarus/IcarusObjectFrame.h"
#include "Math/RoundedMath.h"
#include "MapScripts/Icarus/IcarusObjectStep.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Kernel/PoseBuffer.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/JointRequest.h"
#include "Particles/KalimaParticleKernels.h"
#include "Effects/ModelEffects.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/DynamicWorldLight.h"
#include "Kernel/WorldObject.h"
#include "Kernel/ObjectPass.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// The Icarus objects (map 10): the case of 0x602ed0 (0x603eef), the prelude 0x6061c0 with 0x605d70, the step
// 0x6063d0 of each seen object and the two joints they release, 0x615/0 and 0x614/6 (06 § Los objetos de
// Icarus). The expected values repeat the ASM's formulas with another CrtRandom of the same seed.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::GameObjectPass;
using Client::Kernel::JointState;
using Client::Kernel::WorldObject;
using Client::MapScripts::Icarus::IcarusObjectServices;
using Client::Math::RoundedSin;
using Client::Particles::Particle;

constexpr std::uint8_t IcarusMap = 10;
constexpr std::int32_t GameScene = 5;

using Client::Math::D;
using Client::Math::F32;

// The first seed with which `condition` accepts the draws of a new CrtRandom.
std::uint32_t SeedWhere(const std::function<bool(CrtRandom&)>& condition)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom random(seed);
        if (condition(random))
            return seed;
    }
}

int Draw(CrtRandom& random)
{
    return random.Next();
}

WorldObject ObjectOf(std::int16_t type)
{
    WorldObject object;
    object.type = type;
    object.identity = 41;
    object.transform.scale = 1.25f;
    object.transform.position = {5000.0f, 6000.0f, 170.0f};
    object.transform.angles = {0.0f, 0.0f, 30.0f};
    object.light = {0.25f, 0.5f, 0.75f};
    return object;
}

struct FrameFixture
{
    std::unique_ptr<Client::Particles::Particles> particles = std::make_unique<Client::Particles::Particles>();
    Client::Kernel::PoseBuffer pose;
    CrtRandom random{7};
    float clock = 2345.0f;

    FrameFixture()
    {
        for (int bone = 0; bone < 4; ++bone)
        {
            Client::Math::BoneTransform matrix(1.0f);
            matrix[3] = glm::vec3(static_cast<float>(bone) * 10.0f, 1.0f, 2.0f);
            pose.bones.push_back(matrix);
        }
    }

    [[nodiscard]] GameObjectPass Pass()
    {
        GameObjectPass pass;
        pass.map = IcarusMap;
        pass.random = &random;
        pass.worldClock = clock;
        pass.particles = particles.get();
        pass.retainedPose = &pose;
        pass.scene = GameScene;
        return pass;
    }

    [[nodiscard]] std::size_t AliveCount() const
    {
        std::size_t alive = 0;
        for (const Particle& particle : particles->All())
        {
            if (particle.alive)
                ++alive;
        }
        return alive;
    }
};

void ExpectSameParticles(const Client::Particles::Particles& actual, const Client::Particles::Particles& expected)
{
    const auto all = const_cast<Client::Particles::Particles&>(actual).All();
    const auto reference = const_cast<Client::Particles::Particles&>(expected).All();
    for (std::size_t slot = 0; slot < all.size(); ++slot)
    {
        SCOPED_TRACE(slot);
        ASSERT_EQ(all[slot].alive, reference[slot].alive);
        if (!all[slot].alive)
            continue;
        EXPECT_EQ(all[slot].type, reference[slot].type);
        EXPECT_EQ(all[slot].subtype, reference[slot].subtype);
        EXPECT_EQ(all[slot].life, reference[slot].life);
        EXPECT_EQ(all[slot].position, reference[slot].position);
        EXPECT_EQ(all[slot].angles, reference[slot].angles);
        EXPECT_EQ(all[slot].light, reference[slot].light);
        EXPECT_EQ(all[slot].scale, reference[slot].scale);
        EXPECT_EQ(all[slot].rotationFactor, reference[slot].rotationFactor);
        EXPECT_EQ(all[slot].typedOwner, reference[slot].typedOwner);
    }
}

// A joint requested from 0x530860, copied (the request only lends its vectors).
struct JoinedRequest
{
    std::int32_t type;
    glm::vec3 first;
    glm::vec3 second;
    glm::vec3 angles;
    std::int32_t subtype;
    void* owner;
    float width;
    std::int32_t key;
    std::int32_t target;
};

struct StepFixture
{
    CrtRandom random;
    glm::vec3 hero{12800.0f, 12900.0f, 300.0f};
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    std::unique_ptr<Client::Effects::ModelEffects> effects = std::make_unique<Client::Effects::ModelEffects>();
    std::unique_ptr<Client::Particles::FrameSprites> sprites = std::make_unique<Client::Particles::FrameSprites>();
    std::vector<JoinedRequest> joins;

    explicit StepFixture(std::uint32_t seed) : random(seed) {}

    [[nodiscard]] IcarusObjectServices Services()
    {
        return IcarusObjectServices{
            .random = random,
            .heroPosition = hero,
            .lighting = lighting.get(),
            .effects = effects.get(),
            .sprites = sprites.get(),
            .createJoint =
                [this](const Client::Kernel::JointRequest& request) -> Client::Core::Expected<void, std::string>
            {
                joins.push_back(JoinedRequest{request.type, request.firstPosition, request.secondPosition,
                                              request.angles, request.subtype, request.ownerContext, request.scale,
                                              request.key, request.target});
                return {};
            }};
    }

    [[nodiscard]] std::vector<const Client::Effects::ModelEffect*> AliveEffects() const
    {
        std::vector<const Client::Effects::ModelEffect*> alive;
        for (const auto& effect : effects->Effects())
        {
            if (effect.alive)
                alive.push_back(&effect);
        }
        return alive;
    }
};
} // namespace

// 0x603f03..0x6040db: from 0 to 2, twenty 0x622 clouds with the type's subtype; from 3 to 5, ten. With the object
// as owner, light 0.1f and the object's scale, and +0x58 is left at −2: the second time it neither emits nor draws.
TEST(IcarusObjectsTest, TheCloudsEmitTwentyOrTenOwnedMistsOnlyOnce)
{
    for (const std::int16_t type : {std::int16_t{1}, std::int16_t{4}})
    {
        SCOPED_TRACE(type);
        FrameFixture fixture;
        WorldObject object = ObjectOf(type);
        ASSERT_TRUE(Client::MapScripts::Icarus::StepIcarusObjectFrame(object, fixture.Pass()).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::Kernel::HiddenObject);
        EXPECT_EQ(fixture.AliveCount(), type <= 2 ? 20u : 10u);

        auto reference = std::make_unique<Client::Particles::Particles>();
        CrtRandom draws(7);
        const Client::Particles::ParticleOwner owner{Client::Particles::ParticleOwnerKind::WorldObject,
                                                     object.identity};
        for (std::size_t cloud = 0; cloud < fixture.AliveCount(); ++cloud)
        {
            ASSERT_TRUE(reference
                            ->CreateOwnedChecked(Client::Particles::CloudParticle, object.transform.position,
                                                 object.transform.angles, glm::vec3(std::bit_cast<float>(0x3dcccccdu)),
                                                 type, 1.25f, fixture.clock, draws, GameScene, owner)
                            .HasValue());
        }
        ExpectSameParticles(*fixture.particles, *reference);
        EXPECT_EQ(fixture.random.State(), draws.State());

        ASSERT_TRUE(Client::MapScripts::Icarus::StepIcarusObjectFrame(object, fixture.Pass()).HasValue());
        EXPECT_EQ(fixture.AliveCount(), type <= 2 ? 20u : 10u);
        EXPECT_EQ(fixture.random.State(), draws.State());
    }
}

// 0x6040e9..0x604143: the 10 leaves a 0x5ac/0 at the point of bone 3 of the global pose, with no owner.
TEST(IcarusObjectsTest, TheFlareObjectSendsAKalimaFlareFromItsThirdBone)
{
    FrameFixture fixture;
    WorldObject object = ObjectOf(10);
    ASSERT_TRUE(Client::MapScripts::Icarus::StepIcarusObjectFrame(object, fixture.Pass()).HasValue());
    ASSERT_EQ(fixture.AliveCount(), 1u);
    auto reference = std::make_unique<Client::Particles::Particles>();
    CrtRandom draws(7);
    ASSERT_TRUE(reference
                    ->CreateOwnedChecked(Client::Particles::KalimaFlare, {30.0f, 1.0f, 2.0f}, object.transform.angles,
                                         glm::vec3(1.0f), 0, 1.0f, fixture.clock, draws, GameScene, {})
                    .HasValue());
    ExpectSameParticles(*fixture.particles, *reference);
    EXPECT_EQ(fixture.particles->All()[0].position, glm::vec3(30.0f, 1.0f, 2.0f));
    EXPECT_EQ(fixture.random.State(), draws.State());
    EXPECT_EQ(object.state.hiddenMesh, WorldObject{}.state.hiddenMesh);
}

// From 6 to 9, table 0x60466c does nothing; a cloud type without a pool cuts with 0x603f3b without hiding.
TEST(IcarusObjectsTest, TheOtherTypesDoNothingAndTheCloudsNeedThePool)
{
    FrameFixture fixture;
    for (std::int16_t type = 6; type <= 9; ++type)
    {
        WorldObject object = ObjectOf(type);
        ASSERT_TRUE(Client::MapScripts::Icarus::StepIcarusObjectFrame(object, fixture.Pass()).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, WorldObject{}.state.hiddenMesh);
    }
    EXPECT_EQ(fixture.AliveCount(), 0u);
    EXPECT_EQ(fixture.random.State(), CrtRandom(7).State());

    WorldObject cloud = ObjectOf(0);
    GameObjectPass pass = fixture.Pass();
    pass.particles = nullptr;
    const auto cut = Client::MapScripts::Icarus::StepIcarusObjectFrame(cloud, pass);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x603f3b"), std::string::npos) << cut.Error();
    EXPECT_EQ(cloud.state.hiddenMesh, WorldObject{}.state.hiddenMesh);
}

// 0x6063d0: the selector at zero only adds to the count; without the 0 of rand() % 10, or with a type above 5, the
// count goes back down after a rand().
TEST(IcarusObjectsTest, TheSeenObjectCountsAndOnlySparksWithItsTenth)
{
    StepFixture fixture(SeedWhere([](CrtRandom& random) { return Draw(random) % 10 != 0; }));
    const auto services = fixture.Services();
    WorldObject object = ObjectOf(2);
    std::int32_t count = 4;
    const auto before = fixture.random.State();
    ASSERT_TRUE(Client::MapScripts::Icarus::StepIcarusSeenObject(object, 0, count, services).HasValue());
    EXPECT_EQ(count, 5);
    EXPECT_EQ(fixture.random.State(), before);

    CrtRandom draws = fixture.random;
    static_cast<void>(draws.Next());
    ASSERT_TRUE(Client::MapScripts::Icarus::StepIcarusSeenObject(object, 3, count, services).HasValue());
    EXPECT_EQ(count, 5);
    EXPECT_EQ(fixture.random.State(), draws.State());

    StepFixture other(SeedWhere([](CrtRandom& random) { return Draw(random) % 10 == 0; }));
    WorldObject big = ObjectOf(6);
    ASSERT_TRUE(Client::MapScripts::Icarus::StepIcarusSeenObject(big, 3, count, other.Services()).HasValue());
    EXPECT_EQ(count, 5);
    EXPECT_FALSE(other.sprites->Any());
    EXPECT_TRUE(other.joins.empty());
}

// 0x60642b..0x60655b: the sprite 0x623 of scale 0.5 with the light (rand() % 10) · 0.02f per channel and two
// 0x614/6 of the object, with its angles and of width rand() % 20 + 10. The count stays added.
TEST(IcarusObjectsTest, TheSeenObjectSparksASpriteAndTwoBoltsOfItsOwn)
{
    const std::uint32_t seed = SeedWhere([](CrtRandom& random) { return Draw(random) % 10 == 0; });
    StepFixture fixture(seed);
    WorldObject object = ObjectOf(3);
    std::int32_t count = 2;
    ASSERT_TRUE(Client::MapScripts::Icarus::StepIcarusSeenObject(object, 1, count, fixture.Services()).HasValue());
    EXPECT_EQ(count, 3);

    CrtRandom draws(seed);
    static_cast<void>(draws.Next());
    constexpr double Fiftieth = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));
    glm::vec3 light;
    light.x = F32((draws.Next() % 10) * Fiftieth);
    light.y = F32((draws.Next() % 10) * Fiftieth);
    light.z = F32((draws.Next() % 10) * Fiftieth);
    ASSERT_TRUE(fixture.sprites->Any());
    const auto& sprite = fixture.sprites->Sprites()[0];
    EXPECT_EQ(sprite.texture, 0x623);
    EXPECT_EQ(sprite.position, object.transform.position);
    EXPECT_EQ(sprite.scale, 0.5f);
    EXPECT_EQ(sprite.light, light);
    EXPECT_EQ(sprite.rotation, 0.0f);
    ASSERT_EQ(fixture.joins.size(), 2u);
    for (const JoinedRequest& joined : fixture.joins)
    {
        EXPECT_EQ(joined.type, 0x614);
        EXPECT_EQ(joined.subtype, 6);
        EXPECT_EQ(joined.first, object.transform.position);
        EXPECT_EQ(joined.second, object.transform.position);
        EXPECT_EQ(joined.angles, object.transform.angles);
        EXPECT_EQ(joined.owner, &object);
        EXPECT_EQ(joined.width, F32(draws.Next() % 20 + 10.0));
        EXPECT_EQ(joined.key, -1);
        EXPECT_EQ(joined.target, -1);
    }
    EXPECT_EQ(fixture.random.State(), draws.State());
}

// 0x605d70 and 0x6061c0 without their two gates: one rand() each and nothing else.
TEST(IcarusObjectsTest, ThePreludeWithoutItsGatesOnlyDraws)
{
    const std::uint32_t seed =
        SeedWhere([](CrtRandom& random) { return Draw(random) % 50 != 0 && Draw(random) % 10 != 0; });
    StepFixture fixture(seed);
    const auto selector = Client::MapScripts::Icarus::StepIcarusObjectPrelude(9, fixture.Services());
    ASSERT_TRUE(selector.HasValue()) << selector.Error();
    EXPECT_EQ(selector.Value(), 0);
    EXPECT_TRUE(fixture.AliveEffects().empty());
    EXPECT_TRUE(fixture.joins.empty());
    CrtRandom draws(seed);
    static_cast<void>(draws.Next());
    static_cast<void>(draws.Next());
    EXPECT_EQ(fixture.random.State(), draws.State());
}

// 0x605d91..0x605e49: the dynamic light of radius 2 at (hero x + rand() % 300 − 150; hero y), a discarded rand(),
// L = ((rand() & 3) + 4) · 0.05f and the color (L · 0.3f; L · 0.3f; L · 0.081f); the cloud 0xbb on the hero with
// that color; the selector rand() % count.
TEST(IcarusObjectsTest, ThePreludeLightsTheSkyAndPicksTheSelector)
{
    const std::uint32_t seed = SeedWhere(
        [](CrtRandom& random)
        {
            if (Draw(random) % 50 != 0)
                return false;
            for (int draw = 0; draw < 4; ++draw)
                static_cast<void>(Draw(random));
            return Draw(random) % 5 != 0 && Draw(random) % 10 != 0;
        });
    StepFixture fixture(seed);
    const auto selector = Client::MapScripts::Icarus::StepIcarusObjectPrelude(7, fixture.Services());
    ASSERT_TRUE(selector.HasValue()) << selector.Error();

    CrtRandom draws(seed);
    static_cast<void>(draws.Next());
    const float lightX = F32((draws.Next() % 300 + D(fixture.hero.x)) - 150.0);
    static_cast<void>(draws.Next());
    const double level = ((draws.Next() & 3) + 4) * static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
    const float redGreen = F32(level * static_cast<double>(std::bit_cast<float>(0x3e99999au)));
    const glm::vec3 color{redGreen, redGreen, F32(level * static_cast<double>(std::bit_cast<float>(0x3da5e354u)))};
    EXPECT_EQ(selector.Value(), draws.Next() % 7);
    static_cast<void>(draws.Next());
    static_cast<void>(draws.Next());
    EXPECT_EQ(fixture.random.State(), draws.State());

    auto lit = std::make_unique<Client::Terrain::TerrainLighting>();
    ASSERT_TRUE(
        Client::Terrain::AddDynamicLight(lit.get(), {lightX, fixture.hero.y, 0.0f}, color, 2, "test").HasValue());
    EXPECT_TRUE(lit->cells == fixture.lighting->cells);

    const auto alive = fixture.AliveEffects();
    ASSERT_EQ(alive.size(), 1u);
    const auto& cloud = *alive[0];
    EXPECT_EQ(cloud.type, Client::Effects::IcarusCloudEffect);
    EXPECT_EQ(cloud.transform.position, glm::vec3(12800.0f, 13100.0f, 110.0f));
    EXPECT_EQ(cloud.previousPosition, cloud.transform.position);
    EXPECT_EQ(cloud.light, color);
    EXPECT_EQ(cloud.transform.scale, 10.0f);
    EXPECT_EQ(cloud.life, 2);
    EXPECT_EQ(cloud.objectState.blendMesh, 0);
    EXPECT_FALSE(cloud.vertexLight);
    EXPECT_TRUE(fixture.joins.empty());
}

// 0x605e9a..0x606135: with rand() % 5 == 0, one side of four (rand() & 3). Its offset turns −45° in place and is
// added to the hero (side 1 subtracts it); the reach turns with the side's yaw, also in place. The two 0x615/0 go
// from center + reach to center − reach, 300 lower, with no owner or turn.
TEST(IcarusObjectsTest, ThePreludeThrowsTwoSkyBoltsFromOneOfFourSides)
{
    struct Side
    {
        glm::vec3 first;
        bool subtract;
        float yaw;
        glm::vec3 second;
    };
    constexpr std::array<Side, 4> Sides{{
        {{-400.0f, -1000.0f, 0.0f}, false, 240.0f, {-200.0f, -1000.0f, 0.0f}},
        {{-300.0f, -400.0f, 0.0f}, true, 210.0f, {-500.0f, -1000.0f, 0.0f}},
        {{-200.0f, -400.0f, 0.0f}, false, 235.0f, {-1000.0f, -1500.0f, 0.0f}},
        {{-200.0f, 400.0f, 0.0f}, false, 200.0f, {-600.0f, -1200.0f, 0.0f}},
    }};
    for (int wanted = 0; wanted < 4; ++wanted)
    {
        SCOPED_TRACE(wanted);
        const std::uint32_t seed = SeedWhere(
            [wanted](CrtRandom& random)
            {
                if (Draw(random) % 50 != 0)
                    return false;
                for (int draw = 0; draw < 3; ++draw)
                    static_cast<void>(Draw(random));
                if (Draw(random) % 5 != 0 || (Draw(random) & 3) != wanted)
                    return false;
                static_cast<void>(Draw(random));
                static_cast<void>(Draw(random));
                return Draw(random) % 10 != 0;
            });
        StepFixture fixture(seed);
        const auto selector = Client::MapScripts::Icarus::StepIcarusObjectPrelude(0, fixture.Services());
        ASSERT_TRUE(selector.HasValue()) << selector.Error();
        EXPECT_EQ(selector.Value(), 0);
        ASSERT_EQ(fixture.joins.size(), 2u);

        const Side& side = Sides[static_cast<std::size_t>(wanted)];
        glm::vec3 offset = side.first;
        Client::Math::OriginalVectorRotateInPlace(offset, Client::Math::OriginalAngleMatrix({0.0f, 0.0f, -45.0f}));
        const glm::vec3& hero = fixture.hero;
        glm::vec3 center;
        for (int axis = 0; axis < 3; ++axis)
            center[axis] = side.subtract ? F32(D(hero[axis]) - D(offset[axis])) : F32(D(offset[axis]) + D(hero[axis]));
        glm::vec3 reach = side.second;
        Client::Math::OriginalVectorRotateInPlace(reach, Client::Math::OriginalAngleMatrix({0.0f, 0.0f, side.yaw}));
        const glm::vec3 far{F32(D(center.x) + D(reach.x)), F32(D(center.y) + D(reach.y)),
                            F32((D(center.z) + D(reach.z)) - 300.0)};
        const glm::vec3 near{F32(D(center.x) - D(reach.x)), F32(D(center.y) - D(reach.y)),
                             F32((D(center.z) - D(reach.z)) - 300.0)};

        CrtRandom draws(seed);
        for (int draw = 0; draw < 6; ++draw)
            static_cast<void>(draws.Next());
        for (const JoinedRequest& joined : fixture.joins)
        {
            EXPECT_EQ(joined.type, 0x615);
            EXPECT_EQ(joined.subtype, 0);
            EXPECT_EQ(joined.first, far);
            EXPECT_EQ(joined.second, near);
            EXPECT_EQ(joined.angles, glm::vec3(0.0f));
            EXPECT_EQ(joined.owner, nullptr);
            EXPECT_EQ(joined.width, F32(draws.Next() % 10 + 40.0));
        }
        static_cast<void>(draws.Next());
        EXPECT_EQ(fixture.random.State(), draws.State());
    }
}

// 0x60623b..0x6062d9: with rand() % 10 == 0 in 0x6061c0, the 0x5ac at ±2500 from the hero and 1000 lower.
TEST(IcarusObjectsTest, ThePreludeSometimesDropsAFlareAroundTheHero)
{
    const std::uint32_t seed =
        SeedWhere([](CrtRandom& random) { return Draw(random) % 50 != 0 && Draw(random) % 10 == 0; });
    StepFixture fixture(seed);
    ASSERT_TRUE(Client::MapScripts::Icarus::StepIcarusObjectPrelude(3, fixture.Services()).HasValue());
    CrtRandom draws(seed);
    static_cast<void>(draws.Next());
    static_cast<void>(draws.Next());
    const float x = F32((draws.Next() % 5000 - 2500) + D(fixture.hero.x));
    const float y = F32((draws.Next() % 5000 - 2500) + D(fixture.hero.y));
    const int speed = draws.Next() % 5 + 9;
    EXPECT_EQ(fixture.random.State(), draws.State());
    const auto alive = fixture.AliveEffects();
    ASSERT_EQ(alive.size(), 1u);
    EXPECT_EQ(alive[0]->type, Client::Effects::IcarusFlareEffect);
    EXPECT_EQ(alive[0]->transform.position, glm::vec3(x, y, -700.0f));
    EXPECT_EQ(alive[0]->transform.angles, glm::vec3(0.0f));
    EXPECT_EQ(alive[0]->velocity.z, F32(RoundedSin(std::bit_cast<double>(0x3ff38c3540000000ull)) * (speed * 0.5)));
}

// Without the joint table, the prelude cuts with 0x530860 after the light and the cloud.
TEST(IcarusObjectsTest, ThePreludeNeedsTheJointTable)
{
    const std::uint32_t seed = SeedWhere(
        [](CrtRandom& random)
        {
            if (Draw(random) % 50 != 0)
                return false;
            for (int draw = 0; draw < 3; ++draw)
                static_cast<void>(Draw(random));
            return Draw(random) % 5 == 0;
        });
    StepFixture fixture(seed);
    auto services = fixture.Services();
    services.createJoint = {};
    const auto cut = Client::MapScripts::Icarus::StepIcarusObjectPrelude(0, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x530860"), std::string::npos) << cut.Error();
    EXPECT_EQ(fixture.AliveEffects().size(), 1u);
}

// 0x5319ea..0x531a30: the 0x615/0 takes texture 0x614, the width, 50 points, life 20, the speed rand() % 20 + 80
// and the light's z at 50; stores the start in +0x24. One rand().
TEST(IcarusObjectsTest, TheSkyBoltIsCreatedWithOneDraw)
{
    std::array<JointState, 1> joints{};
    glm::vec3 first{100.0f, 200.0f, 300.0f};
    glm::vec3 second{-100.0f, -200.0f, -300.0f};
    glm::vec3 angles{};
    const Client::Kernel::JointRequest request{0x615, first, second, angles, 0, nullptr, 43.0f, -1, 0, 0, -1};
    CrtRandom random(11);
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, random).HasValue());
    CrtRandom draws(11);
    const JointState& joint = joints[0];
    EXPECT_TRUE(joint.alive);
    EXPECT_EQ(joint.texture, 0x614);
    EXPECT_EQ(joint.width, 43.0f);
    EXPECT_EQ(joint.maximumCount, 50);
    EXPECT_EQ(joint.life, 20);
    EXPECT_EQ(joint.speed, static_cast<float>(draws.Next() % 20 + 80));
    EXPECT_EQ(joint.light, glm::vec3(1.0f, 1.0f, 50.0f));
    EXPECT_EQ(joint.position, first);
    EXPECT_EQ(joint.pathIncrement, first);
    EXPECT_EQ(joint.targetPosition, second);
    EXPECT_TRUE(joint.tailEnabled);
    EXPECT_EQ(random.State(), draws.State());
}

// 0x53727d..0x537408: with life above 15, the 0x615/0 goes back to +0x24 and rebuilds its 50 segments (two rand()
// of the destination and three per segment); with 15 or less, only the light's z drops 10.12f. The common tail goes
// on with life above 10.
TEST(IcarusObjectsTest, TheSkyBoltRebuildsUntilItFades)
{
    std::array<JointState, 1> joints{};
    glm::vec3 first{100.0f, 200.0f, 300.0f};
    glm::vec3 second{-100.0f, -200.0f, -300.0f};
    glm::vec3 angles{};
    const Client::Kernel::JointRequest request{0x615, first, second, angles, 0, nullptr, 43.0f, -1, 0, 0, -1};
    CrtRandom random(11);
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, random).HasValue());
    JointState& joint = joints[0];
    CrtRandom draws = random;
    for (int draw = 0; draw < 2 + 50 * 3; ++draw)
        static_cast<void>(draws.Next());
    const auto stepped = Client::Joints::StepJoints(joints, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), draws.State());
    EXPECT_EQ(joint.life, 19);
    EXPECT_EQ(joint.count, 49);
    EXPECT_TRUE(joint.tailEnabled);
    EXPECT_EQ(joint.light.z, 50.0f);
    EXPECT_EQ(joint.pathIncrement, first);

    joint.life = 15;
    const glm::vec3 position = joint.position;
    const auto faded = Client::Joints::StepJoints(joints, random);
    ASSERT_TRUE(faded.HasValue()) << faded.Error();
    EXPECT_EQ(random.State(), draws.State());
    EXPECT_EQ(joint.light.z, F32(50.0 - D(std::bit_cast<float>(0x4121eb85u))));
    EXPECT_EQ(joint.life, 14);
    EXPECT_TRUE(joint.tailEnabled);
    EXPECT_NE(joint.position, position);

    joint.life = 10;
    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
    EXPECT_FALSE(joint.tailEnabled);
}

// 0x531563..0x53168c: an object's 0x614/6: speed rand() % 10 + 15, life rand() % 20 + 6, the grey
// (rand() % 10) · 0.06666667f + 0.1f, the width and 50 points, no common tail. The owner stays as context.
TEST(IcarusObjectsTest, TheObjectBoltIsCreatedWithThreeDraws)
{
    std::array<JointState, 1> joints{};
    WorldObject object = ObjectOf(2);
    glm::vec3 first = object.transform.position;
    glm::vec3 second = object.transform.position;
    glm::vec3 angles = object.transform.angles;
    const Client::Kernel::JointRequest request{0x614, first, second, angles, 6, &object, 17.0f, -1, 0, 0, -1};
    CrtRandom random(13);
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, random).HasValue());
    CrtRandom draws(13);
    const JointState& joint = joints[0];
    EXPECT_EQ(joint.life, draws.Next() % 20 + 6);
    EXPECT_EQ(joint.speed, static_cast<float>(draws.Next() % 10 + 15));
    const float gray =
        F32((draws.Next() % 10) * D(std::bit_cast<float>(0x3d888889u)) + D(std::bit_cast<float>(0x3dcccccdu)));
    EXPECT_EQ(joint.light, glm::vec3(gray));
    EXPECT_EQ(joint.width, 17.0f);
    EXPECT_EQ(joint.maximumCount, 50);
    EXPECT_FALSE(joint.tailEnabled);
    EXPECT_EQ(joint.ownerContext, &object);
    EXPECT_EQ(joint.position, object.transform.position);
    EXPECT_EQ(joint.pathIncrement, object.transform.position);
    EXPECT_EQ(random.State(), draws.State());
}

// 0x53683e..0x536e8a: with life above 4 the 0x614/6 only advances; with 4 or less it rebuilds the 50 segments from
// +0x24 toward the owner object (five rand() per segment), with the last destination 2050..2249 beyond in x and
// in y and 10000 lower.
TEST(IcarusObjectsTest, TheObjectBoltRebuildsTowardsItsOwnerAtTheEnd)
{
    std::array<JointState, 1> joints{};
    WorldObject object = ObjectOf(2);
    glm::vec3 first = object.transform.position;
    glm::vec3 second = object.transform.position;
    glm::vec3 angles = object.transform.angles;
    const Client::Kernel::JointRequest request{0x614, first, second, angles, 6, &object, 17.0f, -1, 0, 0, -1};
    CrtRandom random(13);
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, random).HasValue());
    JointState& joint = joints[0];
    joint.life = 6;
    CrtRandom draws = random;
    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
    EXPECT_EQ(random.State(), draws.State());
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.life, 5);

    joint.life = 4;
    for (int draw = 0; draw < 50 * 5; ++draw)
        static_cast<void>(draws.Next());
    const auto rebuilt = Client::Joints::StepJoints(joints, random);
    ASSERT_TRUE(rebuilt.HasValue()) << rebuilt.Error();
    EXPECT_EQ(random.State(), draws.State());
    EXPECT_EQ(joint.count, 49);
    EXPECT_EQ(joint.life, 3);
    EXPECT_GE(joint.targetPosition.x, 5000.0f + 2050.0f);
    EXPECT_LT(joint.targetPosition.x, 5000.0f + 2250.0f);
    EXPECT_GE(joint.targetPosition.y, 6000.0f + 2050.0f);
    EXPECT_LT(joint.targetPosition.y, 6000.0f + 2250.0f);
    EXPECT_EQ(joint.targetPosition.z, F32(170.0 - 10000.0));

    joint.life = 4;
    joint.ownerContext = nullptr;
    const auto cut = Client::Joints::StepJoints(joints, random);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x536a15"), std::string::npos) << cut.Error();
}
