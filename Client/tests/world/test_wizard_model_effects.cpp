#include "World/WizardModelEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalRotation.h"
#include "World/GameModelEffects.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"
#include "Data/Terrain/TerrainAttributes.h"

#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::ModelEffect;
using Client::World::ModelEffectArguments;
using Client::World::ModelEffects;
using Client::World::WizardEffectServices;
using Client::World::WizardOwnerView;

constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));

float F32(double value)
{
    return static_cast<float>(value);
}

struct RecordedJoint
{
    std::int32_t type = 0;
    glm::vec3 first{};
    glm::vec3 second{};
    glm::vec3 angles{};
    std::int32_t subtype = 0;
    void* owner = nullptr;
    float scale = 0.0f;
    std::int32_t key = 0;
    std::int32_t target = 0;
};

// Las tablas del mundo en el heap, y los servicios que las prestan.
struct Harness
{
    std::unique_ptr<ModelEffects> effects = std::make_unique<ModelEffects>();
    std::unique_ptr<Client::World::Particles> particles = std::make_unique<Client::World::Particles>();
    std::unique_ptr<Client::World::TerrainLighting> lighting = std::make_unique<Client::World::TerrainLighting>();
    std::unique_ptr<Client::Data::Terrain::TerrainAttributes> attributes =
        std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    Client::World::FrameSprites sprites;
    std::vector<RecordedJoint> joints;
    std::vector<std::int32_t> sounds;
    std::vector<float> hits;
    std::uint8_t depth = 0;
    std::uint8_t count = 7;
    std::int32_t map = 0;
    float shake = 0.0f;
    double ground = 0.0;
    std::optional<WizardOwnerView> owner;
    int hero = 0;

    WizardEffectServices Services()
    {
        return WizardEffectServices{
            .particles = particles.get(),
            .worldClock = 1000.0f,
            .sprites = &sprites,
            .lighting = lighting.get(),
            .effects = effects.get(),
            .terrainHeight = [this](float, float) { return ground; },
            .terrainAttributes = attributes.get(),
            .map = &map,
            .cameraShake = &shake,
            .siegeActive = false,
            .sound = [this](std::int32_t id) { sounds.push_back(id); },
            .joint =
                [this](const Client::World::FishEffectRequest& request) -> Client::Core::Expected<void, std::string>
            {
                joints.push_back({request.type, request.firstPosition, request.secondPosition, request.angles,
                                  request.subtype, request.ownerContext, request.scale, request.key, request.target});
                return {};
            },
            .ownerOf = [this](const void*) { return owner; },
            .hero = &hero,
            .skillHits = [this](const ModelEffect&, float radius) -> Client::Core::Expected<void, std::string>
            {
                hits.push_back(radius);
                return {};
            },
            .ownRecordDepth = &depth,
            .ownRecordCount = &count};
    }

    ModelEffect& At(std::size_t slot)
    {
        return effects->Effects()[slot];
    }

    std::size_t AliveOf(std::int16_t type) const
    {
        std::size_t alive = 0;
        for (const auto& effect : std::as_const(*effects).Effects())
            alive += effect.alive && effect.type == type ? 1U : 0U;
        return alive;
    }

    std::size_t ParticlesOf(std::int32_t type, std::int32_t subtype) const
    {
        std::size_t alive = 0;
        for (const auto& particle : std::as_const(*particles).All())
            alive += particle.alive && particle.type == type && particle.subtype == subtype ? 1U : 0U;
        return alive;
    }
};

ModelEffectArguments Arguments(std::int16_t type, std::int32_t subtype)
{
    return ModelEffectArguments{.type = type,
                                .position = {1000.0f, 2000.0f, 300.0f},
                                .angles = {0.0f, 0.0f, 90.0f},
                                .light = {0.5f, 0.6f, 0.7f},
                                .subtype = subtype};
}

std::size_t Create(Harness& world, const ModelEffectArguments& arguments, CrtRandom& random,
                   glm::vec3* callerLight = nullptr)
{
    const auto created =
        Client::World::CreateWizardModelEffect(*world.effects, arguments, random, world.Services(), callerLight);
    EXPECT_TRUE(created.HasValue()) << (created.HasValue() ? "" : created.Error());
    EXPECT_TRUE(created.HasValue() && created.Value().has_value());
    return created.HasValue() && created.Value() ? *created.Value() : 0;
}

TEST(WizardModelEffectsTest, TheLotHasTheWizardTypesAndTheirStones)
{
    for (const int type :
         {0xbd, 0xc1, 0xc2, 0xc3, 0xc5, 0xc7, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9})
        EXPECT_TRUE(Client::World::IsWizardModelEffect(static_cast<std::int16_t>(type))) << type;
    for (const int type : {0xc0, 0xc4, 0xc8, 0xca, 0xd1, 0xda, 0xf6})
        EXPECT_FALSE(Client::World::IsWizardModelEffect(static_cast<std::int16_t>(type))) << type;
}

TEST(WizardModelEffectsTest, AFullTableCreatesNothingAndDrawsNoRandom)
{
    Harness world;
    for (auto& effect : world.effects->Effects())
        effect.alive = true;
    CrtRandom random(5);
    const auto created =
        Client::World::CreateWizardModelEffect(*world.effects, Arguments(0xc2, 0), random, world.Services());
    ASSERT_TRUE(created.HasValue());
    EXPECT_FALSE(created.Value().has_value());
    EXPECT_EQ(random.State(), CrtRandom(5).State());
}

TEST(WizardModelEffectsTest, TheMeteorFallsFromAboveWithItsTwoDraws)
{
    Harness world;
    CrtRandom random(3);
    CrtRandom oracle(3);
    const std::size_t slot = Create(world, Arguments(0xc2, 0), random);
    const ModelEffect& meteor = world.At(slot);
    EXPECT_EQ(meteor.life, 40);
    EXPECT_EQ(meteor.objectState.blendMesh, 1);
    EXPECT_EQ(meteor.transform.scale, F32(static_cast<double>((oracle.Next() & 7) + 10) * Tenth));
    EXPECT_EQ(meteor.transform.position.x, F32((static_cast<double>(oracle.Next() & 31) + 1000.0) + 130.0));
    EXPECT_EQ(meteor.transform.position.z, 700.0f);
    EXPECT_EQ(meteor.velocity, glm::vec3(0.0f, 0.0f, -50.0f));
    EXPECT_EQ(meteor.transform.angles, glm::vec3(0.0f, 20.0f, 0.0f));
    EXPECT_EQ(random.State(), oracle.State());
}

TEST(WizardModelEffectsTest, TheKeyedMeteorKeepsItsKeyAsSpeedAndLeavesAFireTrail)
{
    Harness world;
    CrtRandom random(9);
    auto arguments = Arguments(0xc2, 6);
    arguments.key = 0xfffe;
    const std::size_t slot = Create(world, arguments, random);
    const ModelEffect& meteor = world.At(slot);
    EXPECT_EQ(meteor.speed, -2.0f);
    EXPECT_EQ(meteor.disturbanceState, 0xffff);
    EXPECT_EQ(meteor.objectState.hiddenMesh, -2);
    EXPECT_EQ(meteor.copiedAngles, glm::vec3(0.0f, 20.0f, 0.0f));
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(9, 5).State());
    ASSERT_EQ(world.joints.size(), 1U);
    EXPECT_EQ(world.joints[0].type, 0x5f2);
    EXPECT_EQ(world.joints[0].scale, 100.0f);
    EXPECT_EQ(world.joints[0].owner, &world.At(slot));
    EXPECT_EQ(world.joints[0].first, meteor.transform.position);
    EXPECT_EQ(world.joints[0].angles, meteor.copiedAngles);
}

TEST(WizardModelEffectsTest, TheIceSmokesThreeTimesAndLightsTheCallersLight)
{
    Harness world;
    Harness reference;
    CrtRandom random(11);
    CrtRandom oracle(11);
    glm::vec3 callerLight(1.0f);
    const std::size_t slot = Create(world, Arguments(0xc1, 1), random, &callerLight);
    const ModelEffect& ice = world.At(slot);
    EXPECT_EQ(ice.life, 20);
    EXPECT_EQ(ice.transform.angles.x, -20.0f);
    EXPECT_EQ(ice.objectState.light, 0.5f);
    EXPECT_EQ(ice.turnDirection, 5.0f);
    EXPECT_EQ(ice.copiedAngles.z, static_cast<float>(oracle.Next() % 360));
    EXPECT_EQ(ice.start, ice.transform.position);
    EXPECT_EQ(ice.light, glm::vec3(0.5f, 0.6f, 0.7f));
    EXPECT_EQ(callerLight, glm::vec3(std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3e99999au),
                                     std::bit_cast<float>(0x3e4ccccdu)));
    for (int puff = 0; puff < 3; ++puff)
    {
        glm::vec3 point{};
        point.x = F32(static_cast<double>((oracle.Next() & 63) - 32) + 1000.0);
        point.y = F32(static_cast<double>((oracle.Next() & 63) - 32) + 2000.0);
        point.z = F32(static_cast<double>((oracle.Next() & 127) + 32) + 300.0);
        ASSERT_TRUE(
            reference.particles->CreateChecked(0x5f2, point, ice.transform.angles, ice.light, 0, 1.0f, 1000.0f, oracle)
                .HasValue());
    }
    EXPECT_EQ(world.ParticlesOf(0x5f2, 0), 3U);
    EXPECT_EQ(random.State(), oracle.State());
}

TEST(WizardModelEffectsTest, TheSnowballStartsAheadAndAboveItsCaster)
{
    Harness world;
    CrtRandom random(1);
    const std::size_t slot = Create(world, Arguments(0xd7, 0), random);
    const ModelEffect& snow = world.At(slot);
    const glm::vec3 offset = Client::Render::Models::OriginalVectorRotate(
        {0.0f, -40.0f, 150.0f}, Client::Render::Models::OriginalAngleMatrix({0.0f, 0.0f, 90.0f}));
    EXPECT_EQ(snow.transform.position.x, F32(static_cast<double>(offset.x) + 1000.0));
    EXPECT_EQ(snow.transform.position.z, F32(static_cast<double>(offset.z) + 300.0));
    EXPECT_EQ(snow.velocity, glm::vec3(0.0f, -40.0f, 10.0f));
    EXPECT_EQ(snow.life, 20);
    EXPECT_EQ(snow.transform.scale, std::bit_cast<float>(0x3f99999au));
    EXPECT_EQ(random.State(), CrtRandom(1).State());
}

TEST(WizardModelEffectsTest, TheHerosHellfireCountsInTheOwnRecordAndHitsAround)
{
    Harness world;
    CrtRandom random(2);
    auto arguments = Arguments(0xcb, 0);
    arguments.owner = &world.hero;
    const std::size_t slot = Create(world, arguments, random);
    EXPECT_EQ(world.At(slot).life, 45);
    EXPECT_EQ(world.At(slot).word8c, 7);
    EXPECT_EQ(world.count, 8);
    ASSERT_EQ(world.hits.size(), 1U);
    EXPECT_EQ(world.hits[0], 300.0f);
    CrtRandom oracle(2);
    oracle.Discard(Client::Render::Models::OwnRecordCloseRandoms);
    EXPECT_EQ(random.State(), oracle.State());
}

TEST(WizardModelEffectsTest, AnotherCastersHellfireLeavesTheRecordAlone)
{
    Harness world;
    CrtRandom random(2);
    int other = 0;
    auto arguments = Arguments(0xcb, 0);
    arguments.owner = &other;
    static_cast<void>(Create(world, arguments, random));
    EXPECT_EQ(world.count, 7);
    EXPECT_TRUE(world.hits.empty());
    EXPECT_EQ(random.State(), CrtRandom(2).State());
}

TEST(WizardModelEffectsTest, TheTwisterSitsOnTheGroundAndCountsInTheOwnRecord)
{
    Harness world;
    world.ground = 123.5;
    CrtRandom random(4);
    const std::size_t slot = Create(world, Arguments(0xcf, 0), random);
    const ModelEffect& storm = world.At(slot);
    EXPECT_EQ(storm.life, 59);
    EXPECT_EQ(storm.transform.position.z, 123.5f);
    EXPECT_EQ(storm.velocity, glm::vec3(0.0f, -10.0f, 0.0f));
    EXPECT_EQ(storm.word8c, 7);
    EXPECT_EQ(world.count, 8);
}

TEST(WizardModelEffectsTest, TheLaserRisesWithItsRedJoint)
{
    Harness world;
    CrtRandom random(4);
    const std::size_t slot = Create(world, Arguments(0xd0, 1), random);
    const ModelEffect& laser = world.At(slot);
    EXPECT_EQ(laser.life, 30);
    EXPECT_EQ(laser.renderFlags, 0x80U);
    EXPECT_EQ(laser.transform.scale, std::bit_cast<float>(0x3fa66666u));
    EXPECT_EQ(laser.transform.position.z, 450.0f);
    EXPECT_EQ(laser.transform.angles, glm::vec3(30.0f, 0.0f, 90.0f));
    EXPECT_EQ(laser.light, glm::vec3(1.0f, 0.0f, 0.0f));
    ASSERT_EQ(world.joints.size(), 1U);
    EXPECT_EQ(world.joints[0].type, 0x61c);
    EXPECT_EQ(world.joints[0].subtype, 1);
    EXPECT_EQ(world.joints[0].scale, 180.0f);
    EXPECT_EQ(world.joints[0].key, -1);
    EXPECT_EQ(world.joints[0].target, -1);
}

TEST(WizardModelEffectsTest, TheShadowRainDropsTenOrbitsFromItsOwner)
{
    Harness world;
    CrtRandom random(4);
    int owner = 0;
    auto arguments = Arguments(0xbd, 0);
    arguments.owner = &owner;
    static_cast<void>(Create(world, arguments, random));
    ASSERT_EQ(world.joints.size(), 10U);
    EXPECT_EQ(world.joints[0].type, 0x60f);
    EXPECT_EQ(world.joints[0].subtype, 0x10);
    EXPECT_EQ(world.joints[0].owner, &owner);
    EXPECT_EQ(world.joints[0].first, glm::vec3(910.0f, 1970.0f, 0.0f));
    EXPECT_EQ(world.joints[9].first, glm::vec3(1018.0f, 2078.0f, 0.0f));
    EXPECT_EQ(world.joints[9].second, glm::vec3(1000.0f, 2000.0f, 300.0f));
    EXPECT_EQ(world.joints[9].scale, 120.0f);
}

TEST(WizardModelEffectsTest, TheSawTurnsAndMovesWithoutRotatingItsVelocity)
{
    Harness world;
    CrtRandom random(6);
    const std::size_t slot = Create(world, Arguments(0xd4, 0), random);
    ModelEffect& saw = world.At(slot);
    const glm::vec3 velocity = saw.velocity;
    const glm::vec3 start = saw.transform.position;
    ASSERT_TRUE(Client::World::StepWizardModelEffect(saw, nullptr, random, world.Services()).HasValue());
    EXPECT_EQ(saw.transform.angles.z, 60.0f);
    EXPECT_EQ(saw.transform.position.x, F32(static_cast<double>(velocity.x) + static_cast<double>(start.x)));
    EXPECT_EQ(saw.transform.position.y, F32(static_cast<double>(velocity.y) + static_cast<double>(start.y)));
    EXPECT_EQ(saw.previousPosition, start);
    EXPECT_EQ(saw.life, 9);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(6, 1).State());
}

TEST(WizardModelEffectsTest, ThePoisonFadesWithItsLife)
{
    Harness world;
    CrtRandom random(6);
    const std::size_t slot = Create(world, Arguments(0xc3, 0), random);
    ModelEffect& poison = world.At(slot);
    ASSERT_TRUE(Client::World::StepWizardModelEffect(poison, nullptr, random, world.Services()).HasValue());
    EXPECT_EQ(poison.alpha, F32(40.0 * Tenth));
    EXPECT_EQ(poison.objectState.light, F32(40.0 * Tenth));
    EXPECT_EQ(poison.life, 39);
}

TEST(WizardModelEffectsTest, TheFollowingMagicAndTheHellGateStayOnTheirOwner)
{
    Harness world;
    world.owner = WizardOwnerView{.alive = true, .type = 0x20a, .position = {50.0f, 60.0f, 70.0f}};
    CrtRandom random(6);
    int owner = 0;
    auto follow = Arguments(0xcd, 0);
    follow.owner = &owner;
    ModelEffect& magic = world.At(Create(world, follow, random));
    auto gate = Arguments(0xc5, 1);
    gate.owner = &owner;
    ModelEffect& hell = world.At(Create(world, gate, random));
    EXPECT_EQ(hell.objectState.blendMesh, -4);
    ASSERT_TRUE(Client::World::StepWizardModelEffect(magic, nullptr, random, world.Services()).HasValue());
    ASSERT_TRUE(Client::World::StepWizardModelEffect(hell, nullptr, random, world.Services()).HasValue());
    // La cola mueve con la velocidad (cero) girada: se quedan donde los puso el caso.
    EXPECT_EQ(magic.transform.position, glm::vec3(50.0f, 60.0f, 170.0f));
    EXPECT_EQ(magic.transform.angles.y, 20.0f);
    EXPECT_EQ(magic.objectState.light, F32(20.0 * Tenth));
    EXPECT_EQ(hell.transform.position, glm::vec3(50.0f, 60.0f, 70.0f));
    world.owner.reset();
    const auto cut = Client::World::StepWizardModelEffect(hell, nullptr, random, world.Services());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5251b6"), std::string::npos);
}

TEST(WizardModelEffectsTest, TheSnowballBreaksOnItsTargetIntoTwoStones)
{
    Harness world;
    world.owner = WizardOwnerView{.alive = true, .position = {1000.0f, 2000.0f, 0.0f}, .seen = true};
    CrtRandom random(8);
    int target = 0;
    auto arguments = Arguments(0xd7, 0);
    arguments.owner = &target;
    arguments.angles = {};
    ModelEffect& snow = world.At(Create(world, arguments, random));
    snow.transform.position = {1030.0f, 2040.0f, 100.0f};
    ASSERT_TRUE(Client::World::StepWizardModelEffect(snow, nullptr, random, world.Services()).HasValue());
    EXPECT_FALSE(snow.alive);
    EXPECT_EQ(world.AliveOf(0xd8) + world.AliveOf(0xd9), 2U);
    EXPECT_EQ(world.ParticlesOf(0x5f2, 0), 2U);
    ASSERT_EQ(world.sounds.size(), 1U);
    EXPECT_EQ(world.sounds[0], 0x44);
}

TEST(WizardModelEffectsTest, TheFireballBreaksOnItsTargetIntoTwoChips)
{
    Harness world;
    world.owner = WizardOwnerView{.alive = true, .position = {1000.0f, 2000.0f, 0.0f}, .seen = true};
    CrtRandom random(8);
    int target = 0;
    auto arguments = Arguments(0xc2, 1);
    arguments.owner = &target;
    ModelEffect& fire = world.At(Create(world, arguments, random));
    ASSERT_TRUE(Client::World::StepWizardModelEffect(fire, nullptr, random, world.Services()).HasValue());
    EXPECT_FALSE(fire.alive);
    EXPECT_EQ(world.AliveOf(0xc8) + world.AliveOf(0xc9), 2U);
    EXPECT_EQ(world.ParticlesOf(0x5d9, 5), 1U);
}

TEST(WizardModelEffectsTest, TheMeteorExplodesOnTheGround)
{
    Harness world;
    world.ground = 1000.0;
    CrtRandom random(10);
    ModelEffect& meteor = world.At(Create(world, Arguments(0xc2, 0), random));
    ASSERT_TRUE(Client::World::StepWizardModelEffect(meteor, nullptr, random, world.Services()).HasValue());
    EXPECT_FALSE(meteor.alive);
    EXPECT_EQ(meteor.transform.position.z, 1000.0f);
    EXPECT_EQ(world.AliveOf(0xc8) + world.AliveOf(0xc9), 6U);
    EXPECT_EQ(world.ParticlesOf(0x5ed, 0), 1U);
    EXPECT_EQ(world.ParticlesOf(0x5d9, 5), 1U);
}

TEST(WizardModelEffectsTest, TheBouncingFireNeedsItsEmberParticle)
{
    Harness world;
    world.ground = -1000.0;
    CrtRandom random(12);
    ModelEffect& fire = world.At(Create(world, Arguments(0xc2, 5), random));
    CrtRandom oracle = random;
    static_cast<void>(oracle.Next());
    const bool ember = oracle.Next() % 5 != 0;
    const auto stepped = Client::World::StepWizardModelEffect(fire, nullptr, random, world.Services());
    if (ember)
    {
        ASSERT_FALSE(stepped.HasValue());
        EXPECT_NE(stepped.Error().find("0x524f05"), std::string::npos);
    }
    else
        EXPECT_TRUE(stepped.HasValue());
    EXPECT_EQ(fire.objectState.hiddenMesh, 0);
    EXPECT_EQ(fire.transform.position.z, 300.0f + 5.0f);
    EXPECT_EQ(fire.turnDirection, 3.0f);
}

TEST(WizardModelEffectsTest, TheStormAndTheMagicCutAtTheirSmoke)
{
    Harness world;
    CrtRandom random(12);
    ModelEffect& storm = world.At(Create(world, Arguments(0xcf, 0), random));
    const auto cut = Client::World::StepWizardModelEffect(storm, nullptr, random, world.Services());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x522dff"), std::string::npos);
    EXPECT_NE(cut.Error().find("(CL11)"), std::string::npos);
    ModelEffect& magic = world.At(Create(world, Arguments(0xce, 0), random));
    const auto smoke = Client::World::StepWizardModelEffect(magic, nullptr, random, world.Services());
    ASSERT_FALSE(smoke.HasValue());
    EXPECT_NE(smoke.Error().find("0x522c0e"), std::string::npos);
}

TEST(WizardModelEffectsTest, TheOuterCircleShakesTheCameraAndGlows)
{
    Harness world;
    CrtRandom random(13);
    ModelEffect& circle = world.At(Create(world, Arguments(0xcc, 0), random));
    CrtRandom oracle = random;
    static_cast<void>(oracle.Next());
    const float shake = F32(static_cast<double>(oracle.Next() % 6 - 6) * Tenth);
    const bool chip = (oracle.Next() & 3) == 0;
    ASSERT_TRUE(Client::World::StepWizardModelEffect(circle, nullptr, random, world.Services()).HasValue());
    EXPECT_EQ(world.shake, shake);
    EXPECT_EQ(circle.objectState.light, 0.0f);
    EXPECT_EQ(circle.objectState.uv.x, F32(40.0 * static_cast<double>(std::bit_cast<float>(0xbc23d70au))));
    EXPECT_EQ(world.AliveOf(0xc8) + world.AliveOf(0xc9), chip ? 1U : 0U);
}

TEST(WizardModelEffectsTest, TheLightningCircleBrightensAndDims)
{
    Harness world;
    CrtRandom random(13);
    ModelEffect& circle = world.At(Create(world, Arguments(0xcb, 2), random));
    ASSERT_TRUE(Client::World::StepWizardModelEffect(circle, nullptr, random, world.Services()).HasValue());
    EXPECT_EQ(circle.objectState.light, F32(0.0 * Tenth));
    circle.life = 100;
    ASSERT_TRUE(Client::World::StepWizardModelEffect(circle, nullptr, random, world.Services()).HasValue());
    EXPECT_EQ(circle.objectState.light, F32(100.0 * Tenth));
}

TEST(WizardModelEffectsTest, TheIceFadesAfterItsFifthFrame)
{
    Harness world;
    CrtRandom random(14);
    ModelEffect& ice = world.At(Create(world, Arguments(0xc1, 0), random));
    ice.animation.action = 0xff;
    ice.animation.frame = 5.0f;
    ASSERT_TRUE(Client::World::StepWizardModelEffect(ice, nullptr, random, world.Services()).HasValue());
    EXPECT_EQ(ice.speed, 0.0f);
    EXPECT_EQ(ice.alpha, F32(1.0 - static_cast<double>(std::bit_cast<float>(0x3d4ccccdu))));
    EXPECT_EQ(ice.life, 49);
}

TEST(WizardModelEffectsTest, TheIceOnAFrozenPlayerHoldsItsFrame)
{
    Harness world;
    world.owner =
        WizardOwnerView{.alive = true, .type = 0x20a, .position = {7.0f, 8.0f, 9.0f}, .flags78 = 0x20, .action = 0x10};
    CrtRandom random(14);
    int owner = 0;
    auto arguments = Arguments(0xc1, 2);
    arguments.owner = &owner;
    ModelEffect& ice = world.At(Create(world, arguments, random));
    ice.animation.action = 0xff;
    ice.animation.frame = 3.0f;
    ASSERT_TRUE(Client::World::StepWizardModelEffect(ice, nullptr, random, world.Services()).HasValue());
    EXPECT_EQ(ice.animation.frame, 0.0f);
    EXPECT_EQ(ice.objectState.hiddenMesh, -2);
    EXPECT_EQ(ice.life, 9);
    world.owner->action = 0x30;
    ASSERT_TRUE(Client::World::StepWizardModelEffect(ice, nullptr, random, world.Services()).HasValue());
    EXPECT_EQ(ice.objectState.hiddenMesh, -1);
    world.owner->flags78 = 0;
    ASSERT_TRUE(Client::World::StepWizardModelEffect(ice, nullptr, random, world.Services()).HasValue());
    EXPECT_EQ(ice.alpha, F32(1.0 - static_cast<double>(std::bit_cast<float>(0x3d4ccccdu))));
}

TEST(WizardModelEffectsTest, TheBoneChipFallsAndDrawsItsTenWithoutFire)
{
    Harness world;
    world.ground = -10000.0;
    CrtRandom random(15);
    ModelEffect& bone = world.At(Create(world, Arguments(0xd5, 0), random));
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(15, 5).State());
    EXPECT_EQ(bone.transform.position.z, 450.0f);
    ASSERT_TRUE(Client::World::StepWizardModelEffect(bone, nullptr, random, world.Services()).HasValue());
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(15, 7).State());
    EXPECT_FALSE(world.particles->Any());
}

TEST(WizardModelEffectsTest, TheLaserAcceleratesBackwards)
{
    Harness world;
    CrtRandom random(15);
    ModelEffect& laser = world.At(Create(world, Arguments(0xd0, 2), random));
    ASSERT_TRUE(Client::World::StepWizardModelEffect(laser, nullptr, random, world.Services()).HasValue());
    EXPECT_EQ(laser.velocity.y, -2.0f);
    EXPECT_EQ(laser.speed, 2.0f);
}
} // namespace
