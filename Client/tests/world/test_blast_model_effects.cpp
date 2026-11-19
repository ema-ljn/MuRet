#include "World/BlastModelEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "World/GameModelEffects.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"

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
using Client::Render::Models::CrtRandom;
using Client::World::BlastEffectServices;
using Client::World::FishEffectRequest;
using Client::World::ModelEffect;
using Client::World::ModelEffectArguments;
using Client::World::ModelEffects;

constexpr int OwnRecordCloseRandoms = 0xf08;

float F32(double value)
{
    return static_cast<float>(value);
}

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
                .sound = [this](std::int32_t id) { sounds.push_back(id); }};
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
    const glm::vec3 offset = Client::Render::Models::OriginalVectorRotate(
        {-10, -60, 135}, Client::Render::Models::OriginalAngleMatrix({0, 0, 90}));
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
    const double argument = 90.0 * static_cast<double>(std::bit_cast<float>(0x3c8efa35u));
    EXPECT_EQ(spear.velocity.x, F32(*Client::Render::Models::Detail::SineProductPC53(argument, 5.0f)));
    EXPECT_EQ(spear.velocity.y, F32(*Client::Render::Models::Detail::CosineDoubleProductPC53(argument, -5.0)));
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
    std::unique_ptr<Client::World::TerrainLighting> lighting = std::make_unique<Client::World::TerrainLighting>();
    std::unique_ptr<Client::World::FrameSprites> sprites = std::make_unique<Client::World::FrameSprites>();
    std::int32_t map = 0;
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
                .map = &map,
                .sound = [this](std::int32_t id) { sounds.push_back(id); },
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

TEST(BlastModelEffectsTest, TheBlastOnTheGroundDropsSixChipsAndCutsAtTheUnportedBurst)
{
    StepHarness harness;
    CrtRandom random(2);
    ModelEffect& blast = Alive(*harness.effects, 0, 0xf5, 0, 30);
    harness.ground = 400.0f;
    const auto stepped = harness.Step(blast, random);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x5211a0"), std::string::npos) << stepped.Error();
    EXPECT_EQ(blast.transform.position.z, 400.0f);
    EXPECT_EQ(blast.velocity, glm::vec3(0.0f));
    const auto chips = std::ranges::count_if(harness.effects->Effects(), [](const ModelEffect& effect)
                                             { return effect.alive && (effect.type == 0xc8 || effect.type == 0xc9); });
    EXPECT_EQ(chips, 6);
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
    ModelEffect& ring = Alive(*harness.effects, 0, 0x5de, 1, 10);
    const auto stepped = harness.Step(ring, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const auto flames = std::ranges::count_if(harness.particles->All(), [](const Client::World::Particle& particle)
                                              { return particle.alive && particle.type == 0x5de; });
    EXPECT_EQ(flames, 18);
    EXPECT_EQ(ring.life, 9);
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

TEST(BlastModelEffectsTest, ThePiercingChildCutsAtTheJointsBatchJoint)
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
} // namespace
