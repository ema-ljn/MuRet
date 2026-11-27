#include "World/BlastModelEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "World/GameModelEffects.h"
#include "World/IFishStepServices.h"
#include "World/KalimaParticleKernels.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "Terrain/TerrainLighting.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
using Client::Crt::CrtRandom;
using Client::World::BlastEffectServices;
using Client::World::FishEffectRequest;
using Client::World::ModelEffect;
using Client::World::ModelEffectArguments;
using Client::World::ModelEffects;

constexpr int OwnRecordCloseRandoms = 0xf08;

using Client::Math::F32;

struct JointCall
{
    std::int32_t type = 0;
    std::int32_t subtype = 0;
    float scale = 0.0f;
    std::int32_t key = 0;
    std::int32_t skill = 0;
    void* owner = nullptr;
    bool effectOwner = false;
};

struct Harness
{
    std::unique_ptr<ModelEffects> effects = std::make_unique<ModelEffects>();
    std::uint8_t depth = 0;
    std::uint8_t count = 7;
    int hero = 0;
    std::vector<JointCall> joints;
    std::vector<float> hits;
    std::vector<std::int32_t> sounds;

    BlastEffectServices Services()
    {
        return {.ownRecordDepth = &depth,
                .ownRecordCount = &count,
                .hero = &hero,
                .skillHits = [this](const ModelEffect&, float radius) -> Client::Core::Expected<void, std::string>
                {
                    hits.push_back(radius);
                    return {};
                },
                .joint = [this](const FishEffectRequest& request,
                                bool effectOwner) -> Client::Core::Expected<void, std::string>
                {
                    joints.push_back({request.type, request.subtype, request.scale, request.key, request.skill,
                                      request.ownerContext, effectOwner});
                    return {};
                },
                .sound = [this](Client::Audio::SoundId id) { sounds.push_back(id.Value()); }};
    }
};

ModelEffectArguments Arguments(std::int16_t type, std::int32_t subtype, void* owner = nullptr)
{
    return {.type = type,
            .position = {1000, 2000, 300},
            .angles = {0, 0, 90},
            .light = {1, 1, 1},
            .subtype = subtype,
            .owner = owner,
            .key = 0xffff,
            .byte89 = 0,
            .byte88 = 0,
            .byte1bd = 0,
            .scale = 0.0f};
}

void Skip(CrtRandom& random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
}

// Los rand() del alta de una partícula: la misma alta en un pool aparte, con el mismo generador.
void ReplayParticle(CrtRandom& replay, std::int32_t type, std::int32_t subtype, float scale = 1.0f)
{
    auto twin = std::make_unique<Client::World::Particles>();
    const auto made =
        twin->CreateChecked(type, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), subtype, scale, 0.0f, replay);
    ASSERT_TRUE(made.HasValue());
}

std::ptrdiff_t AliveParticles(Client::World::Particles& particles, std::int32_t type, std::int32_t subtype)
{
    return std::ranges::count_if(particles.All(), [type, subtype](const Client::World::Particle& particle)
                                 { return particle.alive && particle.type == type && particle.subtype == subtype; });
}

void FillThePool(ModelEffects& effects)
{
    for (auto& effect : effects.Effects())
        effect.alive = true;
}

TEST(BlastModelEffectsTest, TheBlastFallsFromAboveWithItsJointAndCopiesTheOwnCount)
{
    Harness harness;
    CrtRandom random(11);
    CrtRandom replay(11);
    const auto made =
        Client::World::CreateBlastModelEffect(*harness.effects, Arguments(0xf5, 0), {}, random, harness.Services());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    ASSERT_TRUE(made.Value().has_value());
    const ModelEffect& blast = harness.effects->Effects()[*made.Value()];
    // 0x5192f7..0x519381: la escala, la x, la y, la z y la caída, en ese orden.
    EXPECT_EQ(blast.life, 30);
    EXPECT_EQ(blast.objectState.blendMesh, 0);
    EXPECT_EQ(blast.transform.scale, F32(static_cast<double>((replay.Next() & 7) + 10) * static_cast<double>(0.1f)));
    EXPECT_EQ(blast.transform.position.x, F32(static_cast<double>(replay.Next() % 100 + 200) + 1000.0));
    EXPECT_EQ(blast.transform.position.y, F32(static_cast<double>(replay.Next() % 100 - 50) + 2000.0));
    EXPECT_EQ(blast.transform.position.z, F32(static_cast<double>(replay.Next() % 500 + 300) + 300.0));
    EXPECT_EQ(blast.velocity.z, F32(-50.0 - static_cast<double>(replay.Next() % 50)));
    EXPECT_EQ(blast.transform.angles, glm::vec3(0, 20, 0));
    EXPECT_EQ(blast.point40, blast.transform.position);
    ASSERT_EQ(harness.joints.size(), 1u);
    EXPECT_EQ(harness.joints[0].type, 0x618);
    EXPECT_EQ(harness.joints[0].subtype, 5);
    EXPECT_EQ(harness.joints[0].scale, 100.0f);
    EXPECT_TRUE(harness.joints[0].effectOwner);
    EXPECT_EQ(blast.word8c, 7);
    Skip(replay, OwnRecordCloseRandoms);
    EXPECT_EQ(random.State(), replay.State());
}

TEST(BlastModelEffectsTest, AFullPoolCreatesNothingAndDrawsNothing)
{
    Harness harness;
    FillThePool(*harness.effects);
    CrtRandom random(5);
    const auto before = random.State();
    for (const int kind : {0xf5, 0xf6, 0x106, 0x108, 0x10c, 0xe7})
    {
        const auto type = static_cast<std::int16_t>(kind);
        const auto made =
            Client::World::CreateBlastModelEffect(*harness.effects, Arguments(type, 1), {}, random, harness.Services());
        ASSERT_TRUE(made.HasValue()) << made.Error();
        EXPECT_FALSE(made.Value().has_value());
    }
    EXPECT_EQ(random.State(), before);
    EXPECT_TRUE(harness.joints.empty());
}

TEST(BlastModelEffectsTest, TheDeathStabMovesAheadAndCreatesItsPiercingChild)
{
    Harness harness;
    CrtRandom random(3);
    const auto before = random.State();
    const auto made =
        Client::World::CreateBlastModelEffect(*harness.effects, Arguments(0x106, 1), {}, random, harness.Services());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    ASSERT_EQ(*made.Value(), 0u);
    const ModelEffect& stab = harness.effects->Effects()[0];
    const ModelEffect& child = harness.effects->Effects()[1];
    const glm::vec3 offset =
        Client::Math::OriginalVectorRotate({-10, -60, 135}, Client::Math::OriginalAngleMatrix({0, 0, 90}));
    EXPECT_EQ(stab.transform.position.x, F32(static_cast<double>(offset.x) + 1000.0));
    EXPECT_EQ(stab.life, 30);
    EXPECT_EQ(stab.speed, 1.0f);
    EXPECT_EQ(stab.transform.scale, 0.8f);
    EXPECT_EQ(stab.velocity.y, -70.0f);
    ASSERT_TRUE(child.alive);
    EXPECT_EQ(child.type, 0x105);
    EXPECT_EQ(child.subtype, 1);
    EXPECT_EQ(child.owner.effect, &stab);
    EXPECT_EQ(child.transform.position, stab.transform.position);
    EXPECT_EQ(child.life, 100);
    EXPECT_EQ(child.objectState.hiddenMesh, 0);
    EXPECT_EQ(child.transform.scale, 1.0f);
    ASSERT_EQ(harness.joints.size(), 4u);
    for (std::int32_t i = 0; i < 4; ++i)
    {
        EXPECT_EQ(harness.joints[static_cast<std::size_t>(i)].type, 0x610);
        EXPECT_EQ(harness.joints[static_cast<std::size_t>(i)].subtype, i);
        EXPECT_EQ(harness.joints[static_cast<std::size_t>(i)].scale, 24.0f);
        EXPECT_EQ(harness.joints[static_cast<std::size_t>(i)].key, 0x1e);
        EXPECT_EQ(harness.joints[static_cast<std::size_t>(i)].skill, 1);
    }
    EXPECT_EQ(harness.sounds, std::vector<std::int32_t>{0x7a});
    EXPECT_EQ(random.State(), before);
}

TEST(BlastModelEffectsTest, TheHeroInfernoCopiesTheCountAndHitsAtFourHundred)
{
    Harness harness;
    CrtRandom random(9);
    CrtRandom replay(9);
    const auto made = Client::World::CreateBlastModelEffect(*harness.effects, Arguments(0xf6, 0, &harness.hero), {},
                                                            random, harness.Services());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    const ModelEffect& inferno = harness.effects->Effects()[*made.Value()];
    EXPECT_EQ(inferno.life, 15);
    EXPECT_EQ(inferno.light, glm::vec3(0.8f));
    EXPECT_EQ(inferno.objectState.blendMesh, -2);
    EXPECT_EQ(inferno.word8c, 7);
    EXPECT_EQ(harness.hits, std::vector<float>{400.0f});
    Skip(replay, OwnRecordCloseRandoms);
    EXPECT_EQ(random.State(), replay.State());

    // Con el subtipo 2 (o con otro dueño), ni el registro ni los blancos.
    auto keyed = Arguments(0xf6, 2, &harness.hero);
    keyed.key = 150;
    keyed.byte89 = 3;
    const auto second = Client::World::CreateBlastModelEffect(*harness.effects, keyed, {}, random, harness.Services());
    ASSERT_TRUE(second.HasValue()) << second.Error();
    const ModelEffect& ring = harness.effects->Effects()[*second.Value()];
    EXPECT_EQ(ring.life, 12);
    EXPECT_EQ(ring.objectState.hiddenMesh, 3);
    EXPECT_EQ(ring.byte89, 0);
    EXPECT_EQ(ring.disturbanceState, 0xffff);
    EXPECT_EQ(ring.flagEc, 150.0f);
    EXPECT_EQ(ring.transform.scale, F32(150.0 * static_cast<double>(0.01f)));
    EXPECT_EQ(random.State(), replay.State());
    EXPECT_EQ(harness.hits.size(), 1u);
}

// 0x519a49..0x519a60: el dueño se compara con el héroe publicado (0x743a170) y después el subtipo; desde el 2 las dos
// salidas van a 0x51db73. Sin el héroe, solo el /0 y el /1 cortan; del /2 al /6 nacen sin él.
TEST(BlastModelEffectsTest, TheInfernoOnlyAsksForTheHeroBelowTheSecondSubtype)
{
    for (const std::int32_t subtype : {0, 1, 2, 3, 4, 5, 6})
    {
        SCOPED_TRACE(subtype);
        Harness harness;
        BlastEffectServices services = harness.Services();
        services.hero = nullptr;
        CrtRandom random(9);
        const auto made = Client::World::CreateBlastModelEffect(*harness.effects, Arguments(0xf6, subtype, &harness),
                                                                {}, random, services);
        if (subtype < 2)
        {
            ASSERT_FALSE(made.HasValue());
            EXPECT_NE(made.Error().find("0x519a4f"), std::string::npos) << made.Error();
            continue;
        }
        ASSERT_TRUE(made.HasValue()) << made.Error();
        ASSERT_TRUE(made.Value().has_value());
        EXPECT_EQ(harness.effects->Effects()[*made.Value()].subtype, subtype);
        EXPECT_EQ(random.State(), CrtRandom(9).State());
        EXPECT_TRUE(harness.hits.empty());
    }
}

TEST(BlastModelEffectsTest, TheBlizzardDrawsItsFallAndTheSecondIceJointHalfTheTime)
{
    Harness harness;
    CrtRandom random(21);
    CrtRandom replay(21);
    const auto made =
        Client::World::CreateBlastModelEffect(*harness.effects, Arguments(0x108, 2), {}, random, harness.Services());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    const ModelEffect& blizzard = harness.effects->Effects()[*made.Value()];
    EXPECT_EQ(blizzard.life, replay.Next() % 15 + 15);
    EXPECT_EQ(blizzard.speed, static_cast<float>(replay.Next() % 360));
    EXPECT_EQ(blizzard.turnDirection, F32(-20.0 - static_cast<double>(replay.Next() % 30 + 10)));
    const float x = F32(static_cast<double>(replay.Next() % 300) + 1000.0 - 150.0);
    EXPECT_EQ(blizzard.transform.position.y, F32(static_cast<double>(replay.Next() % 300) + 2000.0 - 150.0));
    EXPECT_EQ(blizzard.transform.position.x, F32(static_cast<double>(x) + 100.0));
    EXPECT_EQ(blizzard.transform.position.z, 900.0f);
    EXPECT_EQ(blizzard.start, blizzard.transform.position);
    EXPECT_EQ(blizzard.light, glm::vec3(0.0f));
    EXPECT_EQ(blizzard.transform.scale, 0.5f);
    const float firstScale = F32(static_cast<double>(replay.Next() % 10) + 60.0);
    const bool second = replay.Next() % 2 == 0;
    ASSERT_EQ(harness.joints.size(), second ? 2u : 1u);
    EXPECT_EQ(harness.joints[0].type, 0x615);
    EXPECT_EQ(harness.joints[0].subtype, 4);
    EXPECT_EQ(harness.joints[0].scale, firstScale);
    if (second)
        Skip(replay, 1);
    EXPECT_EQ(random.State(), replay.State());
}

TEST(BlastModelEffectsTest, GateDebrisDrawsNineRandomsForTheFirstGateAndEightForTheSecond)
{
    Harness harness;
    for (const int kind : {0x10c, 0x10d})
    {
        const auto type = static_cast<std::int16_t>(kind);
        CrtRandom random(31);
        CrtRandom replay(31);
        const auto made =
            Client::World::CreateBlastModelEffect(*harness.effects, Arguments(type, 0), {}, random, harness.Services());
        ASSERT_TRUE(made.HasValue()) << made.Error();
        const ModelEffect& debris = harness.effects->Effects()[*made.Value()];
        EXPECT_EQ(debris.transform.position.z, 350.0f);
        EXPECT_EQ(debris.subtype, type == 0x10c ? 1 : 0);
        Skip(replay, type == 0x10c ? 9 : 8);
        EXPECT_EQ(random.State(), replay.State());
    }
}

TEST(BlastModelEffectsTest, TheRidingSpearTakesItsVelocityFromTheYaw)
{
    Harness harness;
    CrtRandom random(1);
    const auto made =
        Client::World::CreateBlastModelEffect(*harness.effects, Arguments(0x10f, 0), {}, random, harness.Services());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    const ModelEffect& spear = harness.effects->Effects()[*made.Value()];
    // Calculados aparte para el rumbo 90: FSIN · 5 y FCOS · −5 en double, guardados en float.
    EXPECT_EQ(spear.velocity.x, std::bit_cast<float>(0x40a00000u));
    EXPECT_EQ(spear.velocity.y, std::bit_cast<float>(0xb382a70cu));
    EXPECT_EQ(spear.life, 20);
    EXPECT_EQ(spear.transform.scale, 1.5f);
}

TEST(BlastModelEffectsTest, TheSwordTrailRaisesTheOwnCountAndTheSpinningItemCopiesIt)
{
    Harness harness;
    CrtRandom random(1);
    CrtRandom replay(1);
    const auto trail =
        Client::World::CreateBlastModelEffect(*harness.effects, Arguments(0xf3, 0), {}, random, harness.Services());
    ASSERT_TRUE(trail.HasValue()) << trail.Error();
    EXPECT_EQ(harness.count, 8);
    EXPECT_EQ(harness.effects->Effects()[*trail.Value()].life, 5);
    const auto item =
        Client::World::CreateBlastModelEffect(*harness.effects, Arguments(0xf4, 2), {}, random, harness.Services());
    ASSERT_TRUE(item.HasValue()) << item.Error();
    EXPECT_EQ(harness.effects->Effects()[*item.Value()].word8c, 8);
    EXPECT_EQ(harness.effects->Effects()[*item.Value()].life, 25);
    Skip(replay, 2 * OwnRecordCloseRandoms);
    EXPECT_EQ(random.State(), replay.State());
}

TEST(BlastModelEffectsTest, OtherTypesAreNotOfThisBatch)
{
    Harness harness;
    CrtRandom random(1);
    EXPECT_FALSE(Client::World::IsBlastModelEffect(0xf8, 0));
    EXPECT_FALSE(Client::World::IsBlastModelEffect(0x105, 3));
    EXPECT_FALSE(Client::World::IsBlastModelEffect(0x5de, 0));
    EXPECT_TRUE(Client::World::IsBlastModelEffect(0x5de, 2));
    const auto made =
        Client::World::CreateBlastModelEffect(*harness.effects, Arguments(0xf8, 0), {}, random, harness.Services());
    EXPECT_FALSE(made.HasValue());
}

// El paso, por StepGameModelEffect: el primer rand() es el de la luz local.
struct StepHarness
{
    std::unique_ptr<ModelEffects> effects = std::make_unique<ModelEffects>();
    std::unique_ptr<Client::World::Particles> particles = std::make_unique<Client::World::Particles>();
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    std::unique_ptr<Client::World::FrameSprites> sprites = std::make_unique<Client::World::FrameSprites>();
    std::unique_ptr<Client::Data::Terrain::TerrainAttributes> attributes =
        std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    std::int32_t map = 0;
    float shake = 0.0f;
    bool withShake = true;
    std::uint8_t depth = 0;
    std::uint8_t count = 4;
    float ground = 0.0f;
    std::vector<std::int32_t> sounds;
    std::vector<std::int32_t> joints;

    Client::World::ModelEffectStepServices Services()
    {
        return {.terrainHeight = [this](float, float) { return static_cast<double>(ground); },
                .sprites = sprites.get(),
                .effects = effects.get(),
                .lighting = lighting.get(),
                .terrainAttributes = attributes.get(),
                .map = &map,
                .cameraShake = withShake ? &shake : nullptr,
                .sound = [this](Client::Audio::SoundId id) { sounds.push_back(id.Value()); },
                .joint = [this](const FishEffectRequest& request) -> Client::Core::Expected<void, std::string>
                {
                    joints.push_back(request.type);
                    return {};
                },
                .ownRecordDepth = &depth,
                .ownRecordCount = &count};
    }

    Client::Core::Expected<void, std::string> Step(ModelEffect& effect, CrtRandom& random)
    {
        return Client::World::StepGameModelEffect(effect, nullptr, *particles, 0.0f, random, nullptr, Services());
    }
};

ModelEffect& Alive(ModelEffects& effects, std::size_t slot, std::int16_t type, std::int32_t subtype, std::int32_t life)
{
    ModelEffect& effect = effects.Effects()[slot];
    effect.alive = true;
    effect.type = type;
    effect.subtype = subtype;
    effect.life = life;
    effect.transform.position = {1000, 2000, 300};
    effect.transform.angles = {};
    effect.velocity = {};
    effect.light = glm::vec3(1.0f);
    effect.animation.action = 0xff;
    return effect;
}

// 0x5211a0 y 0x5211bf: después de las piedritas, el anillo 0x600/0 (el rand() % 360 de su giro, 0x5431a1) y la
// explosión 0x5ed/0 (sin rand(), con el sonido 0x40 en 0x542a82), 80 más arriba; sin dueño, muere.
TEST(BlastModelEffectsTest, TheBlastOnTheGroundDropsSixChipsARingAndAnExplosion)
{
    StepHarness harness;
    CrtRandom random(2);
    CrtRandom replay(2);
    ModelEffect& blast = Alive(*harness.effects, 0, 0xf5, 0, 30);
    harness.ground = 400.0f;
    const auto stepped = harness.Step(blast, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(blast.alive);
    EXPECT_EQ(blast.transform.position.z, 400.0f);
    EXPECT_EQ(blast.velocity, glm::vec3(0.0f));
    const auto chips = std::ranges::count_if(harness.effects->Effects(), [](const ModelEffect& effect)
                                             { return effect.alive && (effect.type == 0xc8 || effect.type == 0xc9); });
    EXPECT_EQ(chips, 6);
    const auto born = [&](std::int32_t type)
    {
        return std::ranges::count_if(harness.particles->All(), [type](const Client::World::Particle& particle)
                                     { return particle.alive && particle.type == type && particle.subtype == 0; });
    };
    EXPECT_EQ(born(0x600), 1);
    EXPECT_EQ(born(Client::World::ExplosionParticle), 1);
    // La luz, por piedrita el rand() % 2 del tipo (0x52115e) y los cinco del alta (0x51b72b), y las dos altas.
    Skip(replay, 1 + 6 * (1 + 5));
    ReplayParticle(replay, 0x600, 0);
    ReplayParticle(replay, Client::World::ExplosionParticle, 0);
    EXPECT_EQ(random.State(), replay.State());
}

TEST(BlastModelEffectsTest, TheBlastInTheAirKeepsFallingWithItsLight)
{
    StepHarness harness;
    CrtRandom random(2);
    ModelEffect& blast = Alive(*harness.effects, 0, 0xf5, 0, 30);
    blast.velocity = {0, 0, -60};
    const auto stepped = harness.Step(blast, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(blast.alive);
    EXPECT_EQ(blast.life, 29);
    EXPECT_EQ(blast.point40, glm::vec3(1000, 2000, 300));
    EXPECT_EQ(blast.transform.position.z, 240.0f);
    EXPECT_EQ(blast.previousPosition, glm::vec3(1000, 2000, 300));
}

TEST(BlastModelEffectsTest, TheSwordTrailLeavesASpinningItemAndOnlyLosesLife)
{
    StepHarness harness;
    CrtRandom random(2);
    CrtRandom replay(2);
    int owner = 0;
    const std::int16_t itemType = 0x650;
    ModelEffect& trail = Alive(*harness.effects, 0, 0xf3, 0, 3);
    trail.velocity = {0, 50, 0};
    trail.ownerContext = &owner;
    trail.hasOwner = true;
    trail.itemType = &itemType;
    trail.disturbanceState = 9;
    trail.byte89 = 2;
    const auto stepped = harness.Step(trail, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(trail.life, 2);
    EXPECT_EQ(trail.transform.position, glm::vec3(1000, 2000, 300));
    const ModelEffect& item = harness.effects->Effects()[1];
    ASSERT_TRUE(item.alive);
    EXPECT_EQ(item.type, 0xf4);
    EXPECT_EQ(item.subtype, 1);
    EXPECT_EQ(item.ownerContext, &owner);
    EXPECT_EQ(item.itemType, &itemType);
    EXPECT_EQ(item.disturbanceState, 9);
    EXPECT_EQ(item.byte89, 2);
    EXPECT_EQ(item.word8c, 4);
    Skip(replay, 1 + OwnRecordCloseRandoms);
    EXPECT_EQ(random.State(), replay.State());
}

TEST(BlastModelEffectsTest, TheRidingSpearMovesWithoutTurningItsVelocity)
{
    StepHarness harness;
    CrtRandom random(2);
    ModelEffect& spear = Alive(*harness.effects, 0, 0x10f, 0, 20);
    spear.transform.angles = {0, 0, 90};
    spear.velocity = {5, 0, 0};
    const auto stepped = harness.Step(spear, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(spear.transform.position, glm::vec3(1005, 2000, 300));
    EXPECT_EQ(spear.life, 19);
}

TEST(BlastModelEffectsTest, TheProtectionFollowsItsOwnerAndDiesWhenTheOwnerIsNotSeen)
{
    StepHarness harness;
    CrtRandom random(2);
    const glm::vec3 position{10, 20, 30};
    const bool alive = true;
    bool seen = true;
    ModelEffect& protection = Alive(*harness.effects, 0, 0xe4, 0, 10000);
    protection.owner.position = &position;
    protection.owner.alive = &alive;
    protection.owner.seen = &seen;
    ASSERT_TRUE(harness.Step(protection, random).HasValue());
    EXPECT_TRUE(protection.alive);
    EXPECT_EQ(protection.transform.position, position);
    EXPECT_EQ(protection.transform.angles.z, 10.0f);
    EXPECT_EQ(protection.objectState.light, std::bit_cast<float>(0x3fa66666u));
    seen = false;
    ASSERT_TRUE(harness.Step(protection, random).HasValue());
    EXPECT_FALSE(protection.alive);
}

TEST(BlastModelEffectsTest, TheAirForceWithoutOwnerDiesWithoutTheTail)
{
    StepHarness harness;
    CrtRandom random(2);
    ModelEffect& air = Alive(*harness.effects, 0, 0x113, 1, 20);
    air.hasOwner = false;
    ASSERT_TRUE(harness.Step(air, random).HasValue());
    EXPECT_FALSE(air.alive);
    EXPECT_EQ(air.life, 20);
}

TEST(BlastModelEffectsTest, TheFlameRingLightsEighteenFlames)
{
    StepHarness harness;
    CrtRandom random(2);
    CrtRandom replay(2);
    ModelEffect& ring = Alive(*harness.effects, 0, 0x5de, 1, 10);
    const auto stepped = harness.Step(ring, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const auto flames = std::ranges::count_if(harness.particles->All(), [](const Client::World::Particle& particle)
                                              { return particle.alive && particle.type == 0x5de; });
    EXPECT_EQ(flames, 18);
    EXPECT_EQ(ring.life, 9);
    // La luz (1) y, por llama, los dos corrimientos (& 63) − 32 (36 en total) antes del alta de la 5de/0 (0x54150e),
    // que saca cinco o seis más.
    Skip(replay, 1);
    auto twin = std::make_unique<Client::World::Particles>();
    for (int flame = 0; flame < 18; ++flame)
    {
        Skip(replay, 2);
        const auto made =
            twin->CreateChecked(0x5de, {}, {}, glm::vec3(1.0f), 0, std::bit_cast<float>(0x3f99999au), 0.0f, replay);
        ASSERT_TRUE(made.HasValue());
    }
    EXPECT_EQ(random.State(), replay.State());
}

TEST(BlastModelEffectsTest, TheBlizzardShardSoundsOnlyAtLifeEighteenWithKeyOne)
{
    StepHarness harness;
    CrtRandom random(2);
    ModelEffect& shard = Alive(*harness.effects, 0, 0x108, 1, 18);
    shard.disturbanceState = 1;
    shard.objectState.light = 1.0f;
    ASSERT_TRUE(harness.Step(shard, random).HasValue());
    EXPECT_EQ(harness.sounds, std::vector<std::int32_t>{0x91});
    EXPECT_EQ(shard.objectState.light, std::bit_cast<float>(0x3f68ba2eu));
    ASSERT_TRUE(harness.Step(shard, random).HasValue());
    EXPECT_EQ(harness.sounds.size(), 1u);
}

TEST(BlastModelEffectsTest, TheComboGrowsByItsAccelerationUntilItsLastFourSteps)
{
    StepHarness harness;
    CrtRandom random(2);
    ModelEffect& combo = Alive(*harness.effects, 0, 0x112, 0, 5);
    combo.transform.scale = 1.0f;
    combo.turnDirection = 0.1f;
    combo.objectState.light = 1.0f;
    ASSERT_TRUE(harness.Step(combo, random).HasValue());
    EXPECT_EQ(combo.transform.scale, F32(1.0 + static_cast<double>(0.1f)));
    EXPECT_EQ(combo.turnDirection, F32(static_cast<double>(0.1f) + static_cast<double>(0.1f)));
    EXPECT_EQ(combo.objectState.light, std::bit_cast<float>(0x3f36db6eu));
    ASSERT_TRUE(harness.Step(combo, random).HasValue());
    EXPECT_EQ(combo.transform.scale, F32(1.0 + static_cast<double>(0.1f)));
}

TEST(BlastModelEffectsTest, TheRisingInfernoKeepsTheUnroundedSumOnTheStack)
{
    StepHarness harness;
    CrtRandom random(2);
    const glm::vec3 position{10, 20, 30};
    ModelEffect& inferno = Alive(*harness.effects, 0, 0xf6, 4, 35);
    inferno.owner.position = &position;
    inferno.transform.scale = 0.1f;
    inferno.turnDirection = 0.0f;
    ASSERT_TRUE(harness.Step(inferno, random).HasValue());
    const double scale = static_cast<double>(0.1f) + static_cast<double>(0.003f);
    EXPECT_EQ(inferno.transform.scale, F32(scale));
    EXPECT_EQ(inferno.turnDirection, F32(scale * 24.0));
    EXPECT_EQ(inferno.transform.position.z, F32(scale * 24.0 + 30.0));
}

TEST(BlastModelEffectsTest, ThePiercingChildDiesWithItsOwner)
{
    StepHarness harness;
    CrtRandom random(2);
    ModelEffect& stab = Alive(*harness.effects, 0, 0x106, 1, 1);
    stab.alive = false;
    ModelEffect& child = Alive(*harness.effects, 1, 0x105, 1, 100);
    child.owner.effect = &stab;
    ASSERT_TRUE(harness.Step(child, random).HasValue());
    EXPECT_FALSE(child.alive);
    EXPECT_EQ(child.life, -2);
}

TEST(BlastModelEffectsTest, ThePiercingChildFollowsItsOwnerAndAsksForItsJoint)
{
    StepHarness harness;
    CrtRandom random(2);
    ModelEffect& stab = Alive(*harness.effects, 0, 0x106, 1, 20);
    stab.transform.position = {50, 60, 70};
    ModelEffect& child = Alive(*harness.effects, 1, 0x105, 1, 100);
    child.owner.effect = &stab;
    ASSERT_TRUE(harness.Step(child, random).HasValue());
    EXPECT_EQ(harness.joints, std::vector<std::int32_t>{0x614});
    EXPECT_EQ(child.transform.position, stab.transform.position);
    EXPECT_EQ(child.start, glm::vec3(1000, 2000, 300));
    EXPECT_EQ(child.copiedAngles.y, 90.0f);
}

// La primera semilla desde 1 con rand() % `modulo` == `value` después de `skipped` rand().
std::uint32_t SeedWhere(int skipped, int modulo, int value)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom probe(seed);
        Skip(probe, skipped);
        if (probe.Next() % modulo == value)
            return seed;
    }
}

// 0x5215a5..0x5216d6: la roca 0xe7 toca el terreno fuera de las celdas con el bit 0x08: queda en la altura, muerta, con
// el temblor de 0x521619 y cinco pedazos 0xea/0xeb por el camino de las piedritas (0x51b72b, no el del 0xca): el
// subtipo 0, +0x64 en −1, +0x68 en 1, la z del terreno, la luz de la roca y cinco rand() cada uno. Después, con el giro
// ya sumado, el polvo 5f2/3 (0x5216d6, cuatro rand() en 0x541cf2), sin la cola.
TEST(BlastModelEffectsTest, TheIceRockLandsShakingTheCameraAndDropsFiveShards)
{
    StepHarness harness;
    harness.ground = 400.0f;
    CrtRandom random(7);
    CrtRandom replay(7);
    constexpr std::size_t RockSlot = 10;
    constexpr std::size_t Shards = 5;
    ModelEffect& rock = Alive(*harness.effects, RockSlot, 0xe7, 0, 30);
    rock.transform.scale = 1.0f;
    rock.light = glm::vec3(0.5f);
    const auto stepped = harness.Step(rock, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(rock.alive);
    EXPECT_EQ(rock.transform.position, glm::vec3(1000, 2000, 400));
    EXPECT_EQ(rock.transform.angles.x, 10.0f);
    Skip(replay, 1);
    EXPECT_EQ(harness.shake, F32(static_cast<double>(replay.Next() % 8 - 8) * static_cast<double>(0.1f)));
    for (std::size_t slot = 0; slot < Shards; ++slot)
    {
        SCOPED_TRACE(slot);
        const ModelEffect& shard = harness.effects->Effects()[slot];
        ASSERT_TRUE(shard.alive);
        EXPECT_EQ(shard.type, 0xea + replay.Next() % 2);
        EXPECT_EQ(shard.subtype, 0);
        EXPECT_EQ(shard.objectState.blendMesh, -1);
        EXPECT_EQ(shard.objectState.light, 1.0f);
        EXPECT_EQ(shard.transform.position, glm::vec3(1000, 2000, 400));
        EXPECT_EQ(shard.light, glm::vec3(0.5f));
        Skip(replay, 1);
        EXPECT_EQ(shard.life, (replay.Next() & 15) + 32);
        Skip(replay, 3);
    }
    EXPECT_FALSE(harness.effects->Effects()[Shards].alive);
    EXPECT_EQ(AliveParticles(*harness.particles, Client::World::BreathParticle, 3), 1);
    ReplayParticle(replay, Client::World::BreathParticle, 3);
    EXPECT_EQ(random.State(), replay.State());

    // Sin el temblor, el corte de 0x52163e llega después del rand() de 0x521619.
    StepHarness still;
    still.ground = 400.0f;
    still.withShake = false;
    CrtRandom stillRandom(7);
    CrtRandom stillReplay(7);
    ModelEffect& stillRock = Alive(*still.effects, RockSlot, 0xe7, 0, 30);
    stillRock.transform.scale = 1.0f;
    const auto cut = still.Step(stillRock, stillRandom);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52163e"), std::string::npos) << cut.Error();
    Skip(stillReplay, 2);
    EXPECT_EQ(stillRandom.State(), stillReplay.State());
}

// 0x521616 apaga la roca antes de 0x521675: si su lugar es el primero libre, el primer pedazo cae en él, y
// 0x521684..0x5216d4 mueven y giran ese pedazo con su velocidad y su escala.
TEST(BlastModelEffectsTest, TheFirstShardTakesTheRockSlotWhenItIsTheFirstFree)
{
    StepHarness harness;
    harness.ground = 400.0f;
    CrtRandom random(7);
    ModelEffect& rock = Alive(*harness.effects, 0, 0xe8, 0, 30);
    rock.transform.scale = 1.0f;
    const auto stepped = harness.Step(rock, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_TRUE(rock.alive);
    EXPECT_TRUE(rock.type == 0xea || rock.type == 0xeb);
    EXPECT_EQ(rock.transform.position.x, F32(static_cast<double>(rock.velocity.x) + 1000.0));
    EXPECT_EQ(rock.transform.position.y, F32(static_cast<double>(rock.velocity.y) + 2000.0));
    EXPECT_EQ(rock.transform.position.z, 400.0f);
    EXPECT_EQ(rock.transform.angles.x, F32(10.0 / static_cast<double>(rock.transform.scale)));
    EXPECT_TRUE(harness.effects->Effects()[4].alive);
    EXPECT_FALSE(harness.effects->Effects()[5].alive);
}

// En el aire, o sobre una celda con el bit 0x08 (0x5215f4), la roca sigue cayendo sin temblor ni pedazos: sólo el
// rand() de la luz y los del polvo 5f2/3 de 0x5216d6.
TEST(BlastModelEffectsTest, TheIceRockKeepsFallingInTheAirOrOverAnOpenCell)
{
    for (const bool open : {false, true})
    {
        SCOPED_TRACE(open);
        StepHarness harness;
        harness.ground = open ? 400.0f : 0.0f;
        if (open)
            harness.attributes->cells[Client::Data::Terrain::CellIndex(10, 20)] = 0x08;
        CrtRandom random(7);
        CrtRandom replay(7);
        ModelEffect& rock = Alive(*harness.effects, 0, open ? 0xe8 : 0xe9, 0, 30);
        rock.transform.scale = 2.0f;
        rock.velocity = {0, 0, -40};
        const auto stepped = harness.Step(rock, random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_TRUE(rock.alive);
        EXPECT_EQ(rock.transform.position, glm::vec3(1000, 2000, 260));
        EXPECT_EQ(rock.transform.angles.x, 5.0f);
        EXPECT_EQ(harness.shake, 0.0f);
        EXPECT_FALSE(harness.effects->Effects()[1].alive);
        EXPECT_EQ(AliveParticles(*harness.particles, Client::World::BreathParticle, 3), 1);
        Skip(replay, 1);
        ReplayParticle(replay, Client::World::BreathParticle, 3);
        EXPECT_EQ(random.State(), replay.State());
    }
}

// 0x52186e..0x521bf6: el 0xf4/2 de un ítem largo (0x650) queda 180 detrás de su dueño, con el alfa 0,5 y 18 menos de
// giro; suelta el polvo 5f2/3 (0x521960) y, fuera de Kalima, cuatro chispas con sus cuatro rand() y su joint 0x617/0
// (0x521b75); la primera, además, la 5c5/0 (0x521b9e, cinco rand()). Al final, el sprite 0x5ac 20 más arriba, y la
// cola le resta la vida.
TEST(BlastModelEffectsTest, TheSpinningItemStaysBehindItsOwnerWithItsDustAndSparks)
{
    StepHarness harness;
    CrtRandom random(7);
    CrtRandom replay(7);
    const glm::vec3 owner{500, 600, 70};
    const std::int16_t itemType = 0x650;
    ModelEffect& item = Alive(*harness.effects, 0, 0xf4, 2, 25);
    item.owner.position = &owner;
    item.itemType = &itemType;
    const auto stepped = harness.Step(item, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(item.alpha, 0.5f);
    EXPECT_EQ(item.transform.position, glm::vec3(500, 420, 70));
    EXPECT_EQ(item.transform.angles.z, -18.0f);
    EXPECT_EQ(item.life, 24);
    EXPECT_EQ(AliveParticles(*harness.particles, Client::World::BreathParticle, 3), 1);
    EXPECT_EQ(AliveParticles(*harness.particles, 0x5c5, 0), 1);
    EXPECT_EQ(harness.joints, (std::vector<std::int32_t>{0x617, 0x617, 0x617, 0x617}));
    Skip(replay, 1);
    ReplayParticle(replay, Client::World::BreathParticle, 3);
    Skip(replay, 4);
    ReplayParticle(replay, 0x5c5, 0);
    Skip(replay, 3 * 4);
    EXPECT_EQ(random.State(), replay.State());
}

// 0x521996..0x521aab y 0x521bb6: en Kalima (24 a 29 y 36), una sola chispa con sus cuatro rand(), sin el joint ni la
// 5c5/0: la 6f4/3 120 más arriba con la luz (0,5; 0,5; 0,5) (0x521aa3) y el sprite 0x5ac 60 más arriba.
TEST(BlastModelEffectsTest, TheSpinningItemDropsOneKalimaFallInKalima)
{
    for (const std::int32_t map : {24, 29, 36})
    {
        SCOPED_TRACE(map);
        StepHarness harness;
        harness.map = map;
        CrtRandom random(7);
        CrtRandom replay(7);
        const glm::vec3 owner{500, 600, 70};
        const std::int16_t itemType = 0x650;
        ModelEffect& item = Alive(*harness.effects, 0, 0xf4, 2, 25);
        item.owner.position = &owner;
        item.itemType = &itemType;
        const auto stepped = harness.Step(item, random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_TRUE(harness.joints.empty());
        EXPECT_EQ(AliveParticles(*harness.particles, 0x5c5, 0), 0);
        ASSERT_EQ(AliveParticles(*harness.particles, Client::World::KalimaWaterfallLast, 3), 1);
        const auto fall = std::ranges::find_if(harness.particles->All(), [](const auto& particle)
                                               { return particle.alive && particle.type == 0x6f4; });
        EXPECT_EQ(fall->light, glm::vec3(0.5f));
        EXPECT_EQ(fall->position.z, 190.0f);
        Skip(replay, 1);
        ReplayParticle(replay, Client::World::BreathParticle, 3);
        Skip(replay, 4);
        ReplayParticle(replay, Client::World::KalimaWaterfallLast, 3);
        EXPECT_EQ(random.State(), replay.State());
        const auto& sprite = harness.sprites->Sprites()[0];
        ASSERT_TRUE(sprite.alive);
        EXPECT_EQ(sprite.texture, 0x5ac);
        EXPECT_EQ(sprite.position, glm::vec3(500, 420, 130));
    }
}

// 0x52552d..0x52569a: el 0x108/0 se hamaca (los dos rand() del seno y el de la caída), suelta el aliento 5f2/0, sortea
// entre la 5ca/1 y la 5db/7 (0x5255fd), sube la luz con los sprites 0x5fd y 0x5ac (tres rand()) y, en el aire, no hace
// más.
TEST(BlastModelEffectsTest, TheIceStormSwaysBreathesAndDropsItsIceOrItsFire)
{
    const auto icyOf = [](std::uint32_t seed)
    {
        CrtRandom probe(seed);
        Skip(probe, 4);
        ReplayParticle(probe, Client::World::BreathParticle, 0, 1.5f);
        return probe.Next() % 2 == 0;
    };
    for (const bool icy : {true, false})
    {
        SCOPED_TRACE(icy);
        std::uint32_t seed = 1;
        while (icyOf(seed) != icy)
            ++seed;
        StepHarness harness;
        CrtRandom random(seed);
        CrtRandom replay(seed);
        ModelEffect& storm = Alive(*harness.effects, 0, 0x108, 0, 20);
        storm.start = {1000, 2000, 300};
        storm.transform.scale = 1.0f;
        const auto stepped = harness.Step(storm, random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        Skip(replay, 3);
        EXPECT_EQ(storm.turnDirection, F32(-static_cast<double>(replay.Next() % 5)));
        EXPECT_EQ(storm.start.x, 990.0f);
        EXPECT_EQ(storm.transform.position.z, 300.0f);
        EXPECT_EQ(storm.light, glm::vec3(F32(1.0 + static_cast<double>(0.1f))));
        ReplayParticle(replay, Client::World::BreathParticle, 0, 1.5f);
        Skip(replay, 1);
        if (icy)
            ReplayParticle(replay, 0x5ca, 1, 0.5f);
        else
            ReplayParticle(replay, Client::World::DeathSpiritFireParticle, 7);
        Skip(replay, 3);
        EXPECT_EQ(random.State(), replay.State());
        EXPECT_EQ(AliveParticles(*harness.particles, Client::World::BreathParticle, 0), 1);
        EXPECT_EQ(AliveParticles(*harness.particles, icy ? 0x5ca : Client::World::DeathSpiritFireParticle, icy ? 1 : 7),
                  1);
    }
}

// 0x5257e8..0x52bfc6: el 0x108/2 se hamaca con 50; en el terreno o debajo no hace más (cuatro rand()); en el aire, el
// rand() % 2 en 0 suelta la 5db/7 (0x5258ab), y en los dos casos la luz sube 0,1f con los sprites 0x5fd y 0x5ac (tres
// rand() más).
TEST(BlastModelEffectsTest, TheFallingIceStormOnlyBrightensInTheAir)
{
    {
        StepHarness harness;
        harness.ground = 400.0f;
        CrtRandom random(7);
        CrtRandom replay(7);
        ModelEffect& storm = Alive(*harness.effects, 0, 0x108, 2, 20);
        storm.start = {1000, 2000, 300};
        ASSERT_TRUE(harness.Step(storm, random).HasValue());
        EXPECT_EQ(storm.start.x, 990.0f);
        EXPECT_EQ(storm.light, glm::vec3(1.0f));
        EXPECT_EQ(storm.life, 19);
        Skip(replay, 4);
        EXPECT_EQ(random.State(), replay.State());
    }
    for (const int parity : {0, 1})
    {
        SCOPED_TRACE(parity);
        const std::uint32_t seed = SeedWhere(4, 2, parity);
        StepHarness harness;
        CrtRandom random(seed);
        CrtRandom replay(seed);
        ModelEffect& storm = Alive(*harness.effects, 0, 0x108, 2, 20);
        storm.start = {1000, 2000, 300};
        storm.transform.scale = 1.0f;
        const auto stepped = harness.Step(storm, random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(storm.light, glm::vec3(F32(1.0 + static_cast<double>(0.1f))));
        EXPECT_EQ(storm.life, 19);
        Skip(replay, 5);
        if (parity == 0)
            ReplayParticle(replay, Client::World::DeathSpiritFireParticle, 7);
        Skip(replay, 3);
        EXPECT_EQ(AliveParticles(*harness.particles, Client::World::DeathSpiritFireParticle, 7), parity == 0 ? 1 : 0);
        EXPECT_EQ(random.State(), replay.State());
    }
}

// 0x5251df..0x525318: la piedra de la puerta avanza, frena 0,9f (la 0x10c/1 vuelve a acelerar 1,1f), cae 4 y, en el
// terreno, rebota a la mitad, pierde 5 de vida y se inclina 128 por la escala. El rand() % 10 del polvo corta sólo con
// un lugar libre en las partículas (0x5252e6).
TEST(BlastModelEffectsTest, TheGateDebrisBouncesOnTheGround)
{
    for (const int kind : {0x10c, 0x10d})
    {
        SCOPED_TRACE(kind);
        StepHarness harness;
        harness.ground = 400.0f;
        for (Client::World::Particle& particle : harness.particles->All())
            particle.alive = true;
        CrtRandom random(7);
        CrtRandom replay(7);
        ModelEffect& debris = Alive(*harness.effects, 0, static_cast<std::int16_t>(kind), kind == 0x10c ? 1 : 0, 30);
        debris.transform.scale = 1.0f;
        debris.velocity = {10, 0, 0};
        debris.turnDirection = 5.0f;
        ASSERT_TRUE(harness.Step(debris, random).HasValue());
        const float braked = F32(10.0 * static_cast<double>(0.9f));
        const float speed = kind == 0x10c ? F32(static_cast<double>(braked) * static_cast<double>(1.1f)) : braked;
        EXPECT_EQ(debris.velocity, glm::vec3(speed, 0, 0));
        EXPECT_EQ(debris.transform.position.z, 400.0f);
        EXPECT_EQ(debris.turnDirection, -0.5f);
        EXPECT_EQ(debris.life, 24);
        EXPECT_EQ(debris.transform.angles.x, -128.0f);
        Skip(replay, 2);
        EXPECT_EQ(random.State(), replay.State());
    }
    StepHarness harness;
    CrtRandom random(SeedWhere(1, 10, 0));
    ModelEffect& debris = Alive(*harness.effects, 0, 0x10d, 0, 30);
    debris.transform.scale = 1.0f;
    const auto stepped = harness.Step(debris, random);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x5252e6"), std::string::npos) << stepped.Error();
}

// 0x5214ea..0x521570: el círculo 0xec/1 crece 0,01f con su luz; el /0 toma la posición de su dueño y crece igual. Sólo
// el rand() de la luz.
TEST(BlastModelEffectsTest, TheMagicCircleGrowsAndTheFirstFollowsItsOwner)
{
    for (const int subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        StepHarness harness;
        CrtRandom random(7);
        CrtRandom replay(7);
        const glm::vec3 owner{10, 20, 30};
        ModelEffect& circle = Alive(*harness.effects, 0, 0xec, subtype, 20);
        circle.transform.scale = 1.0f;
        circle.owner.position = &owner;
        ASSERT_TRUE(harness.Step(circle, random).HasValue());
        EXPECT_EQ(circle.transform.scale, F32(1.0 + static_cast<double>(0.01f)));
        EXPECT_EQ(circle.transform.position, subtype == 0 ? owner : glm::vec3(1000, 2000, 300));
        EXPECT_EQ(circle.life, 19);
        Skip(replay, 1);
        EXPECT_EQ(random.State(), replay.State());
    }
}

// 0x5212b6..0x521316: la onda 0x107 deja +0x68 en la vida por 0,05f y prende su luz; sólo el rand() de la luz.
TEST(BlastModelEffectsTest, TheWaveForceFadesWithItsLife)
{
    StepHarness harness;
    CrtRandom random(7);
    CrtRandom replay(7);
    ModelEffect& wave = Alive(*harness.effects, 0, 0x107, 0, 10);
    ASSERT_TRUE(harness.Step(wave, random).HasValue());
    EXPECT_EQ(wave.objectState.light, F32(10.0 * static_cast<double>(0.05f)));
    EXPECT_EQ(wave.life, 9);
    Skip(replay, 1);
    EXPECT_EQ(random.State(), replay.State());
}

// 0x5210a1..0x5210e0: el escudo 0x109 toma la posición de su dueño y, con menos de 10 de vida, deja +0x68 en la vida
// por 0,1f; sólo el rand() de la luz.
TEST(BlastModelEffectsTest, TheShieldFollowsItsOwnerAndFadesInItsLastSteps)
{
    for (const std::int32_t life : {12, 5})
    {
        SCOPED_TRACE(life);
        StepHarness harness;
        CrtRandom random(7);
        CrtRandom replay(7);
        const glm::vec3 owner{10, 20, 30};
        ModelEffect& shield = Alive(*harness.effects, 0, 0x109, 0, life);
        shield.owner.position = &owner;
        ASSERT_TRUE(harness.Step(shield, random).HasValue());
        EXPECT_EQ(shield.transform.position, owner);
        EXPECT_EQ(shield.objectState.light,
                  life < 10 ? F32(static_cast<double>(life) * static_cast<double>(0.1f)) : 1.0f);
        Skip(replay, 1);
        EXPECT_EQ(random.State(), replay.State());
    }
}

// 0x523974..0x523b3a: la piedra 0xf2/0 cae 3; en el terreno rebota a la mitad, pierde 4 de vida y se inclina 128 por la
// escala; el rand() % 10 de 0x523adb sale sin partícula.
TEST(BlastModelEffectsTest, TheFallingRockBouncesAndDrawsItsUnusedTenth)
{
    StepHarness harness;
    harness.ground = 400.0f;
    CrtRandom random(7);
    CrtRandom replay(7);
    ModelEffect& rock = Alive(*harness.effects, 0, 0xf2, 0, 20);
    rock.transform.scale = 1.0f;
    ASSERT_TRUE(harness.Step(rock, random).HasValue());
    EXPECT_EQ(rock.transform.position.z, 400.0f);
    EXPECT_EQ(rock.turnDirection, 1.5f);
    EXPECT_EQ(rock.life, 15);
    EXPECT_EQ(rock.transform.angles.x, -128.0f);
    Skip(replay, 2);
    EXPECT_EQ(random.State(), replay.State());
}

// 0x523d3a..0x523dd4: la puñalada 0x106 suelta el aliento 5f2/0 con el rand() % 2 en 0, dos fuegos 5d9/5 y su luz.
TEST(BlastModelEffectsTest, TheDeathStabBreathesHalfTheTimeAndLightsTwoFires)
{
    for (const int parity : {0, 1})
    {
        SCOPED_TRACE(parity);
        const std::uint32_t seed = SeedWhere(1, 2, parity);
        StepHarness harness;
        CrtRandom random(seed);
        CrtRandom replay(seed);
        ModelEffect& stab = Alive(*harness.effects, 0, 0x106, 0, 30);
        ASSERT_TRUE(harness.Step(stab, random).HasValue());
        EXPECT_EQ(stab.life, 29);
        const glm::vec3 white(1.0f);
        const glm::vec3 at{1000, 2000, 300};
        auto twin = std::make_unique<Client::World::Particles>();
        Skip(replay, 2);
        if (parity == 0)
        {
            ASSERT_TRUE(twin->CreateChecked(0x5f2, at, {}, white, 0, 1.0f, 0.0f, replay).HasValue());
        }
        for (int fire = 0; fire < 2; ++fire)
        {
            ASSERT_TRUE(twin->CreateChecked(0x5d9, at, {}, white, 5, 1.0f, 0.0f, replay).HasValue());
        }
        EXPECT_EQ(random.State(), replay.State());
        const auto made = std::ranges::count_if(harness.particles->All(),
                                                [](const Client::World::Particle& particle) { return particle.alive; });
        EXPECT_EQ(made, parity == 0 ? 3 : 2);
    }
}
} // namespace
