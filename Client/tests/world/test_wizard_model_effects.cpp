#include "World/WizardModelEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalRotation.h"
#include "World/GameModelEffects.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainLight.h"

#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Client::Crt::CrtRandom;
using Client::World::ModelEffect;
using Client::World::ModelEffectArguments;
using Client::World::ModelEffects;
using Client::World::WizardEffectServices;
using Client::World::WizardOwnerView;

constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));

using Client::Math::F32;

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
    // Lo que devuelve el empujón (0x524a8d): un error del servicio corta el paso ahí.
    std::string pushFailure;
    std::vector<std::pair<float, float>> pushes;
    std::vector<ModelEffectArguments> others;

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
            .ownRecordCount = &count,
            .otherEffect = [this](const ModelEffectArguments& arguments) -> Client::Core::Expected<void, std::string>
            {
                others.push_back(arguments);
                return {};
            },
            .pushAround = [this](float x, float y) -> Client::Core::Expected<void, std::string>
            {
                pushes.emplace_back(x, y);
                if (!pushFailure.empty())
                    return Client::Core::Unexpected(pushFailure);
                return {};
            }};
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

// 0x525438..0x5254f9: con dueño y vida por encima de 10, el paso del 0xbd/0 saca (rand() & 127) − 64 y suelta la
// 0x5fc/2 a (r − 50; r + 50; 360) del dueño, con la escala (rand() % 30 + 50) · 0,01f.
TEST(WizardModelEffectsTest, TheShadowRainDropsAFallingShineNearItsOwner)
{
    Harness world;
    world.owner = WizardOwnerView{.alive = true, .position = {500.0f, 600.0f, 70.0f}};
    CrtRandom random(4);
    int owner = 0;
    auto arguments = Arguments(0xbd, 0);
    arguments.owner = &owner;
    ModelEffect& rain = world.At(Create(world, arguments, random));
    CrtRandom oracle = random;
    const auto stepped = Client::World::StepWizardModelEffect(rain, nullptr, random, world.Services());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(world.ParticlesOf(0x5fc, 2), 1U);
    static_cast<void>(oracle.Next()); // 0x5208c5, la luz del paso.
    const int spread = (oracle.Next() & 127) - 64;
    const float scale = F32(static_cast<double>(oracle.Next() % 30 + 50) * std::bit_cast<float>(0x3c23d70au));
    const glm::vec3 point{static_cast<float>(spread - 50) + 500.0f, static_cast<float>(spread + 50) + 600.0f, 430.0f};
    auto twin = std::make_unique<Client::World::Particles>();
    ASSERT_TRUE(
        twin->CreateChecked(0x5fc, point, rain.transform.angles, rain.light, 2, scale, 1000.0f, oracle).HasValue());
    EXPECT_EQ(random.State(), oracle.State());
    const auto shine =
        std::ranges::find_if(std::as_const(*world.particles).All(), [](const Client::World::Particle& particle)
                             { return particle.alive && particle.type == 0x5fc; });
    const Client::World::Particle& reference = twin->All()[0];
    EXPECT_EQ(shine->start, point);
    EXPECT_EQ(shine->scale, scale);
    EXPECT_EQ(shine->light, rain.light);
    EXPECT_EQ(shine->rotation, reference.rotation);
    EXPECT_EQ(shine->life, 25);
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

// 0x524959..0x5249a0: el meteoro 0xc2/9 del mapa 55 suelta en cada paso sus dos humos 0x5f2/0x24 y /0x25 con él de
// dueño (push ebx); la 0x24 lo sigue (ParticleOwnerStatesOf lo busca por su lugar en la tabla), la 0x25 no lo lee.
TEST(WizardModelEffectsTest, TheMapMeteorTrailsTwoSmokesOwnedByItself)
{
    Harness world;
    world.ground = -1000.0;
    CrtRandom random(14);
    const std::size_t slot = Create(world, Arguments(0xc2, 9), random);
    ModelEffect& meteor = world.At(slot);
    const auto stepped = Client::World::StepWizardModelEffect(meteor, nullptr, random, world.Services());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(meteor.alive);
    EXPECT_EQ(world.ParticlesOf(0x5f2, 0x24), 1U);
    EXPECT_EQ(world.ParticlesOf(0x5f2, 0x25), 1U);
    for (const auto& particle : std::as_const(*world.particles).All())
    {
        if (!particle.alive || particle.type != 0x5f2 || particle.subtype != 0x24)
            continue;
        EXPECT_EQ(particle.typedOwner,
                  (Client::World::ParticleOwner{Client::World::ParticleOwnerKind::ModelEffect, slot}));
    }

    // Fuera de la tabla no hay de dónde sacar el dueño: corta en la primera.
    Harness other;
    other.ground = -1000.0;
    CrtRandom again(14);
    ModelEffect loose = world.At(slot);
    const auto cut = Client::World::StepWizardModelEffect(loose, nullptr, again, other.Services());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52497d"), std::string::npos) << cut.Error();
}

// 0x524c64..0x524e95: el 0xc2/6, /7 y /8 que pasa el suelo se apoya en él y suelta el 0xf6/2 (0x524d38: en su punto,
// ángulos 0, luz (0; 0,5; 0), el meteoro de dueño y la clave 30), los humos de piedra 5f2/11 (15, 7 o 5), las seis
// piedritas sólo el /6, el sonido 0x8f y la explosión 5ed/0; después, su brillo (0x524e99).
TEST(WizardModelEffectsTest, TheGreenMeteorDropsItsBurstWhenItLands)
{
    struct Case
    {
        std::int32_t subtype;
        std::size_t dust;
        std::size_t chips;
    };
    for (const Case& tested : {Case{6, 15, 6}, Case{7, 7, 0}, Case{8, 5, 0}})
    {
        SCOPED_TRACE(tested.subtype);
        Harness world;
        CrtRandom random(21);
        const std::size_t slot = Create(world, Arguments(0xc2, tested.subtype), random);
        ModelEffect& meteor = world.At(slot);
        world.ground = static_cast<double>(meteor.transform.position.z) + 5000.0;
        const auto stepped = Client::World::StepWizardModelEffect(meteor, nullptr, random, world.Services());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_FALSE(meteor.alive);
        ASSERT_EQ(world.others.size(), 1U);
        const ModelEffectArguments& burst = world.others[0];
        EXPECT_EQ(burst.type, 0xf6);
        EXPECT_EQ(burst.subtype, 2);
        EXPECT_EQ(burst.owner, &meteor);
        EXPECT_EQ(burst.key, 30);
        EXPECT_EQ(burst.angles, glm::vec3(0.0f));
        EXPECT_EQ(burst.light, glm::vec3(0.0f, 0.5f, 0.0f));
        EXPECT_EQ(burst.position.z, static_cast<float>(world.ground));
        EXPECT_EQ(burst.position.x, meteor.previousPosition.x);
        EXPECT_EQ(world.ParticlesOf(0x5f2, 11), tested.dust);
        EXPECT_EQ(world.AliveOf(0xc8) + world.AliveOf(0xc9), tested.chips);
        EXPECT_EQ(world.ParticlesOf(0x5ed, 0), 1U);
        ASSERT_FALSE(world.sounds.empty());
        EXPECT_EQ(world.sounds.back(), 0x8f);
    }
}

// 0x524a5f..0x524b2a: el 0xc2/4 que baja del suelo tiembla la cámara, empuja a los personajes del pool que estén
// cerca (el servicio, en su punto) y cae en el suelo como el meteoro (0x524b27 → 0x524c64), haya empujado o no: seis
// piedritas, la explosión y muere. Un error del empujón corta ahí, con el efecto vivo.
TEST(WizardModelEffectsTest, TheFallingFirePushesAndLands)
{
    for (const bool fails : {false, true})
    {
        SCOPED_TRACE(fails);
        Harness world;
        world.ground = 1000.0;
        if (fails)
            world.pushFailure = "0x524afc: prueba";
        CrtRandom random(16);
        ModelEffect& fire = world.At(Create(world, Arguments(0xc2, 4), random));
        const auto stepped = Client::World::StepWizardModelEffect(fire, nullptr, random, world.Services());
        ASSERT_EQ(world.pushes.size(), 1U);
        EXPECT_EQ(world.pushes[0].first, fire.transform.position.x);
        EXPECT_EQ(world.pushes[0].second, fire.transform.position.y);
        EXPECT_NE(world.shake, 0.0f);
        if (fails)
        {
            ASSERT_FALSE(stepped.HasValue());
            EXPECT_EQ(stepped.Error(), "0x524afc: prueba");
            EXPECT_TRUE(fire.alive);
            continue;
        }
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_FALSE(fire.alive);
        EXPECT_EQ(fire.transform.position.z, 1000.0f);
        EXPECT_EQ(world.AliveOf(0xc8) + world.AliveOf(0xc9), 6U);
        EXPECT_EQ(world.ParticlesOf(0x5ed, 0), 1U);
    }
}

// 0x524f05: con rand() % 5 distinto de 0, el 0xc2/5 suelta la brasa 6ad/0 en su posición y con sus ángulos (la luz
// la pisa el alta con 0,5) y termina el paso; con las partículas llenas, sin la brasa ni sus tres rand().
TEST(WizardModelEffectsTest, TheBouncingFireDropsItsEmber)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        Harness world;
        world.ground = -1000.0;
        CrtRandom random(12);
        ModelEffect& fire = world.At(Create(world, Arguments(0xc2, 5), random));
        if (full)
            for (auto& particle : world.particles->All())
                particle.alive = true;
        CrtRandom oracle = random;
        static_cast<void>(oracle.Next());
        ASSERT_NE(oracle.Next() % 5, 0);
        const auto stepped = Client::World::StepWizardModelEffect(fire, nullptr, random, world.Services());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(fire.objectState.hiddenMesh, 0);
        EXPECT_EQ(fire.transform.position.z, 300.0f + 5.0f);
        EXPECT_EQ(fire.turnDirection, 3.0f);
        if (full)
        {
            EXPECT_EQ(random.State(), oracle.State());
            continue;
        }
        ASSERT_EQ(world.ParticlesOf(0x6ad, 0), 1U);
        const auto it =
            std::ranges::find_if(std::as_const(*world.particles).All(), [](const Client::World::Particle& particle)
                                 { return particle.alive && particle.type == 0x6ad; });
        const Client::World::Particle& ember = *it;
        Client::World::Particles twin;
        const auto made =
            twin.CreateChecked(0x6ad, ember.start, fire.transform.angles, glm::vec3(1.0f), 0, 1.0f, 1000.0f, oracle);
        ASSERT_TRUE(made.HasValue() && made.Value().has_value());
        const Client::World::Particle& expected = twin.All()[0];
        EXPECT_EQ(random.State(), oracle.State());
        EXPECT_EQ(ember.angles, fire.transform.angles);
        EXPECT_EQ(ember.life, 24);
        EXPECT_EQ(ember.life, expected.life);
        EXPECT_EQ(ember.velocity, expected.velocity);
        EXPECT_EQ(ember.scale, expected.scale);
        EXPECT_EQ(ember.rotation, expected.rotation);
        EXPECT_EQ(ember.light, glm::vec3(0.5f));
    }
}

// 0x522dff y 0x522c0e: el 0xcf/0 suelta su humo 5f2/3 antes de sus joints y el 0xce/0 cuatro, antes de su luz; los dos
// siguen hasta el final del paso.
TEST(WizardModelEffectsTest, TheStormAndTheMagicLeaveTheirSmoke)
{
    const auto smokes = [](Harness& world)
    {
        return std::ranges::count_if(world.particles->All(), [](const Client::World::Particle& particle)
                                     { return particle.alive && particle.type == 0x5f2 && particle.subtype == 3; });
    };
    Harness world;
    CrtRandom random(12);
    ModelEffect& storm = world.At(Create(world, Arguments(0xcf, 0), random));
    const auto stepped = Client::World::StepWizardModelEffect(storm, nullptr, random, world.Services());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(smokes(world), 1);
    ModelEffect& magic = world.At(Create(world, Arguments(0xce, 0), random));
    const auto smoked = Client::World::StepWizardModelEffect(magic, nullptr, random, world.Services());
    ASSERT_TRUE(smoked.HasValue()) << smoked.Error();
    EXPECT_EQ(smokes(world), 1 + 4);
}

// 0x52303b..0x5230be: el 0xcf/1 suelta su humo 5f2/3 y su gota 607/3 (con la tabla de partículas llena, 0x53fe70 no
// hace nada) y suma al terreno su propia luz (+0xf4, esi de 0x523049) con el radio 5; el tono (−0,1L; −0,3L; −L) de
// [ebp-0x1c] no lo lee nadie.
TEST(WizardModelEffectsTest, TheTwistingStormLightsTheGroundWithItsOwnLight)
{
    Harness world;
    CrtRandom random(14);
    ModelEffect& storm = world.At(Create(world, Arguments(0xcf, 1), random));
    for (auto& particle : world.particles->All())
        particle.alive = true;
    const glm::vec3 light = storm.light;
    const auto stepped = Client::World::StepWizardModelEffect(storm, nullptr, random, world.Services());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const auto reference = std::make_unique<Client::World::TerrainLighting>();
    Client::World::AddTerrainLight(*reference, storm.transform.position.x, storm.transform.position.y,
                                   {light.x, light.y, light.z}, 5);
    EXPECT_EQ(light, glm::vec3(0.5f, 0.6f, 0.7f));
    EXPECT_NE(world.lighting->cells[20 * 256 + 10], (Client::Data::Terrain::TerrainLightColor{}));
    for (std::size_t cell = 0; cell < reference->cells.size(); ++cell)
        ASSERT_EQ(world.lighting->cells[cell], reference->cells[cell]) << cell;
}

// 0x52303b..0x523089: con lugar, el 0xcf/1 suelta en el punto 100 más arriba su humo 5f2/3 y su gota 607/3 de 0,1f
// (0x523070, 0x523089), con sus ángulos ya girados: la gota vive rand() % 10 + 30, su escala suma (rand() % 6 + 4) ·
// 0,03f a la del pedido, sortea otra igual para la subida (+0x50) y pisa la luz con 1 (0x542e1a). Su paso suma 1 al
// cuadro y camina por la subida · 2,5 (0x5470d3).
TEST(WizardModelEffectsTest, TheTwistingStormDropsARisingDrop)
{
    Harness world;
    CrtRandom random(14);
    ModelEffect& storm = world.At(Create(world, Arguments(0xcf, 1), random));
    const glm::vec3 at = storm.transform.position;
    const glm::vec3 point{at.x, at.y, F32(static_cast<double>(at.z) + 100.0)};
    CrtRandom oracle = random;
    const auto stepped = Client::World::StepWizardModelEffect(storm, nullptr, random, world.Services());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(world.ParticlesOf(0x5f2, 3), 1U);
    ASSERT_EQ(world.ParticlesOf(0x607, 3), 1U);
    static_cast<void>(oracle.Next()); // 0x5208c5, la luz del paso.
    static_cast<void>(oracle.Next()); // rand() % 30 del giro.
    auto twin = std::make_unique<Client::World::Particles>();
    ASSERT_TRUE(
        twin->CreateChecked(0x5f2, point, storm.transform.angles, storm.light, 3, 1.0f, 1000.0f, oracle).HasValue());
    ASSERT_TRUE(twin->CreateChecked(0x607, point, storm.transform.angles, storm.light, 3,
                                    std::bit_cast<float>(0x3dcccccdu), 1000.0f, oracle)
                    .HasValue());
    EXPECT_EQ(random.State(), oracle.State());
    const auto drop =
        std::ranges::find_if(std::as_const(*world.particles).All(), [](const Client::World::Particle& particle)
                             { return particle.alive && particle.type == 0x607; });
    const Client::World::Particle& reference = twin->All()[1];
    EXPECT_EQ(drop->start, point);
    EXPECT_EQ(drop->angles, storm.transform.angles);
    EXPECT_EQ(drop->light, glm::vec3(1.0f));
    EXPECT_EQ(drop->life, reference.life);
    EXPECT_GE(drop->life, 30);
    EXPECT_LE(drop->life, 39);
    EXPECT_EQ(drop->scale, reference.scale);
    EXPECT_GT(drop->scale, std::bit_cast<float>(0x3dcccccdu));
    EXPECT_EQ(drop->verticalSpeed, reference.verticalSpeed);
    EXPECT_GT(drop->verticalSpeed, 0.0f);
}

// La piedrita /2 de las tormentas (0x5231a9 → 0x52b14d → 0x51b72b), con valores a mano: la semilla 298 da 1011, 32248,
// 3577, 29880 y 2406. El empuje ((1011 & 255) + 64) · 0,1f = 30,7f (0x41f5999a) en y, girado por los ángulos con la z
// pisada por 29880 % 360 = 0: sin giro; la vida (32248 & 15) + 32 = 40; la escala ((3577 & 3) + 8) · 0,1f (0x3f666667);
// la caída (2406 & 15) + 8 = 14. Cinco rand().
TEST(WizardModelEffectsTest, TheStormChipIsBornWithItsFiveDrawsByHand)
{
    for (const std::int16_t type : {std::int16_t{0xc8}, std::int16_t{0xc9}})
    {
        SCOPED_TRACE(type);
        auto effects = std::make_unique<ModelEffects>();
        CrtRandom random(298);
        const auto slot = effects->CreateStoneChip(type, {10.0f, 20.0f, 30.0f}, {0.0f, 0.0f, 45.0f}, {0.5f, 0.6f, 0.7f},
                                                   random, Client::World::SpinningStoneChipSubtype);
        ASSERT_EQ(slot, std::optional<std::size_t>(0));
        const ModelEffect& chip = effects->Effects()[0];
        EXPECT_EQ(chip.type, type);
        EXPECT_EQ(chip.subtype, 2);
        EXPECT_EQ(chip.velocity, glm::vec3(0.0f, std::bit_cast<float>(0x41f5999au), 0.0f));
        EXPECT_EQ(chip.life, 40);
        EXPECT_EQ(chip.transform.scale, std::bit_cast<float>(0x3f666667u));
        EXPECT_EQ(chip.transform.angles, glm::vec3(0.0f));
        EXPECT_EQ(chip.turnDirection, 14.0f);
        EXPECT_EQ(chip.transform.position, glm::vec3(10.0f, 20.0f, 30.0f));
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(298, 5).State());
    }
}

// 0x523101..0x52318f: el 0xcf/3..7 tiembla la cámara ((rand() & 7) − 4) · 0,1f y suelta en su punto la niebla 5f2/28,
// /29 y /30 (0x523144, 0x52315d, 0x523176), con sus ángulos, su luz y la escala 1, que piden la altura del terreno y,
// la /30, el seno del reloj. Después, con rand() & 1 en 0, la piedrita 0xc8 + (rand() & 1) con el subtipo 2 (0x5231a9
// → 0x52b14d), que nace como la /0 (0x51b72b) con sus cinco rand(). Con la tabla de partículas llena, la niebla no saca
// nada. Réplica en un pool gemelo con el mismo CrtRandom.
TEST(WizardModelEffectsTest, TheLittleStormsLeaveTheirMistOnTheGround)
{
    bool calm = false, chipped = false;
    for (const std::int32_t subtype : {3, 7})
        for (const bool full : {false, true})
            for (std::uint32_t seed = 20; seed < 24; ++seed)
            {
                SCOPED_TRACE(testing::Message() << subtype << ' ' << full << ' ' << seed);
                Harness world;
                world.ground = 123.25;
                CrtRandom random(seed);
                ModelEffect& storm = world.At(Create(world, Arguments(0xcf, subtype), random));
                if (full)
                    for (auto& particle : world.particles->All())
                        particle.alive = true;
                const glm::vec3 at = storm.transform.position;
                const glm::vec3 angles = storm.transform.angles;
                const glm::vec3 light = storm.light;
                CrtRandom oracle = random;
                static_cast<void>(oracle.Next()); // 0x5208c5, la luz del paso.
                const float shake = F32(static_cast<double>((oracle.Next() & 7) - 4) * Tenth);
                auto twin = std::make_unique<Client::World::Particles>();
                if (full)
                    for (auto& particle : twin->All())
                        particle.alive = true;
                Client::World::PortableParticleTrigonometry trigonometry;
                const float clock = 1000.0f;
                const Client::World::ParticleTerrainHeight ground = [](float, float) { return 123.25; };
                for (const std::int32_t mist : {28, 29, 30})
                    ASSERT_TRUE(twin->CreateOnGroundChecked(0x5f2, at, angles, light, mist, 1.0f, std::cref(clock),
                                                            oracle, trigonometry, ground)
                                    .HasValue());
                const bool chip = (oracle.Next() & 1) == 0;
                auto twinEffects = std::make_unique<ModelEffects>();
                std::optional<std::size_t> twinChip;
                if (chip)
                {
                    // 0x5231a9, el tipo de la piedrita, y su alta en una tabla gemela.
                    const auto type = static_cast<std::int16_t>(0xc8 + (oracle.Next() & 1));
                    twinChip = twinEffects->CreateStoneChip(type, at, angles, light, oracle, 2);
                    ASSERT_TRUE(twinChip.has_value());
                }
                const auto stepped = Client::World::StepWizardModelEffect(storm, nullptr, random, world.Services());
                ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
                EXPECT_EQ(world.shake, shake);
                const auto chips =
                    std::ranges::count_if(world.effects->Effects(), [](const ModelEffect& effect)
                                          { return effect.alive && (effect.type == 0xc8 || effect.type == 0xc9); });
                if (chip)
                {
                    ASSERT_EQ(chips, 1);
                    const auto found = std::ranges::find_if(world.effects->Effects(), [](const ModelEffect& effect)
                                                            { return effect.alive && effect.subtype == 2; });
                    const ModelEffect& reference = twinEffects->Effects()[*twinChip];
                    EXPECT_EQ(found->type, reference.type);
                    EXPECT_EQ(found->transform.position, at);
                    EXPECT_EQ(found->transform.angles, reference.transform.angles);
                    EXPECT_EQ(found->transform.scale, reference.transform.scale);
                    EXPECT_EQ(found->velocity, reference.velocity);
                    EXPECT_EQ(found->life, reference.life);
                    EXPECT_EQ(found->turnDirection, reference.turnDirection);
                    EXPECT_EQ(found->light, light);
                    chipped = true;
                }
                else
                {
                    EXPECT_EQ(chips, 0);
                    calm = true;
                }
                EXPECT_EQ(random.State(), oracle.State());
                if (full)
                    continue;
                for (const std::int32_t mist : {28, 29, 30})
                {
                    ASSERT_EQ(world.ParticlesOf(0x5f2, mist), 1U) << mist;
                    const auto found = std::ranges::find_if(
                        std::as_const(*world.particles).All(), [mist](const Client::World::Particle& particle)
                        { return particle.alive && particle.type == 0x5f2 && particle.subtype == mist; });
                    const auto reference =
                        std::ranges::find_if(std::as_const(*twin).All(), [mist](const Client::World::Particle& particle)
                                             { return particle.alive && particle.subtype == mist; });
                    EXPECT_EQ(found->start, at);
                    EXPECT_EQ(found->angles, angles);
                    EXPECT_EQ(found->position, reference->position);
                    EXPECT_EQ(found->position.z, 138.25f);
                    EXPECT_EQ(found->rotation, reference->rotation);
                    EXPECT_EQ(found->life, reference->life);
                    EXPECT_EQ(found->scale, reference->scale);
                    EXPECT_EQ(found->texture, mist == 28 ? 0x6ad : 0x5f2);
                    EXPECT_EQ(found->light, light);
                }
            }
    EXPECT_TRUE(calm);
    EXPECT_TRUE(chipped);
}

// 0x5231f6..0x5232f8: el 0xcb/1 con la vida por encima de 44 − +0x1bc del dueño suelta 36 joints 613/6 en abanico,
// cada uno con su rand() % 10 (0x52321f) antes del alta: los ángulos (−10, 0, (r + 10) · i), el punto (x, y, z + 100)
// en las dos posiciones, el efecto de dueño, ancho 60, la clave 0 y sin blanco. Con la vida justo en 45 − +0x1bc, un
// 613/7 de ángulo 10 · i detrás de cada uno. Con la vida en 44 − +0x1bc o menos, nada.
TEST(WizardModelEffectsTest, TheHiddenCircleFansItsRays)
{
    struct Case
    {
        std::uint8_t byte1bc;
        std::int32_t life;
        bool second;
    };
    for (const Case tested : {Case{0, 45, true}, Case{0, 46, false}, Case{3, 42, true}, Case{3, 43, false}})
    {
        SCOPED_TRACE(tested.life);
        Harness world;
        world.owner = WizardOwnerView{.alive = true, .byte1bc = tested.byte1bc};
        CrtRandom random(15);
        ModelEffectArguments arguments = Arguments(0xcb, 1);
        int owner = 0;
        arguments.owner = &owner;
        ModelEffect& circle = world.At(Create(world, arguments, random));
        circle.life = tested.life;
        CrtRandom oracle = random;
        static_cast<void>(oracle.Next()); // 0x5208c5, la luz del paso.
        const auto stepped = Client::World::StepWizardModelEffect(circle, nullptr, random, world.Services());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_EQ(world.joints.size(), tested.second ? 72U : 36U);
        const glm::vec3 point{1000.0f, 2000.0f, 400.0f};
        std::size_t next = 0;
        for (int ray = 0; ray < 36; ++ray)
        {
            SCOPED_TRACE(ray);
            const auto spread = static_cast<float>(oracle.Next() % 10 + 10);
            const RecordedJoint& fan = world.joints[next++];
            EXPECT_EQ(fan.type, 0x613);
            EXPECT_EQ(fan.subtype, 6);
            EXPECT_EQ(fan.first, point);
            EXPECT_EQ(fan.second, point);
            EXPECT_EQ(fan.angles, glm::vec3(-10.0f, 0.0f, spread * static_cast<float>(ray)));
            EXPECT_EQ(fan.owner, &circle);
            EXPECT_EQ(fan.scale, 60.0f);
            EXPECT_EQ(fan.key, 0);
            EXPECT_EQ(fan.target, -1);
            if (!tested.second)
                continue;
            const RecordedJoint& trail = world.joints[next++];
            EXPECT_EQ(trail.subtype, 7);
            EXPECT_EQ(trail.first, point);
            EXPECT_EQ(trail.angles, glm::vec3(-10.0f, 0.0f, 10.0f * static_cast<float>(ray)));
            EXPECT_EQ(trail.owner, &circle);
            EXPECT_EQ(trail.scale, 60.0f);
            EXPECT_EQ(trail.key, 0);
        }
        EXPECT_EQ(random.State(), oracle.State());
    }

    Harness world;
    world.owner = WizardOwnerView{.alive = true, .byte1bc = 3};
    CrtRandom random(15);
    ModelEffectArguments arguments = Arguments(0xcb, 1);
    int owner = 0;
    arguments.owner = &owner;
    ModelEffect& circle = world.At(Create(world, arguments, random));
    circle.life = 41;
    CrtRandom quiet = random;
    static_cast<void>(quiet.Next());
    ASSERT_TRUE(Client::World::StepWizardModelEffect(circle, nullptr, random, world.Services()).HasValue());
    EXPECT_TRUE(world.joints.empty());
    EXPECT_EQ(random.State(), quiet.State());
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

// 0x523640..0x52384c: el círculo 0xcc/3 (con más de 30 de vida y rand() & 1 en 0) y el /4 (rand() % 5 en 0 y más de 5
// de vida) sacan el alcance y el giro y dejan la columna 0x62b/0 de 1 con sus ángulos y su luz (0x523677, 0x52384c;
// 0x542d01: dos rand() más, vida rand() % 10 + 30, luz 1 y la velocidad (0; 0; (rand() % 100) · 0,02f)); después el
// joint 0x62b/0x13 600 más arriba. Con las partículas llenas, el alcance y el giro salen igual (son los argumentos) y
// el joint también; sólo faltan los dos rand() de la alta. Lo esperado sale de column.py (0x523582..0x523898 y
// 0x542d01 en una x87 aparte), con la semilla 2 para el /3 y la 4 para el /4.
TEST(WizardModelEffectsTest, TheOuterCircleRaisesFlareColumns)
{
    struct Column
    {
        std::int32_t subtype;
        std::uint32_t seed;
        std::uint32_t state;
        std::uint32_t fullState;
        std::array<std::uint32_t, 2> point; // x e y: (0, alcance, 0) girado por (0, 0, giro), más la posición
        std::int32_t life;
        std::uint32_t rise;
    };
    constexpr std::array<Column, 2> Columns{{
        {3, 2, 0x4cc2cb10u, 0xc583c166u, {0x44651495u, 0x44e391a2u}, 34, 0x3f800000u},
        {4, 4, 0x6b7040c2u, 0x81826208u, {0x447388b6u, 0x45005076u}, 35, 0x3da3d70au},
    }};
    for (const Column& expected : Columns)
    {
        SCOPED_TRACE(expected.subtype);
        for (const bool full : {false, true})
        {
            SCOPED_TRACE(full);
            Harness world;
            if (full)
                for (auto& particle : world.particles->All())
                    particle.alive = true;
            CrtRandom random(expected.seed);
            ModelEffect& circle = world.At(Create(world, Arguments(0xcc, expected.subtype), random));
            circle.life = 100;
            ASSERT_TRUE(Client::World::StepWizardModelEffect(circle, nullptr, random, world.Services()).HasValue());
            EXPECT_EQ(random.State(), full ? expected.fullState : expected.state);
            const glm::vec3 point{std::bit_cast<float>(expected.point[0]), std::bit_cast<float>(expected.point[1]),
                                  300.0f};
            ASSERT_EQ(world.joints.size(), 1U);
            EXPECT_EQ(world.joints[0].type, 0x62b);
            EXPECT_EQ(world.joints[0].subtype, 0x13);
            EXPECT_EQ(world.joints[0].first, (glm::vec3{point.x, point.y, 900.0f}));
            EXPECT_EQ(world.joints[0].second, world.joints[0].first);
            EXPECT_EQ(world.joints[0].angles, (glm::vec3{0.0f, 0.0f, 45.0f}));
            EXPECT_EQ(world.joints[0].scale, 40.0f);
            if (full)
            {
                EXPECT_TRUE(
                    std::ranges::all_of(world.particles->All(), [](const auto& made) { return made.type == 0; }));
                continue;
            }
            EXPECT_EQ(world.ParticlesOf(0x62b, 0), 1U);
            const Client::World::Particle& flare = world.particles->All()[0];
            EXPECT_EQ(flare.position, point);
            EXPECT_EQ(flare.angles, (glm::vec3{0.0f, 0.0f, 90.0f}));
            EXPECT_EQ(flare.light, glm::vec3(1.0f));
            EXPECT_EQ(flare.life, expected.life);
            EXPECT_EQ(flare.velocity, (glm::vec3{0.0f, 0.0f, std::bit_cast<float>(expected.rise)}));
            EXPECT_EQ(flare.scale, 1.0f);
        }
    }
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

// 0x51b432..0x51b4ff, 0x51b509..0x51b5d6 y 0x51b612..0x51b658: las piedritas 0xd8 de subtipo 10 y 0xc nacen con el
// empuje ((& 255) + 64) girado por el yaw rand() % 360, la vida (& 15) + 32, la escala ((& 3) + 15) y la caída (la /10
// con 0,2 en double, 0,05f y + 28; la /0xc con 0,1f, 0,1f y + 8); la /0xb, con la vida 40, la escala ((& 3) + 8) ·
// 0,8f y el yaw. En el paso, después del rand() de la luz, la /10 cae sin otro rand(), la /0xc saca el % 10 (0x523adb)
// y la /0xb, con la vida en 10 o más, no hace nada.
TEST(WizardModelEffectsTest, TheStonesOfSubtypesTenElevenAndTwelveAreBorn)
{
    for (const std::int32_t subtype : {10, 0xb, 0xc})
    {
        SCOPED_TRACE(subtype);
        Harness world;
        world.ground = -10000.0;
        CrtRandom random(31);
        ModelEffect& stone = world.At(Create(world, Arguments(0xd8, subtype), random));
        CrtRandom oracle(31);
        if (subtype == 0xb)
        {
            const auto scale = F32(static_cast<double>((oracle.Next() & 3) + 8) *
                                   static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)));
            const auto yaw = static_cast<float>(oracle.Next() % 360);
            EXPECT_EQ(stone.life, 40);
            EXPECT_EQ(stone.transform.scale, scale);
            EXPECT_EQ(stone.transform.angles, (glm::vec3{0.0f, 0.0f, yaw}));
        }
        else
        {
            const bool ground = subtype == 10;
            const double pushStep = ground ? 0.2 : Tenth;
            const double scaleStep = ground ? static_cast<double>(std::bit_cast<float>(0x3d4ccccdu)) : Tenth;
            const auto push = F32(static_cast<double>((oracle.Next() & 255) + 64) * pushStep);
            const std::int32_t life = (oracle.Next() & 15) + 32;
            const auto scale = F32(static_cast<double>((oracle.Next() & 3) + 15) * scaleStep);
            const auto yaw = static_cast<float>(oracle.Next() % 360);
            const auto fall = static_cast<float>((oracle.Next() & 15) + (ground ? 28 : 8));
            const glm::vec3 angles{0.0f, 0.0f, yaw};
            EXPECT_EQ(stone.life, life);
            EXPECT_EQ(stone.transform.scale, scale);
            EXPECT_EQ(stone.transform.angles, angles);
            EXPECT_EQ(stone.velocity, Client::Render::Models::OriginalVectorRotate(
                                          {0.0f, push, 0.0f}, Client::Render::Models::OriginalAngleMatrix(angles)));
            EXPECT_EQ(stone.turnDirection, fall);
        }
        EXPECT_EQ(random.State(), oracle.State());
        const float alpha = stone.alpha;
        ASSERT_TRUE(Client::World::StepWizardModelEffect(stone, nullptr, random, world.Services()).HasValue());
        static_cast<void>(oracle.Next()); // 0x5208c5, la luz del paso.
        if (subtype == 0xc)
            static_cast<void>(oracle.Next()); // 0x523adb, el % 10 sin fuego.
        EXPECT_EQ(random.State(), oracle.State());
        if (subtype == 0xb)
        {
            EXPECT_EQ(stone.alpha, alpha);
        }
        EXPECT_FALSE(world.particles->Any());
    }
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
