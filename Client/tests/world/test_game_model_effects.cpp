#include "Models/IModelLibrary.h"
#include "Effects/GameModelEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Scenes/GameDrawTail.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Render/ModelBatch.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Data/Models/Model.h"
#include "Core/IClock.h"
#include "Terrain/SphereFrustum.h"
#include "Effects/GameParticles.h"
#include "Kernel/WorldState.h"
#include "Kernel/WorldMap.h"
#include "Kernel/JointRequest.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Kernel/KalimaWater.h"
#include "MapScripts/Kalima/KalimaWorld.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/ParticleOwner.h"
#include "Particles/SelectionParticleKernels.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/DynamicWorldLight.h"
#include "Audio/SoundId.h"

#include "support/KalimaWaves.h"
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
#include <span>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
// 0x523b8b..0x523d35 with subtype 3 (the 0xf8/1's pulse): the same step as trail 0x105/0. With the arrow alive, +0xe4
// 90 more, its angles in +0x1c and +0x28, the start at the previous position and the position at the arrow's; the
// pointer keeps the slot even if the type changes. With the arrow dead, life at −1 and the tail frees it.
TEST(GameModelEffectsTest, TheVolleyPulseStepsLikeTheTrailAndRetiresWithItsArrow)
{
    auto particles = std::make_unique<Client::Particles::Particles>();
    Client::Effects::ModelEffect projectile;
    projectile.alive = true;
    projectile.type = 0xf8;
    projectile.subtype = 1;
    projectile.transform.position = {10, 20, 30};
    projectile.transform.angles = {0, 60, 90};
    Client::Effects::ModelEffect pulse;
    pulse.alive = true;
    pulse.type = 0x105;
    pulse.subtype = 3;
    pulse.life = 10;
    pulse.transform.position = {1, 2, 3};
    pulse.owner.effect = &projectile;
    Client::Crt::CrtRandom random(1);
    const std::int32_t map = 34;
    const Client::Effects::ModelEffectStepServices services{.map = &map};

    auto result = Client::Effects::StepGameModelEffect(pulse, nullptr, *particles, 0, random, nullptr, services);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(pulse.alive);
    EXPECT_EQ(pulse.life, 9);
    EXPECT_EQ(pulse.turnDirection, 90.0f);
    EXPECT_EQ(pulse.start, (glm::vec3{1, 2, 3}));
    EXPECT_EQ(pulse.transform.angles, projectile.transform.angles);
    EXPECT_EQ(pulse.copiedAngles, (glm::vec3{0, 90, 90}));
    projectile.type = 0xc1a;
    result = Client::Effects::StepGameModelEffect(pulse, nullptr, *particles, 0, random, nullptr, services);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(pulse.alive);
    EXPECT_EQ(pulse.turnDirection, 180.0f);
    projectile.alive = false;
    result = Client::Effects::StepGameModelEffect(pulse, nullptr, *particles, 0, random, nullptr, services);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FALSE(pulse.alive);
    EXPECT_EQ(pulse.life, -2);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 3).State());
}

TEST(GameModelEffectsTest, TheShortBowTrailFollowsThePhysicalArrowSlotAndRunsItsCommonTail)
{
    Client::Particles::Particles particles;
    Client::Effects::ModelEffect arrow;
    arrow.alive = true;
    arrow.type = 0xdb;
    arrow.transform.position = {100, 200, 300};
    arrow.transform.angles = {10, 20, 30};
    Client::Effects::ModelEffect trail;
    trail.alive = true;
    trail.type = 0x105;
    trail.life = 100;
    trail.transform.position = {1, 2, 3};
    trail.owner.effect = &arrow;
    Client::Crt::CrtRandom random(1);
    const std::int32_t map = 0;
    const Client::Effects::ModelEffectStepServices services{.map = &map};
    auto result = Client::Effects::StepGameModelEffect(trail, nullptr, particles, 0, random, nullptr, services);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(trail.life, 99);
    EXPECT_EQ(trail.turnDirection, 90);
    EXPECT_EQ(trail.start, (glm::vec3{1, 2, 3}));
    EXPECT_EQ(trail.transform.position, arrow.transform.position);
    EXPECT_EQ(trail.transform.angles, arrow.transform.angles);
    EXPECT_EQ(trail.copiedAngles, (glm::vec3{10, 90, 30}));
    arrow.type = 0x123; // The original follows the reused slot, not a generation or a type.
    arrow.transform.position = {400, 500, 600};
    ASSERT_TRUE(
        Client::Effects::StepGameModelEffect(trail, nullptr, particles, 0, random, nullptr, services).HasValue());
    EXPECT_EQ(trail.life, 98);
    EXPECT_EQ(trail.turnDirection, 180);
    EXPECT_EQ(trail.start, (glm::vec3{100, 200, 300}));
    EXPECT_EQ(trail.transform.position, arrow.transform.position);
    EXPECT_EQ(trail.copiedAngles, (glm::vec3{10, 180, 30}));
    arrow.alive = false;
    ASSERT_TRUE(
        Client::Effects::StepGameModelEffect(trail, nullptr, particles, 0, random, nullptr, services).HasValue());
    EXPECT_FALSE(trail.alive);
    EXPECT_EQ(trail.life, -2);
    EXPECT_EQ(trail.turnDirection, 180);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 3).State());
}

TEST(GameModelEffectsTest, TheShortBowTrailPublishesAndAdvancesAnimationEvenOnItsDeadOwnerTick)
{
    Client::Particles::Particles particles;
    Client::Effects::ModelEffect arrow;
    arrow.alive = true;
    arrow.transform.position = {100, 200, 300};
    Client::Effects::ModelEffect trail;
    trail.alive = true;
    trail.type = 0x105;
    trail.life = 100;
    trail.speed = 0.25f;
    trail.owner.effect = &arrow;
    trail.transform.position = {1, 2, 3};
    Client::Models::Skeleton skeleton;
    skeleton.actions.resize(1, {.repeats = false, .speed = 1, .keyCount = 2});
    Client::Crt::CrtRandom random(1);
    const std::int32_t map = 0;
    int published = 0;
    Client::Effects::ModelEffectStepServices services{.map = &map};
    services.publishAction = [&](std::uint16_t type, std::uint8_t action)
    {
        EXPECT_EQ(type, 0x105);
        EXPECT_EQ(action, 0);
        EXPECT_EQ(trail.animation.frame, published == 0 ? 0 : 0.25f);
        EXPECT_EQ(trail.life, published == 0 ? 100 : -1);
        ++published;
    };
    ASSERT_TRUE(
        Client::Effects::StepGameModelEffect(trail, &skeleton, particles, 0, random, nullptr, services).HasValue());
    EXPECT_EQ(trail.animation.frame, 0.25f);
    EXPECT_EQ(trail.previousPosition, (glm::vec3{1, 2, 3}));
    arrow.alive = false;
    ASSERT_TRUE(
        Client::Effects::StepGameModelEffect(trail, &skeleton, particles, 0, random, nullptr, services).HasValue());
    EXPECT_EQ(trail.animation.frame, 0.5f);
    EXPECT_EQ(trail.life, -2);
    EXPECT_FALSE(trail.alive);
    EXPECT_EQ(published, 2);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 2).State());
}

// 0x52d78f: trail 0x105/0 goes on through 0x52da92 → 0x602e90 with its mesh, Piercing.bmd (0x615700), in both world
// passes. In the second, 0x52d66a..0x52d693 filters it by blendMesh.
TEST(GameModelEffectsTest, TheShortBowTrailDrawsPiercingBmdInBothNativePasses)
{
    class TriangleModels final : public Client::Models::IModelLibrary
    {
    public:
        TriangleModels()
        {
            Client::Data::Models::Mesh mesh;
            mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
            mesh.normals = {{0, {0, 0, 1}, 0}};
            mesh.texCoords = {{0, 0}};
            mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
            mesh.textureName = "Piercing.jpg";
            model.meshes = {mesh};
            Client::Models::SkeletonBone bone;
            bone.actions = {{Client::Models::BoneKey{}}};
            skeleton.bones = {bone};
            skeleton.actions = {Client::Models::ActionInfo{.keyCount = 1}};
        }
        const Client::Data::Models::Model* Find(Client::Models::ModelId id) const override
        {
            found.push_back(id.Value());
            return &model;
        }
        const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId) const override
        {
            return &skeleton;
        }
        std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
        {
            return textures;
        }
        Client::Data::Models::Model model;
        Client::Models::Skeleton skeleton;
        std::array<Client::Render::TextureSlot, 1> textures{Client::Render::TextureSlot{0x400}};
        mutable std::vector<std::uint16_t> found;
    } models;
    Client::Effects::ModelEffect trail;
    trail.alive = true;
    trail.type = 0x105;
    trail.objectState.blendMesh = 0;
    trail.alpha = 1;
    const Client::Terrain::SphereFrustum frustum;
    for (const std::uint8_t mode : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}})
    {
        SCOPED_TRACE(mode);
        models.found.clear();
        Client::Render::ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        const auto drawn = Client::Scenes::DrawGameModelEffects(batch, models, std::span(&trail, 1), frustum, nullptr,
                                                                Client::Core::Duration{}, 1, mode);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_TRUE(trail.seen);
        ASSERT_FALSE(models.found.empty());
        EXPECT_TRUE(std::ranges::all_of(models.found, [](std::uint16_t id) { return id == 0x105; }));
        EXPECT_FALSE(batch.Vertices().empty());
    }
    trail.objectState.blendMesh = -1;
    Client::Render::ModelBatch filtered;
    filtered.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Client::Scenes::DrawGameModelEffects(filtered, models, std::span(&trail, 1), frustum, nullptr,
                                                     Client::Core::Duration{}, 1, 1)
                    .HasValue());
    EXPECT_TRUE(filtered.Vertices().empty());
}

TEST(GameModelEffectsTest, TheShortBowTrailOnlyDisturbsKalimaForThePublishedHeroArrow)
{
    for (const std::int32_t map : {23, 24, 29, 30, 36, 37})
        for (const bool heroOwner : {false, true})
        {
            SCOPED_TRACE(map);
            Client::Particles::Particles particles;
            auto water = std::make_unique<Client::MapScripts::Kalima::KalimaWorld>(map);
            auto expectedWater = water->CloneForFrame();
            int hero = 0;
            int other = 0;
            Client::Effects::ModelEffect arrow;
            arrow.alive = true;
            arrow.ownerContext = heroOwner ? &hero : &other;
            arrow.transform.position = {1000.25f, 2000.5f, 300};
            Client::Effects::ModelEffect trail;
            trail.alive = true;
            trail.type = 0x105;
            trail.life = 100;
            trail.owner.effect = &arrow;
            Client::Crt::CrtRandom random(7);
            auto expected = random;
            (void)expected.Next();
            if (heroOwner && (map == 24 || map == 29 || map == 36))
            {
                ASSERT_TRUE(expectedWater->Disturb(10, 20, 2, -200, expected).HasValue());
            }
            const Client::Effects::ModelEffectStepServices services{.map = &map, .hero = &hero};
            const auto stepped =
                Client::Effects::StepGameModelEffect(trail, nullptr, particles, 0, random, water.get(), services);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(water->Waves().planes, Client::Tests::WavesOf(*expectedWater).planes);
            EXPECT_EQ(random.State(), expected.State());
            EXPECT_EQ(trail.life, 99);
        }
}

TEST(GameModelEffectsTest, NoriaUpdatesLightAndPitchThenTheCommonLifeWithOneRandom)
{
    Client::Effects::ModelEffect effect;
    effect.alive = true;
    effect.type = 322;
    effect.life = 100;
    effect.turnDirection = 3.0f;
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1);
    const auto result = Client::Effects::StepGameModelEffect(effect, nullptr, particles, 0.0f, random);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(effect.light, glm::vec3(0.01f));
    EXPECT_EQ(effect.transform.angles.y, 7.0f);
    EXPECT_EQ(effect.life, 99);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());
}
TEST(GameModelEffectsTest, MagicalTeleportCreatesEighteenSparksWithThreeAnglesEach)
{
    Client::Effects::ModelEffect effect;
    effect.alive = true;
    effect.type = Client::Effects::TeleportEffect;
    effect.life = 10;
    effect.transform.position = {100.0f, 200.0f, 300.0f};
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1);
    const auto result = Client::Effects::StepGameModelEffect(effect, nullptr, particles, 0.0f, random);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(effect.life, 9);
    ASSERT_TRUE(particles.All()[0].alive);
    EXPECT_EQ(particles.All()[0].position.z, 324.0f);
    EXPECT_EQ(particles.All()[0].scale, 12.0f);
    EXPECT_EQ(particles.All()[0].subtype, 1);
    EXPECT_EQ(particles.All()[0].life, 2);
    EXPECT_EQ(particles.All()[17].position.z, 732.0f);
    EXPECT_EQ(particles.All()[17].scale, 6.0f);
    EXPECT_FALSE(particles.All()[18].alive);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 55).State());
}
TEST(GameModelEffectsTest, AFullSparkPoolConsumesOnlyTheModelEffectPrefix)
{
    Client::Effects::ModelEffect effect;
    effect.alive = true;
    effect.type = Client::Effects::TeleportEffect;
    effect.life = 10;
    Client::Particles::Particles particles;
    for (auto& particle : particles.All())
        particle.alive = true;
    Client::Crt::CrtRandom random(1);
    ASSERT_TRUE(Client::Effects::StepGameModelEffect(effect, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_EQ(effect.life, 9);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());
}
TEST(GameModelEffectsTest, TeleportSparksUseTheirOwnShrinkRatherThanTheRavenFade)
{
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1);
    const auto created =
        particles.CreateChecked(Client::Particles::SparkParticle, {}, {}, {1.0f, 0.5f, 0.25f}, 1, 12.0f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 3).State());
    const auto step = Client::Particles::StepParticlesChecked(particles, random);
    ASSERT_TRUE(step.HasValue());
    EXPECT_EQ(particles.All()[0].scale, 10.0f);
    EXPECT_EQ(particles.All()[0].light, glm::vec3(1.0f, 0.5f, 0.25f));
}
TEST(GameModelEffectsTest, NoriaUsesTheStoredBlueArgumentAtANonzeroClock)
{
    Client::Effects::ModelEffect effect;
    effect.alive = true;
    effect.type = 324;
    effect.life = 100;
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1);
    ASSERT_TRUE(Client::Effects::StepGameModelEffect(effect, nullptr, particles, 1000.0f, random).HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.light.x), 0x3e40c25fu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.light.y), 0x3e55553du);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.light.z), 0x3e4f93a4u);
}
TEST(GameModelEffectsTest, TextureEffectsSkipTheNativeModelAnimationRange)
{
    Client::Effects::ModelEffect effect;
    effect.alive = true;
    effect.type = Client::Effects::ArrivalEffect;
    effect.life = 100;
    effect.speed = 1.0f;
    effect.animation.frame = 1.0f;
    Client::Models::Skeleton skeleton;
    skeleton.actions.resize(1);
    skeleton.actions[0].keyCount = 4;
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1);
    ASSERT_TRUE(Client::Effects::StepGameModelEffect(effect, &skeleton, particles, 1000.0f, random).HasValue());
    EXPECT_EQ(effect.animation.frame, 1.0f);
    EXPECT_EQ(effect.life, 99);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());
}
TEST(GameModelEffectsTest, SparseParticleSlotsPreserveTheirPayloadAndConsumeOnlyTheirThreeAngles)
{
    Client::Effects::ModelEffect effect;
    effect.alive = true;
    effect.type = Client::Effects::TeleportEffect;
    effect.life = 10;
    Client::Particles::Particles particles;
    for (auto& particle : particles.All())
        particle.alive = true;
    particles.All()[5].alive = false;
    particles.All()[7].alive = false;
    particles.All()[5].field38 = 0x12345678;
    particles.All()[5].rotationFactor = 23.0f;
    Client::Crt::CrtRandom random(1);
    ASSERT_TRUE(Client::Effects::StepGameModelEffect(effect, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_EQ(particles.All()[5].scale, 12.0f);
    EXPECT_EQ(particles.All()[7].scale, 6.0f);
    EXPECT_EQ(particles.All()[5].position.z, 24.0f);
    EXPECT_EQ(particles.All()[7].position.z, 48.0f);
    EXPECT_EQ(particles.All()[5].field38, 0x12345678u);
    EXPECT_EQ(particles.All()[5].rotationFactor, 23.0f);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 7).State());
}
// 0x520d11..0x520fd1, the selection's 0x3c butterfly 0xae: the turn by +0x88, the drift and flap of the z speed, the
// brake against the terrain height (at +50 it rises, at +150 it falls), the z jump, the subtype's light and the 0x5ac
// sprite with scale 1 at its place, before the common step.
struct Butterfly
{
    Client::Effects::ModelEffect effect;
    Client::Particles::Particles particles;
    Client::Particles::FrameSprites sprites;
    double height = 1000.0;
    Client::Effects::ModelEffectStepServices services;

    explicit Butterfly(std::int32_t subtype)
    {
        effect.alive = true;
        effect.type = Client::Effects::ButterflyEffect;
        effect.subtype = subtype;
        effect.life = 250;
        effect.state = 1;
        effect.transform.position = {100.0f, 200.0f, 300.0f};
        effect.transform.angles = {0.0f, 0.0f, 10.0f};
        effect.light = {0.5f, 0.5f, 0.5f};
        services.terrainHeight = [this](float x, float y)
        {
            EXPECT_EQ(x, 100.0f);
            EXPECT_EQ(y, 200.0f);
            return height;
        };
        services.sprites = &sprites;
    }
};

TEST(GameModelEffectsTest, TheButterflyFlutterKeepsItsHeightBandAndGlowsWithItsSubtype)
{
    for (const double height : {1000.0, 0.0})
    {
        for (const std::uint8_t state : {std::uint8_t{0}, std::uint8_t{1}})
        {
            Butterfly butterfly(0);
            butterfly.height = height;
            butterfly.effect.state = state;
            Client::Crt::CrtRandom random(5);
            Client::Crt::CrtRandom draws(5);
            static_cast<void>(draws.Next()); // 0x5208c5.
            const int turn = draws.Next() % 10;
            float velocity = 0.0f;
            if ((draws.Next() & 31) == 0)
                velocity = static_cast<float>(draws.Next() % 15 - 7);
            velocity = static_cast<float>((draws.Next() % 15 - 7) * static_cast<double>(0.2f) + velocity);
            const double damping = static_cast<double>(0.8f);
            if (height + 50.0 > 300.0)
                velocity = static_cast<float>(velocity * damping + 1.0);
            if (height + 150.0 < 300.0)
                velocity = static_cast<float>(velocity * damping - 1.0);
            const float z = static_cast<float>((draws.Next() % 15 - 7) * static_cast<double>(0.3f) + 300.0);
            const double level = ((draws.Next() & 31) + 64) * static_cast<double>(0.01f);

            const auto stepped = Client::Effects::StepGameModelEffect(butterfly.effect, nullptr, butterfly.particles,
                                                                      0.0f, random, nullptr, butterfly.services);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(butterfly.effect.transform.angles.z, static_cast<float>(state > 0 ? 10.0 + turn : 10.0 - turn));
            EXPECT_EQ(butterfly.effect.velocity.z, velocity);
            const auto& sprite = butterfly.sprites.Sprites()[0];
            ASSERT_TRUE(sprite.alive);
            EXPECT_EQ(sprite.texture, 0x5ac);
            EXPECT_EQ(sprite.scale, 1.0f);
            EXPECT_EQ(sprite.position, glm::vec3(100.0f, 200.0f, z));
            EXPECT_EQ(sprite.light, glm::vec3(static_cast<float>(level * static_cast<double>(0.4f)),
                                              static_cast<float>(level * static_cast<double>(0.8f)),
                                              static_cast<float>(level * static_cast<double>(0.6f))));
            EXPECT_EQ(butterfly.effect.life, 249);
            EXPECT_EQ(random.State(), draws.State());
        }
    }
}

// 0x520ec6..0x520fb1: the /3 shines (0.7; 0.9; 0.5) and, with rand() & 1 at 0, releases a 0x5c5/7 with the effect's
// light, three shifts of (rand() & 15) − 8 drawn before being added.
TEST(GameModelEffectsTest, TheLargeButterflyDropsASparkOnEvenDraws)
{
    int sparks = 0;
    for (const std::uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u})
    {
        Butterfly butterfly(3);
        Client::Crt::CrtRandom random(seed);
        Client::Crt::CrtRandom draws(seed);
        static_cast<void>(draws.Next()); // 0x5208c5.
        static_cast<void>(draws.Next()); // The turn.
        if ((draws.Next() & 31) == 0)
            static_cast<void>(draws.Next()); // The drift.
        static_cast<void>(draws.Next());     // The flap.
        static_cast<void>(draws.Next());     // The jump.
        const double level = ((draws.Next() & 31) + 64) * static_cast<double>(0.01f);
        const bool spark = (draws.Next() & 1) == 0;
        glm::vec3 offset{};
        if (spark)
        {
            for (int axis = 0; axis < 3; ++axis)
                offset[axis] = static_cast<float>((draws.Next() & 15) - 8);
        }
        const auto stepped = Client::Effects::StepGameModelEffect(butterfly.effect, nullptr, butterfly.particles, 0.0f,
                                                                  random, nullptr, butterfly.services);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const auto& sprite = butterfly.sprites.Sprites()[0];
        EXPECT_EQ(sprite.light, glm::vec3(static_cast<float>(level * static_cast<double>(0.7f)),
                                          static_cast<float>(level * static_cast<double>(0.9f)),
                                          static_cast<float>(level * static_cast<double>(0.5f))));
        const auto& particle = butterfly.particles.All()[0];
        EXPECT_EQ(particle.alive, spark) << seed;
        if (!spark)
            continue;
        ++sparks;
        EXPECT_EQ(particle.type, Client::Particles::FairySparkParticle);
        EXPECT_EQ(particle.subtype, 7);
        EXPECT_EQ(particle.light, glm::vec3(0.5f));
        EXPECT_EQ(particle.position.x, 100.0f + offset.x);
        EXPECT_EQ(particle.position.y, 200.0f + offset.y);
        EXPECT_EQ(particle.position.z, sprite.position.z + offset.z);
    }
    EXPECT_GT(sparks, 0);
}

// Without the terrain height it stops at 0x520dad; without the sprite table, at 0x520fd1.
TEST(GameModelEffectsTest, TheButterflyNeedsTheTerrainHeightAndTheSpriteTable)
{
    Butterfly heightless(0);
    heightless.services.terrainHeight = {};
    Client::Crt::CrtRandom random(5);
    auto cut = Client::Effects::StepGameModelEffect(heightless.effect, nullptr, heightless.particles, 0.0f, random,
                                                    nullptr, heightless.services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x520dad"), std::string::npos);

    Butterfly spriteless(0);
    spriteless.services.sprites = nullptr;
    cut = Client::Effects::StepGameModelEffect(spriteless.effect, nullptr, spriteless.particles, 0.0f, random, nullptr,
                                               spriteless.services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x520fd1"), std::string::npos);
}

// Dungeon and Lost Tower effects, ownerless and with the common animation off (no skeleton).
Client::Effects::ModelEffect DungeonEffect(std::int16_t type, std::int32_t life)
{
    Client::Effects::ModelEffect effect;
    effect.alive = true;
    effect.type = type;
    effect.life = life;
    effect.transform.scale = 1.0f;
    effect.transform.position = {1000.0f, 1000.0f, 100.0f};
    effect.light = {0.25f, 0.5f, 0.75f};
    effect.animation.action = 0xff;
    return effect;
}

// The first seed whose draw number `draw` (from 0) meets the condition.
std::uint32_t SeedWhereDraw(int draw, bool (*condition)(int))
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        Client::Crt::CrtRandom random(seed);
        for (int skipped = 0; skipped < draw; ++skipped)
            static_cast<void>(random.Next());
        if (condition(random.Next()))
            return seed;
    }
}

// 0x52444d..0x5244ae: z adds the fall (+0xe4), which loses 1; with the ground above, it rests, the fall bounces to
// −0.4f, it loses 4 life and 2 in its speed's y (which 0x51fc80 adds rotated in the same step). A single rand(), the
// prefix's. previousPosition keeps the position before the whole step.
TEST(GameModelEffectsTest, TheFallingStoneFallsAndBouncesOnTheGround)
{
    Client::Effects::ModelEffect falling = DungeonEffect(Client::Effects::FallingStoneEffect, 30);
    falling.turnDirection = -2.0f;
    Client::Particles::Particles particles;
    Client::Effects::ModelEffectStepServices services;
    services.terrainHeight = [](float, float) { return 50.0; };
    Client::Crt::CrtRandom random(1);
    ASSERT_TRUE(
        Client::Effects::StepGameModelEffect(falling, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
    EXPECT_EQ(falling.transform.position, glm::vec3(1000.0f, 1000.0f, 98.0f));
    EXPECT_EQ(falling.turnDirection, -3.0f);
    EXPECT_EQ(falling.life, 29);
    EXPECT_EQ(falling.velocity, glm::vec3(0.0f));
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());

    Client::Effects::ModelEffect landing = DungeonEffect(Client::Effects::FallingStoneEffect, 30);
    landing.turnDirection = -2.0f;
    services.terrainHeight = [](float, float) { return 120.25; };
    ASSERT_TRUE(
        Client::Effects::StepGameModelEffect(landing, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
    EXPECT_EQ(landing.transform.position, glm::vec3(1000.0f, 998.0f, 120.25f));
    EXPECT_EQ(landing.turnDirection, static_cast<float>(-3.0 * static_cast<double>(std::bit_cast<float>(0xbecccccdu))));
    EXPECT_EQ(landing.life, 25);
    EXPECT_EQ(landing.velocity, glm::vec3(0.0f, -2.0f, 0.0f));
    EXPECT_EQ(landing.previousPosition, glm::vec3(1000.0f, 1000.0f, 100.0f));

    services.terrainHeight = {};
    Client::Effects::ModelEffect blind = DungeonEffect(Client::Effects::FallingStoneEffect, 30);
    const auto cut = Client::Effects::StepGameModelEffect(blind, nullptr, particles, 0.0f, random, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x524473"), std::string::npos) << cut.Error();
}

// Stone 0x10b of the attack on Blood Castle's statues. The creation (0x51bafb): nine rand() with the 0x10b, which goes
// to subtype 1, and eight with the 0x10a; z 50 more, no owner. The step (0x52531d): the unrotated speed is added and
// multiplied by 0.9f (by 1.1f more with subtype 1), the fall adds and loses 3; in the air it tilts scale · 16 and on
// the ground it rests, the fall times −0.2f, loses 5 life and tilts scale · 128; +0x174 = life · 0.1f. Then, rand() %
// 10: with 0 it stops at 0x5252e6 (the 0x5f3/0 dust goes in an unwritten local, Q-C1176); otherwise, the common tail
// takes the life.
TEST(GameModelEffectsTest, TheStatueStonesJumpFallTiltAndCutAtTheDust)
{
    {
        Client::Effects::ModelEffects effects;
        Client::Crt::CrtRandom random(7);
        const auto slot = effects.CreateDebris(Client::Effects::DebrisEffect, {1000.0f, 1000.0f, 100.0f},
                                               glm::vec3(0.0f), glm::vec3(1.0f), random);
        ASSERT_TRUE(slot.has_value());
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, 9).State());
        const Client::Effects::ModelEffect& stone = effects.Effects()[*slot];
        EXPECT_EQ(stone.subtype, 1);
        EXPECT_EQ(stone.transform.position, glm::vec3(1000.0f, 1000.0f, 150.0f));
        EXPECT_GE(stone.life, 32);
        EXPECT_LE(stone.life, 47);
        EXPECT_GE(stone.turnDirection, 2.0f);
        EXPECT_LE(stone.turnDirection, 10.0f);
        EXPECT_FALSE(stone.hasOwner);
        EXPECT_EQ(stone.disturbanceState, 0xffff);
        Client::Crt::CrtRandom other(7);
        const auto first = effects.CreateDebris(Client::Effects::FirstDebrisEffect, glm::vec3(0.0f), glm::vec3(0.0f),
                                                glm::vec3(1.0f), other);
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(other.State(), Client::Tests::AfterDraws(7, 8).State());
        EXPECT_EQ(effects.Effects()[*first].subtype, 0);
    }
    const auto stone = []
    {
        Client::Effects::ModelEffect effect = DungeonEffect(Client::Effects::DebrisEffect, 40);
        effect.subtype = 1;
        effect.velocity = {0.0f, 10.0f, 0.0f};
        effect.turnDirection = 5.0f;
        effect.transform.position = {1000.0f, 1000.0f, 500.0f};
        return effect;
    };
    const auto times = [](double a, float b) { return static_cast<float>(a * static_cast<double>(b)); };
    Client::Particles::Particles particles;
    Client::Effects::ModelEffectStepServices services;
    const std::uint32_t quiet = SeedWhereDraw(1, [](int draw) { return draw % 10 != 0; });
    {
        Client::Effects::ModelEffect flying = stone();
        services.terrainHeight = [](float, float) { return 0.0; };
        Client::Crt::CrtRandom random(quiet);
        ASSERT_TRUE(Client::Effects::StepGameModelEffect(flying, nullptr, particles, 0.0f, random, nullptr, services)
                        .HasValue());
        EXPECT_EQ(flying.velocity.y,
                  times(times(10.0, std::bit_cast<float>(0x3f666666u)), std::bit_cast<float>(0x3f8ccccdu)));
        EXPECT_EQ(flying.turnDirection, 2.0f);
        EXPECT_EQ(flying.transform.angles.x, -16.0f);
        EXPECT_EQ(flying.alpha, times(40.0, std::bit_cast<float>(0x3dcccccdu)));
        EXPECT_EQ(flying.life, 39);
        EXPECT_EQ(flying.previousPosition, glm::vec3(1000.0f, 1000.0f, 500.0f));
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(quiet, 2).State());
        EXPECT_FALSE(particles.Any());
    }
    {
        Client::Effects::ModelEffect landing = stone();
        services.terrainHeight = [](float, float) { return 1000.0; };
        Client::Crt::CrtRandom random(quiet);
        ASSERT_TRUE(Client::Effects::StepGameModelEffect(landing, nullptr, particles, 0.0f, random, nullptr, services)
                        .HasValue());
        EXPECT_EQ(landing.turnDirection, times(2.0, std::bit_cast<float>(0xbe4ccccdu)));
        EXPECT_EQ(landing.transform.angles.x, -128.0f);
        EXPECT_EQ(landing.alpha, times(35.0, std::bit_cast<float>(0x3dcccccdu)));
        EXPECT_EQ(landing.life, 34);
    }
    {
        // The step where rand() % 10 gives 0: it stops at 0x5252e6 after its two rand(), without the dust or the tail.
        const std::uint32_t dusty = SeedWhereDraw(1, [](int draw) { return draw % 10 == 0; });
        Client::Effects::ModelEffect raising = stone();
        services.terrainHeight = [](float, float) { return 0.0; };
        Client::Crt::CrtRandom random(dusty);
        const auto cut =
            Client::Effects::StepGameModelEffect(raising, nullptr, particles, 0.0f, random, nullptr, services);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x5252e6"), std::string::npos) << cut.Error();
        EXPECT_NE(cut.Error().find("P-C232"), std::string::npos) << cut.Error();
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(dusty, 2).State());
        EXPECT_EQ(raising.transform.angles.x, -16.0f);
        EXPECT_EQ(raising.alpha, times(40.0, std::bit_cast<float>(0x3dcccccdu)));
        EXPECT_EQ(raising.life, 40);
        EXPECT_FALSE(particles.Any());
    }
    services.terrainHeight = {};
    Client::Effects::ModelEffect blind = stone();
    Client::Crt::CrtRandom random(quiet);
    const auto cut = Client::Effects::StepGameModelEffect(blind, nullptr, particles, 0.0f, random, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5253d9"), std::string::npos) << cut.Error();
}

// 0x5239c8..0x523b3a with subtype 0: the unrotated speed is added and multiplied by 0.9f; the fall adds and loses 3; in
// the air it tilts scale · 32 and on the ground it rests, bounces to −0.5, loses 4 life and tilts scale · 128. With
// rand() % 10 at 0, a 0x5d9 fire of subtype rand() % 3 + 1 with its light.
TEST(GameModelEffectsTest, TheStoneChipsDragTiltBounceAndSometimesBurn)
{
    const std::uint32_t burns = SeedWhereDraw(1, [](int value) { return value % 10 == 0; });
    const std::uint32_t cold = SeedWhereDraw(1, [](int value) { return value % 10 != 0; });
    {
        Client::Effects::ModelEffect chip = DungeonEffect(Client::Effects::FirstStoneChipEffect, 40);
        chip.velocity = {10.0f, 0.0f, 0.0f};
        chip.turnDirection = 5.0f;
        Client::Particles::Particles particles;
        Client::Effects::ModelEffectStepServices services;
        services.terrainHeight = [](float, float) { return 0.0; };
        Client::Crt::CrtRandom random(cold);
        ASSERT_TRUE(
            Client::Effects::StepGameModelEffect(chip, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
        EXPECT_EQ(chip.velocity.x, static_cast<float>(10.0 * static_cast<double>(std::bit_cast<float>(0x3f666666u))));
        EXPECT_EQ(chip.turnDirection, 2.0f);
        EXPECT_EQ(chip.transform.angles.x, -32.0f);
        EXPECT_EQ(chip.transform.position.z, 105.0f);
        EXPECT_EQ(chip.life, 39);
        EXPECT_FALSE(particles.All()[0].alive);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(cold, 2).State());
    }
    {
        Client::Effects::ModelEffect chip = DungeonEffect(Client::Effects::SecondStoneChipEffect, 40);
        chip.turnDirection = 5.0f;
        Client::Particles::Particles particles;
        Client::Effects::ModelEffectStepServices services;
        services.terrainHeight = [](float, float) { return 200.0; };
        Client::Crt::CrtRandom random(burns);
        ASSERT_TRUE(
            Client::Effects::StepGameModelEffect(chip, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
        EXPECT_EQ(chip.transform.position.z, 200.0f);
        EXPECT_EQ(chip.turnDirection, -1.0f);
        EXPECT_EQ(chip.transform.angles.x, -128.0f);
        EXPECT_EQ(chip.life, 35);
        const auto& fire = particles.All()[0];
        ASSERT_TRUE(fire.alive);
        EXPECT_EQ(fire.type, 0x5d9);
        Client::Crt::CrtRandom draws = Client::Tests::AfterDraws(burns, 2);
        EXPECT_EQ(fire.subtype, draws.Next() % 3 + 1);
    }
}

// 0x523980, 0x523a3a and 0x523ad2: the column's small stone 0xc8/0xc9 (subtype 0xc) falls like the /0 (the unrotated
// speed, the drag, the fall, the bounce and the tilt) and, with rand() % 10 at 0, releases its 0x5d9 fire with subtype
// rand() % 3 + 1 and its light.
TEST(GameModelEffectsTest, TheColumnChipFallsAndBurnsLikeTheFirstOne)
{
    const std::uint32_t burns = SeedWhereDraw(1, [](int value) { return value % 10 == 0; });
    const std::uint32_t cold = SeedWhereDraw(1, [](int value) { return value % 10 != 0; });
    for (const std::int16_t type : {Client::Effects::FirstStoneChipEffect, Client::Effects::SecondStoneChipEffect})
    {
        SCOPED_TRACE(type);
        for (const std::uint32_t seed : {cold, burns})
        {
            SCOPED_TRACE(seed);
            // The /0xc and its twin /0, with the same seed, the same push and the same terrain.
            std::array<Client::Effects::ModelEffect, 2> chips{DungeonEffect(type, 40), DungeonEffect(type, 40)};
            std::array<std::unique_ptr<Client::Particles::Particles>, 2> particles{
                std::make_unique<Client::Particles::Particles>(), std::make_unique<Client::Particles::Particles>()};
            std::array<Client::Crt::CrtRandom, 2> randoms{Client::Crt::CrtRandom(seed), Client::Crt::CrtRandom(seed)};
            chips[0].subtype = Client::Effects::ColumnStoneChipSubtype;
            Client::Effects::ModelEffectStepServices services;
            services.terrainHeight = [](float, float) { return 0.0; };
            for (std::size_t twin = 0; twin < chips.size(); ++twin)
            {
                chips[twin].velocity = {10.0f, 0.0f, 0.0f};
                chips[twin].turnDirection = 5.0f;
                const auto stepped = Client::Effects::StepGameModelEffect(chips[twin], nullptr, *particles[twin], 0.0f,
                                                                          randoms[twin], nullptr, services);
                ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            }
            const auto& column = chips[0];
            EXPECT_EQ(column.velocity.x,
                      static_cast<float>(10.0 * static_cast<double>(std::bit_cast<float>(0x3f666666u))));
            EXPECT_EQ(column.turnDirection, 2.0f);
            EXPECT_EQ(column.transform.angles.x, -32.0f);
            EXPECT_EQ(column.life, 39);
            EXPECT_EQ(column.transform.position, chips[1].transform.position);
            EXPECT_EQ(column.velocity, chips[1].velocity);
            EXPECT_EQ(randoms[0].State(), randoms[1].State());
            const auto& fire = particles[0]->All()[0];
            const auto& twinFire = particles[1]->All()[0];
            EXPECT_EQ(fire.alive, seed == burns);
            EXPECT_EQ(twinFire.alive, seed == burns);
            if (seed == burns)
            {
                Client::Crt::CrtRandom draws = Client::Tests::AfterDraws(seed, 2);
                EXPECT_EQ(fire.type, 0x5d9);
                EXPECT_EQ(fire.subtype, draws.Next() % 3 + 1);
                EXPECT_EQ(fire.light, column.light);
                EXPECT_EQ(fire.position, twinFire.position);
            }
        }
    }
}

// Pieces 0xe5 and 0xe6 of the 0x16b's death (subtype 0) enter through 0x523926 → 0x523974 and take the small stone's
// step; with rand() % 10 at 0 they release nothing (0x523b10): the prefix's and the % 10.
TEST(GameModelEffectsTest, TheSplintersStepLikeChipsAndNeverBurn)
{
    const std::uint32_t burns = SeedWhereDraw(1, [](int value) { return value % 10 == 0; });
    for (const std::int16_t type : {Client::Effects::FirstSplinterEffect, Client::Effects::SecondSplinterEffect})
    {
        SCOPED_TRACE(type);
        Client::Effects::ModelEffect splinter = DungeonEffect(type, 40);
        splinter.velocity = {10.0f, 0.0f, 0.0f};
        splinter.turnDirection = 5.0f;
        auto particles = std::make_unique<Client::Particles::Particles>();
        Client::Effects::ModelEffectStepServices services;
        services.terrainHeight = [](float, float) { return 0.0; };
        Client::Crt::CrtRandom random(burns);
        const auto stepped =
            Client::Effects::StepGameModelEffect(splinter, nullptr, *particles, 0.0f, random, nullptr, services);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(splinter.velocity.x,
                  static_cast<float>(10.0 * static_cast<double>(std::bit_cast<float>(0x3f666666u))));
        EXPECT_EQ(splinter.turnDirection, 2.0f);
        EXPECT_EQ(splinter.transform.angles.x, -32.0f);
        EXPECT_EQ(splinter.transform.position.z, 105.0f);
        EXPECT_EQ(splinter.life, 39);
        EXPECT_FALSE(particles->Any());
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(burns, 2).State());
    }
}

TEST(GameModelEffectsTest, TheRisingStoneChipAcceleratesWithoutTerrainOrFire)
{
    auto particles = std::make_unique<Client::Particles::Particles>();
    auto chip = DungeonEffect(Client::Effects::FirstStoneChipEffect, 2);
    chip.subtype = 1;
    chip.transform.angles = {0, 0, 90};
    chip.velocity = {0, 10, 0};
    chip.turnDirection = 8;
    const auto start = chip.transform.position;
    Client::Crt::CrtRandom random(1);

    auto result = Client::Effects::StepGameModelEffect(chip, nullptr, *particles, 0.0f, random);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(chip.previousPosition, start);
    EXPECT_NEAR(chip.transform.position.x, start.x - 19, 0.001f);
    EXPECT_NEAR(chip.transform.position.y, start.y, 0.001f);
    EXPECT_EQ(chip.transform.position.z, start.z + 8);
    EXPECT_EQ(chip.turnDirection, 8.5f);
    EXPECT_EQ(chip.transform.angles, (glm::vec3{0, 0, 90}));
    EXPECT_EQ(chip.life, 1);
    EXPECT_TRUE(chip.alive);

    result = Client::Effects::StepGameModelEffect(chip, nullptr, *particles, 0.0f, random);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(chip.transform.position.z, start.z + 16.5f);
    EXPECT_EQ(chip.life, 0);
    EXPECT_FALSE(chip.alive);
    EXPECT_FALSE(particles->All()[0].alive);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 2).State());
}

// 0x523985..0x523a62 with the /2 (the storms'): it moves and rises like the /1 with the previous angles and, after the
// move, turns 20 in z (0x523a44, 0x6b86d0). No terrain or fire: only each step's light rand().
TEST(GameModelEffectsTest, TheSpinningStoneChipRisesAndTurnsTwentyEachStep)
{
    auto particles = std::make_unique<Client::Particles::Particles>();
    auto chip = DungeonEffect(Client::Effects::SecondStoneChipEffect, 3);
    chip.subtype = Client::Effects::SpinningStoneChipSubtype;
    chip.transform.angles = {0, 0, 90};
    chip.velocity = {0, 10, 0};
    chip.turnDirection = 8;
    Client::Crt::CrtRandom random(1);
    // The twin: the move with the previous angles, the drag, the fall, the turn of 20 and the common tail
    // (StepModelEffect, 0x52cb98), written separately.
    auto twin = chip;
    for (int step = 0; step < 2; ++step)
    {
        SCOPED_TRACE(step);
        const auto moved =
            Client::Math::OriginalVectorRotate(twin.velocity, Client::Math::OriginalAngleMatrix(twin.transform.angles));
        for (int axis = 0; axis < 3; ++axis)
            twin.transform.position[axis] = static_cast<float>(static_cast<double>(moved[axis]) +
                                                               static_cast<double>(twin.transform.position[axis]));
        for (int axis = 0; axis < 3; ++axis)
            twin.velocity[axis] = static_cast<float>(static_cast<double>(twin.velocity[axis]) *
                                                     static_cast<double>(std::bit_cast<float>(0x3f666666u)));
        twin.transform.position.z = static_cast<float>(static_cast<double>(twin.transform.position.z) +
                                                       static_cast<double>(twin.turnDirection));
        twin.transform.angles.z = static_cast<float>(static_cast<double>(twin.transform.angles.z) + 20.0);
        twin.turnDirection = static_cast<float>(static_cast<double>(twin.turnDirection) + 0.5);
        Client::Effects::StepModelEffect(twin, nullptr);

        const auto result = Client::Effects::StepGameModelEffect(chip, nullptr, *particles, 0.0f, random);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(chip.transform.position, twin.transform.position);
        EXPECT_EQ(chip.velocity, twin.velocity);
        EXPECT_EQ(chip.transform.angles, (glm::vec3{0, 0, step == 0 ? 110.0f : 130.0f}));
        EXPECT_EQ(chip.turnDirection, step == 0 ? 8.5f : 9.0f);
        EXPECT_EQ(chip.life, twin.life);
    }
    EXPECT_FALSE(particles->All()[0].alive);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 2).State());
}

// 0x52e810's ice piece 0xca enters 0x523974 with subtype 0: the same step as the small stone (drags, falls, bounces
// and tilts) and, with rand() % 10 at 0, the 0x5f2/0 breath with its light and scale 1 instead of the fire, without its
// subtype's rand() (0x523af4 and 0x523b47): the prefix, the % 10 and the two of 0x54184b's creation. Another subtype
// stops at 0x523974.
TEST(GameModelEffectsTest, TheIceShardStepsLikeAChipAndSometimesBreathes)
{
    const std::uint32_t breathes = SeedWhereDraw(1, [](int value) { return value % 10 == 0; });
    const std::uint32_t cold = SeedWhereDraw(1, [](int value) { return value % 10 != 0; });
    Client::Effects::ModelEffectStepServices services;
    services.terrainHeight = [](float, float) { return 200.0; };
    {
        Client::Effects::ModelEffect shard = DungeonEffect(Client::Effects::IceShardEffect, 40);
        shard.velocity = {10.0f, 0.0f, 0.0f};
        shard.turnDirection = 5.0f;
        auto particles = std::make_unique<Client::Particles::Particles>();
        Client::Crt::CrtRandom random(breathes);
        ASSERT_TRUE(Client::Effects::StepGameModelEffect(shard, nullptr, *particles, 0.0f, random, nullptr, services)
                        .HasValue());
        EXPECT_EQ(shard.velocity.x, static_cast<float>(10.0 * static_cast<double>(std::bit_cast<float>(0x3f666666u))));
        EXPECT_EQ(shard.turnDirection, -1.0f);
        EXPECT_EQ(shard.transform.angles.x, -128.0f);
        EXPECT_EQ(shard.life, 35);
        const auto& breath = particles->All()[0];
        ASSERT_TRUE(breath.alive);
        EXPECT_EQ(breath.type, 0x5f2);
        EXPECT_EQ(breath.subtype, 0);
        // Where it was before 0x51fc80, which then adds the rotated speed.
        EXPECT_EQ(breath.position, glm::vec3(1010.0f, 1000.0f, 200.0f));
        EXPECT_FALSE(particles->All()[1].alive);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(breathes, 2 + 2).State());
    }
    {
        Client::Effects::ModelEffect shard = DungeonEffect(Client::Effects::IceShardEffect, 40);
        auto particles = std::make_unique<Client::Particles::Particles>();
        Client::Crt::CrtRandom random(cold);
        ASSERT_TRUE(Client::Effects::StepGameModelEffect(shard, nullptr, *particles, 0.0f, random, nullptr, services)
                        .HasValue());
        EXPECT_FALSE(particles->Any());
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(cold, 2).State());
    }
    Client::Effects::ModelEffect owned = DungeonEffect(Client::Effects::IceShardEffect, 40);
    owned.subtype = 5;
    auto particles = std::make_unique<Client::Particles::Particles>();
    Client::Crt::CrtRandom random(1);
    const auto cut = Client::Effects::StepGameModelEffect(owned, nullptr, *particles, 0.0f, random, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x523974"), std::string::npos) << cut.Error();
}

// 0x52ad21..0x52ae25 with subtype 0: six 0x5de/0 flames at ±25 in x and y (their two draws before each creation), with
// rand() & 7 at 0 the small stone 0xc8 + (rand() & 1), and the dynamic light (L; L · 0.4f; 0) with radius 3 and L =
// ((first rand() & 3) + 7) · 0.1f. The same with the same calls in another order would give other numbers: the
// expected one repeats the ASM's sequence with another CrtRandom.
TEST(GameModelEffectsTest, TheFlameJetSpitsSixFlamesASometimesChipAndItsLight)
{
    for (const bool chipped : {true, false})
    {
        SCOPED_TRACE(chipped);
        // No unary + on the lambdas: in MSVC, with several calling conventions, it is ambiguous.
        const auto chips = [](int value) { return (value & 7) == 0; };
        const auto stays = [](int value) { return (value & 7) != 0; };
        const std::uint32_t seed = chipped ? SeedWhereDraw(25, chips) : SeedWhereDraw(25, stays);
        Client::Effects::ModelEffect jet = DungeonEffect(Client::Effects::FlameJetEffect, 30);
        Client::Particles::Particles particles;
        Client::Effects::ModelEffects effects;
        auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
        Client::Effects::ModelEffectStepServices services;
        services.effects = &effects;
        services.lighting = lighting.get();
        Client::Crt::CrtRandom random(seed);
        const auto stepped =
            Client::Effects::StepGameModelEffect(jet, nullptr, particles, 0.0f, random, nullptr, services);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();

        Client::Crt::CrtRandom draws(seed);
        const auto luminosity =
            static_cast<float>(((draws.Next() & 3) + 7) * static_cast<double>(std::bit_cast<float>(0x3dcccccdu)));
        Client::Particles::Particles expectedParticles;
        for (std::size_t flame = 0; flame < 6; ++flame)
        {
            const auto dx = static_cast<float>(draws.Next() % 50 - 25);
            const auto dy = static_cast<float>(draws.Next() % 50 - 25);
            const glm::vec3 point{static_cast<float>(static_cast<double>(dx) + 1000.0),
                                  static_cast<float>(static_cast<double>(dy) + 1000.0), 100.0f};
            ASSERT_TRUE(expectedParticles
                            .CreateChecked(Client::Particles::FlameParticle, point, glm::vec3(0.0f), glm::vec3(1.0f), 0,
                                           1.0f, 0.0f, draws)
                            .HasValue());
            EXPECT_EQ(particles.All()[flame].position, point) << flame;
            EXPECT_EQ(particles.All()[flame].subtype, 0);
            EXPECT_EQ(particles.All()[flame].light, glm::vec3(1.0f));
            EXPECT_EQ(particles.All()[flame].scale, expectedParticles.All()[flame].scale);
        }
        EXPECT_FALSE(particles.All()[6].alive);
        Client::Effects::ModelEffects expectedEffects;
        if ((draws.Next() & 7) == 0)
        {
            const auto type = static_cast<std::int16_t>(0xc8 + (draws.Next() & 1));
            ASSERT_TRUE(
                expectedEffects.CreateStoneChip(type, {1000.0f, 1000.0f, 100.0f}, glm::vec3(0.0f), jet.light, draws)
                    .has_value());
        }
        EXPECT_EQ(effects.Effects()[0].alive, chipped);
        EXPECT_EQ(effects.Effects()[0].type, expectedEffects.Effects()[0].type);
        EXPECT_EQ(effects.Effects()[0].velocity, expectedEffects.Effects()[0].velocity);
        EXPECT_EQ(random.State(), draws.State());
        auto expectedLighting = std::make_unique<Client::Terrain::TerrainLighting>();
        ASSERT_TRUE(Client::Terrain::AddDynamicLight(
                        expectedLighting.get(), {1000.0f, 1000.0f, 100.0f},
                        {luminosity,
                         static_cast<float>(static_cast<double>(luminosity) *
                                            static_cast<double>(std::bit_cast<float>(0x3ecccccdu))),
                         0.0f},
                        3, "test")
                        .HasValue());
        EXPECT_EQ(lighting->cells, expectedLighting->cells);
        EXPECT_EQ(jet.life, 29);
    }
}

// 0x52ae9a..0x52b019, the 0x1a4's 0x5de/3: three times the 0x5de/6 flame and the 0x6ec/0 ember (±5, 40 higher, 2.8f);
// the 0x5f2/0x15 smoke (±5, 40 lower, 0.8f), in white; the rand() % 2 of the small stone 0xc8/0xc9 with subtype 0xc
// (0x52afe1), at its position, with its angles and light, which with the table full does nothing and does not draw its
// five rand(); and the light (L; L · 0.4f; 0) with radius 3.
TEST(GameModelEffectsTest, TheFlameColumnSpitsFlamesEmbersSmokeAndItsChip)
{
    for (const bool room : {false, true})
    {
        SCOPED_TRACE(room);
        constexpr std::uint32_t Seed = 0x1a4u;
        Client::Effects::ModelEffect column = DungeonEffect(Client::Effects::FlameJetEffect, 15);
        column.subtype = Client::Effects::FlameColumnSubtype;
        column.transform.angles = {0.0f, 0.0f, 30.0f};
        auto particles = std::make_unique<Client::Particles::Particles>();
        auto effects = std::make_unique<Client::Effects::ModelEffects>();
        if (!room)
            for (auto& effect : effects->Effects())
                effect.alive = true;
        auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
        Client::Effects::ModelEffectStepServices services;
        services.effects = effects.get();
        services.lighting = lighting.get();
        Client::Crt::CrtRandom random(Seed);
        const auto stepped =
            Client::Effects::StepGameModelEffect(column, nullptr, *particles, 0.0f, random, nullptr, services);

        // The replay: the local light's rand(), the same particles in a twin pool and the small stone's.
        Client::Crt::CrtRandom draws(Seed);
        const auto luminosity =
            static_cast<float>(((draws.Next() & 3) + 7) * static_cast<double>(std::bit_cast<float>(0x3dcccccdu)));
        auto twin = std::make_unique<Client::Particles::Particles>();
        const glm::vec3 angles{0.0f, 0.0f, 30.0f};
        const auto aside = [&draws](double height)
        {
            const int dx = draws.Next() % 10 - 5;
            const int dy = draws.Next() % 10 - 5;
            return glm::vec3{static_cast<float>(1000 + dx), static_cast<float>(1000 + dy),
                             static_cast<float>(100.0 + height)};
        };
        for (int flame = 0; flame < 3; ++flame)
        {
            ASSERT_TRUE(
                twin->CreateChecked(0x5de, {1000.0f, 1000.0f, 100.0f}, angles, glm::vec3(1.0f), 6, 1.0f, 0.0f, draws)
                    .HasValue());
            const glm::vec3 ember = aside(40.0);
            ASSERT_TRUE(twin->CreateChecked(0x6ec, ember, angles, glm::vec3(1.0f), 0, std::bit_cast<float>(0x40333333u),
                                            0.0f, draws)
                            .HasValue());
        }
        const glm::vec3 smoke = aside(-40.0);
        ASSERT_TRUE(twin->CreateChecked(0x5f2, smoke, angles, glm::vec3(1.0f), 0x15, std::bit_cast<float>(0x3f4ccccdu),
                                        0.0f, draws)
                        .HasValue());
        const auto chip = static_cast<std::int16_t>(0xc8 + draws.Next() % 2);
        auto twinEffects = std::make_unique<Client::Effects::ModelEffects>();
        if (room)
        {
            ASSERT_TRUE(twinEffects
                            ->CreateStoneChip(chip, {1000.0f, 1000.0f, 100.0f}, angles, {0.25f, 0.5f, 0.75f}, draws,
                                              Client::Effects::ColumnStoneChipSubtype)
                            .has_value());
            const auto& made = effects->Effects()[0];
            const auto& expected = twinEffects->Effects()[0];
            ASSERT_TRUE(made.alive);
            EXPECT_EQ(made.type, chip);
            EXPECT_EQ(made.subtype, 0xc);
            EXPECT_EQ(made.transform.position, expected.transform.position);
            EXPECT_EQ(made.transform.angles, expected.transform.angles);
            EXPECT_EQ(made.transform.scale, expected.transform.scale);
            EXPECT_EQ(made.light, expected.light);
            EXPECT_EQ(made.life, expected.life);
            EXPECT_EQ(made.velocity, expected.velocity);
            EXPECT_EQ(made.turnDirection, expected.turnDirection);
            EXPECT_EQ(made.disturbanceState, 0xffff);
            EXPECT_FALSE(effects->Effects()[1].alive);
        }
        for (std::size_t slot = 0; slot < 7; ++slot)
        {
            SCOPED_TRACE(slot);
            const auto& made = particles->All()[slot];
            const auto& expected = twin->All()[slot];
            ASSERT_TRUE(made.alive);
            EXPECT_EQ(made.type, expected.type);
            EXPECT_EQ(made.subtype, expected.subtype);
            EXPECT_EQ(made.position, expected.position);
            EXPECT_EQ(made.angles, expected.angles);
            EXPECT_EQ(made.light, expected.light);
            EXPECT_EQ(made.scale, expected.scale);
        }
        EXPECT_FALSE(particles->All()[7].alive);
        EXPECT_EQ(random.State(), draws.State());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        auto expectedLighting = std::make_unique<Client::Terrain::TerrainLighting>();
        ASSERT_TRUE(Client::Terrain::AddDynamicLight(
                        expectedLighting.get(), {1000.0f, 1000.0f, 100.0f},
                        {luminosity,
                         static_cast<float>(static_cast<double>(luminosity) *
                                            static_cast<double>(std::bit_cast<float>(0x3ecccccdu))),
                         0.0f},
                        3, "test")
                        .HasValue());
        EXPECT_EQ(lighting->cells, expectedLighting->cells);
        EXPECT_EQ(column.life, 14);
    }
}

// 0x52ae2a: after the light, with the hero ([0x743a170]) as owner (+0x108) and the life before the tail a multiple of
// 20, the Flame's jet asks 0x517a20 at 150 over itself; with another owner, no owner or another life, it does not. The
// step's rand() are those of the ownerless jet (0x517a20's are drawn by whoever asks). Without `skillHits`, it stops.
TEST(GameModelEffectsTest, TheHerosFlameJetHitsEveryTwentySteps)
{
    int hero = 0;
    int other = 0;
    const std::uint32_t quiet = SeedWhereDraw(25, [](int value) { return (value & 7) != 0; });
    struct Case
    {
        void* owner;
        std::int32_t life;
        bool hits;
    };
    for (const Case probe : {Case{&hero, 40, true}, Case{&hero, 20, true}, Case{&hero, 39, false},
                             Case{&hero, 21, false}, Case{&other, 40, false}, Case{nullptr, 40, false}})
    {
        SCOPED_TRACE(testing::Message() << "vida " << probe.life << ", héroe " << (probe.owner == &hero));
        Client::Effects::ModelEffect jet = DungeonEffect(Client::Effects::FlameJetEffect, probe.life);
        jet.ownerContext = probe.owner;
        jet.hasOwner = probe.owner != nullptr;
        Client::Effects::ModelEffect plain = DungeonEffect(Client::Effects::FlameJetEffect, probe.life);
        Client::Particles::Particles particles;
        Client::Particles::Particles plainParticles;
        Client::Effects::ModelEffects effects;
        auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
        Client::Effects::ModelEffectStepServices services;
        services.effects = &effects;
        services.lighting = lighting.get();
        services.hero = &hero;
        std::vector<float> radii;
        const Client::Effects::ModelEffect* asked = nullptr;
        services.skillHits = [&](const Client::Effects::ModelEffect& flame,
                                 float radius) -> Client::Core::Expected<void, std::string>
        {
            asked = &flame;
            radii.push_back(radius);
            return {};
        };
        Client::Crt::CrtRandom random(quiet);
        Client::Crt::CrtRandom plainRandom(quiet);

        ASSERT_TRUE(
            Client::Effects::StepGameModelEffect(jet, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
        ASSERT_TRUE(
            Client::Effects::StepGameModelEffect(plain, nullptr, plainParticles, 0.0f, plainRandom, nullptr, services)
                .HasValue());

        EXPECT_EQ(radii, probe.hits ? std::vector<float>{150.0f} : std::vector<float>{});
        EXPECT_EQ(asked, probe.hits ? &jet : nullptr);
        EXPECT_EQ(random.State(), plainRandom.State());
        EXPECT_EQ(jet.life, probe.life - 1);
    }
    Client::Effects::ModelEffect jet = DungeonEffect(Client::Effects::FlameJetEffect, 40);
    jet.ownerContext = &hero;
    jet.hasOwner = true;
    Client::Particles::Particles particles;
    Client::Effects::ModelEffects effects;
    auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    Client::Effects::ModelEffectStepServices services;
    services.effects = &effects;
    services.lighting = lighting.get();
    services.hero = &hero;
    Client::Crt::CrtRandom random(quiet);
    const auto cut = Client::Effects::StepGameModelEffect(jet, nullptr, particles, 0.0f, random, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52ae72"), std::string::npos) << cut.Error();
}

// The first rand()'s local light dims with life under 5: L − (5 − life) · 0.2f, at 0 if below. Without the effect
// table it stops when the small stone comes out; without the dynamic light, at the end; another subtype, before.
TEST(GameModelEffectsTest, TheFlameJetFadesItsLightAndCutsWithoutItsTables)
{
    Client::Effects::ModelEffect fading = DungeonEffect(Client::Effects::FlameJetEffect, 1);
    Client::Particles::Particles particles;
    Client::Effects::ModelEffects effects;
    auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    Client::Effects::ModelEffectStepServices services;
    services.effects = &effects;
    services.lighting = lighting.get();
    Client::Crt::CrtRandom random(3);
    ASSERT_LT(Client::Tests::AfterDraws(3, 0).Next() & 3, 2);
    ASSERT_TRUE(
        Client::Effects::StepGameModelEffect(fading, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
    EXPECT_FALSE(fading.alive);
    EXPECT_EQ(lighting->cells, std::make_unique<Client::Terrain::TerrainLighting>()->cells)
        << "con la vida en 1, L − 0,8: el primer rand() de la semilla 3 deja L en 0,7 u 0,8 y la luz negra";

    const std::uint32_t chips = SeedWhereDraw(25, [](int value) { return (value & 7) == 0; });
    Client::Effects::ModelEffect jet = DungeonEffect(Client::Effects::FlameJetEffect, 30);
    services.effects = nullptr;
    Client::Crt::CrtRandom chipping(chips);
    auto cut = Client::Effects::StepGameModelEffect(jet, nullptr, particles, 0.0f, chipping, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52adf2"), std::string::npos) << cut.Error();

    const std::uint32_t quiet = SeedWhereDraw(25, [](int value) { return (value & 7) != 0; });
    Client::Effects::ModelEffect dark = DungeonEffect(Client::Effects::FlameJetEffect, 30);
    Client::Particles::Particles more;
    services.lighting = nullptr;
    Client::Crt::CrtRandom quietRandom(quiet);
    cut = Client::Effects::StepGameModelEffect(dark, nullptr, more, 0.0f, quietRandom, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52ae25"), std::string::npos) << cut.Error();

    // The /1 and the /2 belong to the CL11 switch batch (Effects/BlastModelEffects) and the /3 is the column
    // (StepFlameColumn); the /4 is still unported.
    Client::Effects::ModelEffect other = DungeonEffect(Client::Effects::FlameJetEffect, 30);
    other.subtype = 4;
    cut = Client::Effects::StepGameModelEffect(other, nullptr, more, 0.0f, quietRandom, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52ae7f"), std::string::npos) << cut.Error();
}
} // namespace

// 0x52a737..0x52a7af and 0x52cdb3..0x52cdfb: the 0x5ac adds its speed, lowers it 0.01f in z, releases a 0x5ac/1 with
// its light and scale, and the common tail adds it again; with the remaining life odd it takes another whole step.
// Cloud 0xbb only has the common tail.
TEST(GameModelEffectsTest, TheIcarusFlareStepsTwiceAndLeavesTwoTrails)
{
    Client::Effects::ModelEffect flare = DungeonEffect(Client::Effects::IcarusFlareEffect, 400);
    flare.velocity = {1.0f, 2.0f, 3.0f};
    flare.transform.scale = 3.0f;
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1);
    ASSERT_TRUE(Client::Effects::StepGameModelEffect(flare, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_EQ(flare.life, 398);
    const float sink = std::bit_cast<float>(0x3c23d70au);
    const auto vz1 = static_cast<float>(3.0 - static_cast<double>(sink));
    const auto vz2 = static_cast<float>(static_cast<double>(vz1) - static_cast<double>(sink));
    EXPECT_EQ(flare.velocity, glm::vec3(1.0f, 2.0f, vz2));
    float z = 100.0f;
    for (const float vz : {vz1, vz2})
    {
        z = static_cast<float>(static_cast<double>(vz == vz1 ? 3.0f : vz1) + static_cast<double>(z));
        z = static_cast<float>(static_cast<double>(z) + static_cast<double>(vz));
    }
    EXPECT_EQ(flare.transform.position, glm::vec3(1004.0f, 1008.0f, z));
    EXPECT_EQ(flare.previousPosition, glm::vec3(1000.0f, 1000.0f, 100.0f));
    int trails = 0;
    for (const auto& particle : particles.All())
    {
        if (!particle.alive)
            continue;
        ++trails;
        EXPECT_EQ(particle.type, 0x5ac);
        EXPECT_EQ(particle.subtype, 1);
        EXPECT_EQ(particle.life, 50);
        EXPECT_EQ(particle.scale, 3.0f);
        EXPECT_EQ(particle.light, glm::vec3(0.25f, 0.5f, 0.75f));
    }
    EXPECT_EQ(trails, 2);
    // 0x5208a0's common rand(), one per whole step.
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 2).State());

    Client::Effects::ModelEffect sinking = DungeonEffect(Client::Effects::IcarusFlareEffect, 400);
    sinking.velocity = {0.0f, 0.0f, -1.995f};
    ASSERT_TRUE(Client::Effects::StepGameModelEffect(sinking, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_FALSE(sinking.alive);

    Client::Effects::ModelEffect cloud = DungeonEffect(Client::Effects::IcarusCloudEffect, 2);
    ASSERT_TRUE(Client::Effects::StepGameModelEffect(cloud, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_EQ(cloud.life, 1);
    EXPECT_EQ(cloud.transform.position, glm::vec3(1000.0f, 1000.0f, 100.0f));
}

namespace
{
// The x87's sine · f (FSIN and FMUL), with f's sign.
double SineTimes(double argument, float multiplier)
{
    const auto product = Client::Math::Detail::SineProductPC53(argument, std::abs(multiplier));
    EXPECT_TRUE(product.has_value());
    return multiplier < 0.0f ? -product.value_or(0.0) : product.value_or(0.0);
}

Client::Effects::ModelEffect AidaWisp()
{
    Client::Effects::ModelEffect wisp = DungeonEffect(Client::Effects::AidaWispEffect, 1000);
    wisp.speed = 1.0f;
    wisp.turnDirection = 1.0f;
    wisp.flagEc = 1.0f;
    wisp.transform.scale = 0.95f;
    wisp.transform.angles = {10.0f, 0.0f, 30.0f};
    wisp.velocity = {0.0f, -4.5f, 0.0f};
    wisp.start = wisp.transform.position;
    return wisp;
}
} // namespace

// 0x520b77..0x520d0c: without drawn turns, the yaw adds sin(ftol(T) % 10000 · 0.0001f) · +0xe4 and the pitch the same
// sine · +0xec · 1/3f; the light is +0x68 · (0.1f; 0.1f; 0.25f) and the 0x5ac/9 comes out at its place with the new
// angles, before the common tail advances it by the rotated speed. Three rand(): the common one and the two % 40.
TEST(GameModelEffectsTest, TheAidaWispTurnsWithTheClockAndLeavesATrailBeforeMoving)
{
    const std::uint32_t seed = SeedWhereDraw(1, [](int value) { return value % 40 != 1; });
    ASSERT_NE(Client::Tests::AfterDraws(seed, 2).Next() % 40, 1);
    Client::Effects::ModelEffect wisp = AidaWisp();
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(seed);
    const float clock = 12345.75f;
    ASSERT_TRUE(Client::Effects::StepGameModelEffect(wisp, nullptr, particles, clock, random).HasValue());
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(seed, 3).State());
    const double argument = 2345.0 * static_cast<double>(std::bit_cast<float>(0x38d1b717u));
    const float yaw = static_cast<float>(SineTimes(argument, 1.0f) + 30.0);
    const float pitch =
        static_cast<float>(SineTimes(argument, 1.0f) * static_cast<double>(std::bit_cast<float>(0x3eaaaaabu)) + 10.0);
    EXPECT_EQ(wisp.transform.angles, glm::vec3(pitch, 0.0f, yaw));
    EXPECT_EQ(wisp.light, glm::vec3(0.1f, 0.1f, 0.25f));
    EXPECT_EQ(wisp.life, 999);
    EXPECT_EQ(wisp.state, 0);
    EXPECT_EQ(wisp.speed, 1.0f);
    const auto& trail = particles.All()[0];
    ASSERT_TRUE(trail.alive);
    EXPECT_EQ(trail.type, 0x5ac);
    EXPECT_EQ(trail.subtype, 9);
    EXPECT_EQ(trail.position, glm::vec3(1000.0f, 1000.0f, 100.0f));
    EXPECT_EQ(trail.angles, wisp.transform.angles);
    EXPECT_EQ(trail.light, glm::vec3(0.1f, 0.1f, 0.25f));
    EXPECT_EQ(trail.scale, 0.95f);
    EXPECT_EQ(trail.life, 37); // ftol(0.95f · 40) = ftol(37.999998).
    EXPECT_FALSE(particles.All()[1].alive);
    const glm::vec3 step = Client::Math::OriginalVectorRotate(glm::vec3(0.0f, -4.5f, 0.0f),
                                                              Client::Math::OriginalAngleMatrix(wisp.transform.angles));
    EXPECT_EQ(wisp.transform.position, glm::vec3(static_cast<float>(static_cast<double>(step.x) + 1000.0),
                                                 static_cast<float>(static_cast<double>(step.y) + 1000.0),
                                                 static_cast<float>(static_cast<double>(step.z) + 100.0)));
}

// With rand() % 40 at 1, the direction +0xd8 flips and +0xe4 becomes (rand() % 3 + 5) · direction (the yaw turns the
// other way); the second does the same with +0xec and rand() % 3 + 1. Far from the start, life 50 and state 1, only
// once.
TEST(GameModelEffectsTest, TheAidaWispFlipsItsTurnsAndShortensItsLifeFarFromItsStart)
{
    const std::uint32_t seed = SeedWhereDraw(1, [](int value) { return value % 40 == 1; });
    Client::Crt::CrtRandom draws(seed);
    static_cast<void>(draws.Next());
    static_cast<void>(draws.Next());
    const int yawTurn = draws.Next() % 3 + 5;
    const bool second = draws.Next() % 40 == 1;
    const int pitchTurn = second ? draws.Next() % 3 + 1 : 0;
    Client::Effects::ModelEffect wisp = AidaWisp();
    wisp.start = {1000.0f, 1500.0f, 100.0f};
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(seed);
    ASSERT_TRUE(Client::Effects::StepGameModelEffect(wisp, nullptr, particles, 5000.0f, random).HasValue());
    EXPECT_EQ(random.State(), draws.State());
    EXPECT_EQ(wisp.speed, second ? 1.0f : -1.0f);
    EXPECT_EQ(wisp.turnDirection, static_cast<float>(-yawTurn));
    EXPECT_EQ(wisp.flagEc, second ? static_cast<float>(pitchTurn) : 1.0f);
    const double argument = 5000.0 * static_cast<double>(std::bit_cast<float>(0x38d1b717u));
    EXPECT_EQ(wisp.transform.angles.z, static_cast<float>(SineTimes(argument, static_cast<float>(-yawTurn)) + 30.0));
    // 0x520c3f: at exactly 500 it is not "less"; life 50 minus the tail's.
    EXPECT_EQ(wisp.life, 49);
    EXPECT_EQ(wisp.state, 1);
    ASSERT_TRUE(Client::Effects::StepGameModelEffect(wisp, nullptr, particles, 5000.0f, random).HasValue());
    EXPECT_EQ(wisp.life, 48);

    Client::Effects::ModelEffect near = AidaWisp();
    near.start = {1000.0f, 1499.0f, 100.0f};
    ASSERT_TRUE(Client::Effects::StepGameModelEffect(near, nullptr, particles, 5000.0f, random).HasValue());
    EXPECT_EQ(near.life, 999);
    EXPECT_EQ(near.state, 0);
}

// 0x52c5ca..0x52c7a6, wave 0x6eb/1: after the common rand(), the scale grows (rand() % 5) · 0.1f and the position
// shakes (rand() & 7) − 4 in x and y; with life up to 20 the light is life times 0.05f, and past 20, 40 minus life.
// The tail lowers the life. With seed 1 (41 from the creation, 18467 common, 6334, 26500 and 19169): scale 1.5, x + 0
// and y − 3.
TEST(GameModelEffectsTest, TheGallopShockWaveGrowsShakesAndFadesWithThreeRandoms)
{
    Client::Effects::ModelEffects effects;
    Client::Crt::CrtRandom random(1);
    ASSERT_TRUE(effects.CreateGallopShockWave({100.5f, 200.25f, 300.0f}, {0.0f, 0.0f, 45.0f}, glm::vec3(1.0f), random)
                    .has_value());
    Client::Effects::ModelEffect& wave = effects.Effects()[0];
    Client::Particles::Particles particles;

    ASSERT_TRUE(Client::Effects::StepGameModelEffect(wave, nullptr, particles, 0.0f, random).HasValue());

    EXPECT_EQ(wave.transform.scale, 1.5f);
    EXPECT_EQ(wave.transform.position, glm::vec3(100.5f, 197.25f, 300.0f));
    EXPECT_EQ(wave.light, glm::vec3(1.0f));
    EXPECT_EQ(wave.life, 19);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 5).State());

    ASSERT_TRUE(Client::Effects::StepGameModelEffect(wave, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(wave.light.x), 0x3f733333u);
    EXPECT_EQ(wave.life, 18);

    // Past 20, 40 − 25 = 15: 0.75. A negative scale goes back to 0, unordered too.
    for (const float scale : {-5.0f, std::numeric_limits<float>::quiet_NaN()})
    {
        Client::Effects::ModelEffect young = wave;
        young.life = 25;
        young.transform.scale = scale;
        ASSERT_TRUE(Client::Effects::StepGameModelEffect(young, nullptr, particles, 0.0f, random).HasValue());
        EXPECT_EQ(young.light, glm::vec3(0.75f));
        EXPECT_EQ(young.transform.scale, 0.0f);
        EXPECT_EQ(young.life, 24);
    }
}

// 0x52c649..0x52c6d4 and 0x52c74e: after the common rand(), the /2 grows (rand() % 5) · 0.025f and shakes like the /1;
// the /3 grows 2 and the /4 0.3f, no rand(); a subtype without a branch (9, −1) does not touch the scale. From 0 to 3
// the light follows the life; outside, with life under 6, it dims by 0.769f.
TEST(GameModelEffectsTest, TheOtherShockWavesGrowByTheirOwnSteps)
{
    Client::Particles::Particles particles;
    for (const std::int32_t subtype : {2, 3, 4, 9, -1})
    {
        SCOPED_TRACE(subtype);
        Client::Effects::ModelEffect wave;
        wave.alive = true;
        wave.type = 0x6eb;
        wave.subtype = subtype;
        wave.life = 5;
        wave.transform.position = {100.5f, 200.25f, 300.0f};
        wave.transform.scale = 1.0f;
        wave.light = glm::vec3(1.0f);
        Client::Crt::CrtRandom random(1);
        ASSERT_TRUE(Client::Effects::StepGameModelEffect(wave, nullptr, particles, 0.0f, random).HasValue());
        Client::Crt::CrtRandom replay(1);
        static_cast<void>(replay.Next());
        glm::vec3 position{100.5f, 200.25f, 300.0f};
        float scale = 1.0f;
        if (subtype == 2)
        {
            const int growth = replay.Next() % 5;
            scale = static_cast<float>(growth * static_cast<double>(0.025f) + 1.0);
            for (int axis = 0; axis < 2; ++axis)
            {
                const int shake = replay.Next() & 7;
                position[axis] = static_cast<float>((shake - 4.0) + static_cast<double>(position[axis]));
            }
        }
        else if (subtype == 3)
            scale = 3.0f;
        else if (subtype == 4)
            scale = static_cast<float>(1.0 + static_cast<double>(0.3f));
        EXPECT_EQ(wave.transform.scale, scale);
        EXPECT_EQ(wave.transform.position, position);
        EXPECT_EQ(random.State(), replay.State());
        const float fade = static_cast<float>(static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)));
        EXPECT_EQ(wave.light, subtype >= 0 && subtype <= 3
                                  ? glm::vec3(static_cast<float>(5 * static_cast<double>(0.05f)))
                                  : glm::vec3(fade));
        EXPECT_EQ(wave.life, 4);
    }
}

namespace
{
// 0x5295a4, the Fenrirs' ray 0x14d, after the common rand(): with subtype 0 alpha rises 0.3f until it switches to 1
// with subtype 1; with 1 it drops 0.3f until it dies at 0; the three angles turn 0.15f; and the common life.
TEST(GameModelEffectsTest, TheLightningFlashesOnAndOffAndSpins)
{
    using Client::Effects::ModelEffect;
    ModelEffect bolt;
    bolt.alive = true;
    bolt.type = Client::Effects::LightningEffect;
    bolt.life = 100;
    bolt.alpha = std::bit_cast<float>(0x3f333333u);
    bolt.transform.angles = {10.0f, 20.0f, 30.0f};
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1);
    const auto step = [&]
    {
        const auto stepped = Client::Effects::StepGameModelEffect(bolt, nullptr, particles, 0.0f, random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    };
    const auto spun = [](float angle)
    { return static_cast<float>(static_cast<double>(angle) + static_cast<double>(0.15f)); };

    // 0.7f + 0.3f gives exactly 1 in double: subtype 1 and alpha at 1 on the first step.
    ASSERT_EQ(static_cast<double>(std::bit_cast<float>(0x3f333333u)) + static_cast<double>(0.3f), 1.0);
    step();
    EXPECT_EQ(bolt.subtype, 1);
    EXPECT_EQ(bolt.alpha, 1.0f);
    EXPECT_EQ(bolt.transform.angles, glm::vec3(spun(10.0f), spun(20.0f), spun(30.0f)));
    EXPECT_EQ(bolt.life, 99);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());
    for (int dim = 0; dim < 3; ++dim)
        step();
    EXPECT_TRUE(bolt.alive);
    EXPECT_EQ(bolt.subtype, 1);
    step(); // the fourth subtraction leaves nothing: it dies with alpha at 0.
    EXPECT_FALSE(bolt.alive);
    EXPECT_EQ(bolt.alpha, 0.0f);

    ModelEffect dim = bolt; // below 1 it stays at 0 with the stored sum.
    dim.alive = true;
    dim.subtype = 0;
    dim.life = 50;
    dim.alpha = 0.5f;
    ASSERT_TRUE(Client::Effects::StepGameModelEffect(dim, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_EQ(dim.subtype, 0);
    EXPECT_EQ(dim.alpha, static_cast<float>(0.5 + static_cast<double>(0.3f)));

    ModelEffect negative = bolt; // a negative (or unordered) stored alpha with subtype 0 goes back to 1.
    negative.alive = true;
    negative.subtype = 0;
    negative.life = 50;
    for (const float alpha : {-2.0f, std::numeric_limits<float>::quiet_NaN()})
    {
        negative.alpha = alpha;
        ASSERT_TRUE(Client::Effects::StepGameModelEffect(negative, nullptr, particles, 0.0f, random).HasValue());
        EXPECT_EQ(negative.alpha, 1.0f);
        EXPECT_EQ(negative.subtype, 0);
    }
}

// 0x52965a, decal 0x14e: x turns 0.1f, the light drops 0.05f in the subtype's channels and alpha 0.05f; with more than
// 200 ms since its time it goes to the next frame with a new time, and past the fourth it dies. Without
// timeGetTime(), it stops before touching it.
TEST(GameModelEffectsTest, TheLightningDecalFadesItsColourAndTurnsItsFramesByTheClock)
{
    using Client::Effects::ModelEffect;
    std::uint32_t now = 1000;
    Client::Effects::ModelEffectStepServices services;
    services.systemTime = [&now]() { return now; };
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1);
    const auto fade = [](float value)
    { return static_cast<float>(static_cast<double>(value) - static_cast<double>(0.05f)); };
    struct Case
    {
        std::int32_t subtype;
        glm::vec3 light;
    };
    for (const Case value : {Case{1, {1.0f, fade(1.0f), fade(1.0f)}}, Case{2, {fade(1.0f), fade(1.0f), 1.0f}},
                             Case{3, {fade(1.0f), 1.0f, fade(1.0f)}}, Case{4, glm::vec3(1.0f)}})
    {
        SCOPED_TRACE(value.subtype);
        ModelEffect decal;
        decal.alive = true;
        decal.type = Client::Effects::LightningDecalEffect;
        decal.subtype = value.subtype;
        decal.life = 200;
        decal.alpha = 1.0f;
        decal.light = glm::vec3(1.0f);
        decal.textureTime = 800;
        decal.transform.angles = {5.0f, 0.0f, 45.0f};
        const auto stepped =
            Client::Effects::StepGameModelEffect(decal, nullptr, particles, 0.0f, random, nullptr, services);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(decal.light, value.light);
        EXPECT_EQ(decal.alpha, fade(1.0f));
        EXPECT_EQ(decal.transform.angles, glm::vec3(static_cast<float>(5.0 + static_cast<double>(0.1f)), 0.0f, 45.0f));
        EXPECT_EQ(decal.textureFrame, 0); // 1000 − 800 = 200, which does not pass 200.
        EXPECT_EQ(decal.textureTime, 800u);
        EXPECT_EQ(decal.life, 199);
    }

    ModelEffect decal;
    decal.alive = true;
    decal.type = Client::Effects::LightningDecalEffect;
    decal.subtype = 1;
    decal.life = 200;
    decal.alpha = 1.0f;
    decal.textureTime = 799;
    decal.textureFrame = 3;
    ASSERT_TRUE(
        Client::Effects::StepGameModelEffect(decal, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
    EXPECT_EQ(decal.textureFrame, 4);
    EXPECT_EQ(decal.textureTime, 1000u);
    EXPECT_TRUE(decal.alive);
    now = 1201;
    ASSERT_TRUE(
        Client::Effects::StepGameModelEffect(decal, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
    EXPECT_EQ(decal.textureFrame, 5);
    EXPECT_EQ(decal.textureTime, 1201u);
    EXPECT_FALSE(decal.alive);

    ModelEffect faded = decal; // the alpha subtraction at 0 or less kills it.
    faded.alive = true;
    faded.textureFrame = 0;
    faded.textureTime = now;
    faded.alpha = 0.05f;
    ASSERT_TRUE(
        Client::Effects::StepGameModelEffect(faded, nullptr, particles, 0.0f, random, nullptr, services).HasValue());
    EXPECT_FALSE(faded.alive);

    ModelEffect untouched = faded;
    untouched.alive = true;
    untouched.alpha = 1.0f;
    const ModelEffect before = untouched;
    const auto cut = Client::Effects::StepGameModelEffect(untouched, nullptr, particles, 0.0f, random);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x529701"), std::string::npos) << cut.Error();
    EXPECT_EQ(untouched.alpha, before.alpha);
    EXPECT_EQ(untouched.transform.angles, before.transform.angles);
}
} // namespace

// The horse's earthquake 0xfa and its cracks 0xfb..0x102 (0x521c5c..0x522bd2).
namespace
{
using Client::Effects::ModelEffect;
using Client::Effects::ModelEffectOwner;
using Client::Effects::ModelEffects;

constexpr std::uint16_t HorseType = 0xbf; // Pets::DarkHorseModel
constexpr std::uint16_t SeparateType = 0x1b1;
const glm::vec3 QuakeLight{0.25f, 0.5f, 0.75f};

float Narrow(double value)
{
    return static_cast<float>(value);
}

double Wide(float value)
{
    return static_cast<double>(value);
}

glm::vec3 QuakeTurned(const glm::vec3& offset, const glm::vec3& angles)
{
    return Client::Math::OriginalVectorRotate(offset, Client::Math::OriginalAngleMatrix(angles));
}

// The tests' terrain height: it depends on x and y so the asked point shows.
double QuakeGround(float x, float y)
{
    return Wide(x) * 0.0625 + Wide(y) * 0.03125;
}

// ftol(x · 0.01f) and y · 256 + x, like 0x5f8290.
std::size_t QuakeCell(const glm::vec3& point)
{
    const double step = Wide(std::bit_cast<float>(0x3c23d70au));
    const auto x = static_cast<std::int32_t>(Wide(point.x) * step);
    const auto y = static_cast<std::int32_t>(Wide(point.y) * step);
    return static_cast<std::size_t>(y) * 256u + static_cast<std::size_t>(x);
}

struct QuakeScene
{
    std::unique_ptr<ModelEffects> effects = std::make_unique<ModelEffects>();
    std::unique_ptr<Client::Data::Terrain::TerrainAttributes> attributes =
        std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    std::int32_t map = 0;
    float shake = 0.0f;
    std::vector<std::int32_t> sounds;

    [[nodiscard]] Client::Effects::ModelEffectStepServices Services()
    {
        Client::Effects::ModelEffectStepServices services;
        services.terrainHeight = QuakeGround;
        services.effects = effects.get();
        services.lighting = lighting.get();
        services.terrainAttributes = attributes.get();
        services.map = &map;
        services.cameraShake = &shake;
        services.sound = [this](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); };
        return services;
    }
};

// What 0x51a66d left with the state and subtype 17: the start at the position, +0x28 with 80 and 180 more, +0xe4 at
// 50.
ModelEffect Earthquake(std::uint8_t state, std::int32_t life, const ModelEffectOwner& owner)
{
    ModelEffect quake;
    quake.alive = true;
    quake.type = Client::Effects::EarthquakeEffect;
    quake.subtype = 17;
    quake.state = state;
    quake.life = life;
    quake.transform.scale = 0.9f;
    quake.transform.position = {5000.5f, 6000.25f, 300.0f};
    quake.transform.angles = {0.0f, 0.0f, 375.0f};
    quake.start = quake.transform.position;
    quake.copiedAngles = {80.0f, 0.0f, 225.0f};
    quake.light = QuakeLight;
    quake.turnDirection = 50.0f;
    quake.owner = owner;
    quake.hasOwner = owner.type != nullptr || owner.effect != nullptr || owner.angles != nullptr;
    quake.disturbanceState = 0xffff;
    return quake;
}

void ExpectSameEffects(const ModelEffects& actual, const ModelEffects& expected)
{
    for (std::size_t slot = 0; slot < actual.Effects().size(); ++slot)
    {
        SCOPED_TRACE(slot);
        const ModelEffect& got = actual.Effects()[slot];
        const ModelEffect& want = expected.Effects()[slot];
        ASSERT_EQ(got.alive, want.alive);
        if (!want.alive)
            continue;
        EXPECT_EQ(got.type, want.type);
        EXPECT_EQ(got.subtype, want.subtype);
        EXPECT_EQ(got.transform.position, want.transform.position);
        EXPECT_EQ(got.transform.angles, want.transform.angles);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(got.transform.scale),
                  std::bit_cast<std::uint32_t>(want.transform.scale));
        EXPECT_EQ(got.disturbanceState, want.disturbanceState);
        EXPECT_EQ(got.life, want.life);
        EXPECT_EQ(got.light, want.light);
        EXPECT_EQ(got.velocity, want.velocity);
        EXPECT_EQ(got.objectState.blendMesh, want.objectState.blendMesh);
        EXPECT_EQ(got.owner.type, want.owner.type);
        EXPECT_EQ(got.owner.effect, want.owner.effect);
        EXPECT_EQ(got.owner.angles, want.owner.angles);
    }
}

// A ray of life 11, before looking at the cell: the point with the start's z plus 3, the size and the tilt.
struct QuakeSpoke
{
    glm::vec3 point;
    std::int16_t size;
    glm::vec3 tilt;
};

// The five rays of 0x522004..0x5221ea with their three rand(): rand() % 150 + 100 long at (k · 72 + subtype)°, rand()
// % 50 + 40 in size and (rand() % 30 − 15) + 45 of tilt.
std::array<QuakeSpoke, 5> QuakeSpokes(Client::Crt::CrtRandom& draws, const glm::vec3& start, std::int32_t subtype)
{
    std::array<QuakeSpoke, 5> spokes{};
    for (std::size_t spoke = 0; spoke < spokes.size(); ++spoke)
    {
        const auto reach = static_cast<float>(draws.Next() % 150 + 100);
        const glm::vec3 local{0.0f, 0.0f, Narrow(static_cast<double>(spoke) * 72.0 + static_cast<double>(subtype))};
        const glm::vec3 ray = QuakeTurned({0.0f, reach, 0.0f}, local);
        spokes[spoke].point = {Narrow(Wide(ray.x) + Wide(start.x)), Narrow(Wide(ray.y) + Wide(start.y)),
                               Narrow((Wide(ray.z) + Wide(start.z)) + 3.0)};
        spokes[spoke].size = static_cast<std::int16_t>(draws.Next() % 50 + 40);
        spokes[spoke].tilt = {0.0f, 0.0f, Narrow(static_cast<double>(draws.Next() % 30 - 15) + 45.0)};
    }
    return spokes;
}
} // namespace

// 0x521c8f..0x5221f0, life 11 with the horse's state 2: the start 25 back and 80 to the side rotated by the owner's
// angles, on the ground plus 25; the 0x5ed/0 explosion of 0.5f with its sound 0x40; the start 27 lower; with an owner
// that is not 0x1b1, cracks 0xfd, 0xfb and 0xfc of size 0x96 owned by the earthquake; and five rays with cracks 0xfe
// and 0xff on the ground plus 3, owned by the earthquake's owner. A cell with 0x4, 0x8 or 0x10 leaves its ray without
// cracks. The tail does not move the earthquake: it only takes a life.
TEST(GameModelEffectsTest, TheEarthquakeAtElevenBurstsAndCracksTheGroundAroundIt)
{
    for (const float yaw : {0.0f, 33.0f})
    {
        SCOPED_TRACE(yaw);
        const std::uint16_t horseType = HorseType;
        const glm::vec3 horseAngles{0.0f, 0.0f, yaw};
        const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = &horseAngles};
        ModelEffect quake = Earthquake(2, 11, horse);
        QuakeScene scene;
        Client::Particles::Particles particles;

        // The expected values, with another CRT in the ASM's order.
        Client::Crt::CrtRandom draws(5);
        static_cast<void>(draws.Next()); // 0x5208c5
        const glm::vec3 behind = QuakeTurned({-25.0f, -80.0f, 0.0f}, horseAngles);
        glm::vec3 start{Narrow(Wide(behind.x) + 5000.5), Narrow(Wide(behind.y) + 6000.25), 0.0f};
        start.z = Narrow(QuakeGround(start.x, start.y) + 25.0);
        const glm::vec3 burst = start;
        Client::Particles::Particles expectedParticles;
        ASSERT_TRUE(expectedParticles
                        .CreateChecked(Client::Particles::ExplosionParticle, start, glm::vec3(0.0f), glm::vec3(1.0f), 0,
                                       0.5f, 0.0f, draws)
                        .HasValue());
        start.z = Narrow(Wide(start.z) - 27.0);
        if (yaw == 0.0f)
        {
            EXPECT_EQ(burst, (glm::vec3{4975.5f, 5920.25f, 520.9765625f}));
            EXPECT_EQ(start.z, 493.9765625f);
        }
        auto expected = std::make_unique<ModelEffects>();
        const ModelEffectOwner self{.type = nullptr, .effect = &quake, .angles = &quake.transform.angles};
        for (const std::int16_t type : {std::int16_t{0xfd}, std::int16_t{0xfb}, std::int16_t{0xfc}})
            ASSERT_TRUE(
                expected->CreateEarthquakeCrack(type, start, glm::vec3(0.0f), QuakeLight, 0, self, 0x96).has_value());
        const auto spokes = QuakeSpokes(draws, start, quake.subtype);
        // Ray 1 lands on a cell with a single bit (0x8): no cracks.
        const std::size_t blocked = QuakeCell(spokes[1].point);
        scene.attributes->cells[blocked] = 0x8;
        for (std::size_t spoke = 0; spoke < spokes.size(); ++spoke)
        {
            if (QuakeCell(spokes[spoke].point) == blocked)
                continue;
            glm::vec3 point = spokes[spoke].point;
            point.z = Narrow(QuakeGround(point.x, point.y) + 3.0);
            for (const std::int16_t type : {std::int16_t{0xfe}, std::int16_t{0xff}})
                ASSERT_TRUE(expected
                                ->CreateEarthquakeCrack(type, point, spokes[spoke].tilt, QuakeLight, 0, horse,
                                                        spokes[spoke].size)
                                .has_value());
        }

        Client::Crt::CrtRandom random(5);
        const auto stepped =
            Client::Effects::StepGameModelEffect(quake, nullptr, particles, 0.0f, random, nullptr, scene.Services());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(quake.start, start);
        const auto& explosion = particles.All()[0];
        ASSERT_TRUE(explosion.alive);
        EXPECT_EQ(explosion.type, Client::Particles::ExplosionParticle);
        EXPECT_EQ(explosion.position, burst);
        EXPECT_EQ(explosion.scale, 0.5f);
        EXPECT_EQ(explosion.life, 20);
        EXPECT_FALSE(particles.All()[1].alive);
        EXPECT_EQ(scene.sounds, std::vector<std::int32_t>{0x40});
        ExpectSameEffects(*scene.effects, *expected);
        EXPECT_EQ(scene.effects->Effects()[0].type, 0xfd);
        EXPECT_EQ(scene.effects->Effects()[3].type, 0xfe);
        EXPECT_EQ(scene.effects->Effects()[4].type, 0xff);
        EXPECT_EQ(random.State(), draws.State());
        // 0x52cbb0: no animation or 0x51fc80.
        EXPECT_TRUE(quake.alive);
        EXPECT_EQ(quake.life, 10);
        EXPECT_EQ(quake.transform.position, (glm::vec3{5000.5f, 6000.25f, 300.0f}));
        EXPECT_EQ(quake.previousPosition, (glm::vec3{5000.5f, 6000.25f, 300.0f}));
        EXPECT_EQ(quake.transform.angles, (glm::vec3{0.0f, 0.0f, 375.0f}));
    }
}

// With a 0x1b1 owner (or no owner) the first three cracks do not come out, only the rays'. State 3 leaves the start
// where it was and does not read the owner's angles.
TEST(GameModelEffectsTest, TheEarthquakeSkipsItsFirstCracksForTheSeparateOwnerAndKeepsAnUnanchoredStart)
{
    for (const bool separate : {true, false})
    {
        SCOPED_TRACE(separate);
        const std::uint16_t ownerType = SeparateType;
        const ModelEffectOwner owner{.type = separate ? &ownerType : nullptr, .effect = nullptr, .angles = nullptr};
        ModelEffect quake = Earthquake(3, 11, owner);
        QuakeScene scene;
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(9);
        ASSERT_TRUE(
            Client::Effects::StepGameModelEffect(quake, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        Client::Crt::CrtRandom draws(9);
        static_cast<void>(draws.Next());
        glm::vec3 start{5000.5f, 6000.25f, Narrow(QuakeGround(5000.5f, 6000.25f) + 25.0)};
        Client::Particles::Particles expectedParticles;
        ASSERT_TRUE(expectedParticles
                        .CreateChecked(Client::Particles::ExplosionParticle, start, glm::vec3(0.0f), glm::vec3(1.0f), 0,
                                       0.5f, 0.0f, draws)
                        .HasValue());
        start.z = Narrow(Wide(start.z) - 27.0);
        auto expected = std::make_unique<ModelEffects>();
        for (const QuakeSpoke& spoke : QuakeSpokes(draws, start, quake.subtype))
        {
            const glm::vec3 point{spoke.point.x, spoke.point.y,
                                  Narrow(QuakeGround(spoke.point.x, spoke.point.y) + 3.0)};
            for (const std::int16_t type : {std::int16_t{0xfe}, std::int16_t{0xff}})
                ASSERT_TRUE(expected->CreateEarthquakeCrack(type, point, spoke.tilt, QuakeLight, 0, owner, spoke.size)
                                .has_value());
        }
        EXPECT_EQ(quake.start, start);
        EXPECT_EQ(particles.All()[0].position,
                  (glm::vec3{5000.5f, 6000.25f, Narrow(QuakeGround(5000.5f, 6000.25f) + 25.0)}));
        ExpectSameEffects(*scene.effects, *expected);
        EXPECT_EQ(scene.effects->Effects()[0].type, 0xfe);
        EXPECT_EQ(random.State(), draws.State());
        EXPECT_EQ(quake.life, 10);
    }
}

// In Kalima (24..29 and 36), with state 0 or 2 the earthquake stirs the water (0x437860 at the start's cell, radius 2
// and −1000) and rises 25; with another state it does not stir it and rises 100. The rays' cracks stay 100 above the
// ground.
TEST(GameModelEffectsTest, TheEarthquakeInKalimaDisturbsTheWaterOrRisesHigher)
{
    for (const std::uint8_t state : {std::uint8_t{2}, std::uint8_t{3}})
    {
        SCOPED_TRACE(static_cast<int>(state));
        const std::uint16_t horseType = HorseType;
        const glm::vec3 horseAngles(0.0f);
        const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = &horseAngles};
        ModelEffect quake = Earthquake(state, 11, horse);
        QuakeScene scene;
        scene.map = 24;
        auto water = std::make_unique<Client::MapScripts::Kalima::KalimaWorld>(24);
        auto stillWater = water->CloneForFrame();
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(3);
        const auto stepped = Client::Effects::StepGameModelEffect(quake, nullptr, particles, 0.0f, random, water.get(),
                                                                  scene.Services());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();

        Client::Crt::CrtRandom draws(3);
        static_cast<void>(draws.Next());
        glm::vec3 start = state == 2 ? glm::vec3{4975.5f, 5920.25f, 0.0f} : glm::vec3{5000.5f, 6000.25f, 0.0f};
        auto expectedWater = std::make_unique<Client::MapScripts::Kalima::KalimaWorld>(24);
        if (state == 2)
        {
            ASSERT_TRUE(expectedWater->Disturb(49, 59, 2, -1000, draws).HasValue());
        }
        start.z = Narrow(QuakeGround(start.x, start.y) + (state == 2 ? 25.0 : 100.0));
        EXPECT_EQ(particles.All()[0].position, start);
        EXPECT_EQ(water->Waves().planes, expectedWater->Waves().planes);
        if (state == 2)
        {
            EXPECT_NE(water->Waves().planes, Client::Tests::WavesOf(*stillWater).planes);
        }
        Client::Particles::Particles expectedParticles;
        ASSERT_TRUE(expectedParticles
                        .CreateChecked(Client::Particles::ExplosionParticle, start, glm::vec3(0.0f), glm::vec3(1.0f), 0,
                                       0.5f, 0.0f, draws)
                        .HasValue());
        start.z = Narrow(Wide(start.z) - 27.0);
        const auto spokes = QuakeSpokes(draws, start, quake.subtype);
        const ModelEffect& firstSpoke = scene.effects->Effects()[3];
        EXPECT_EQ(firstSpoke.type, 0xfe);
        EXPECT_EQ(firstSpoke.transform.position,
                  (glm::vec3{spokes[0].point.x, spokes[0].point.y,
                             Narrow(QuakeGround(spokes[0].point.x, spokes[0].point.y) + 100.0)}));
        EXPECT_EQ(random.State(), draws.State());

        // Without Kalima's water, state 2 stops before the height.
        if (state == 2)
        {
            ModelEffect dry = Earthquake(state, 11, horse);
            QuakeScene other;
            other.map = 24;
            const auto cut =
                Client::Effects::StepGameModelEffect(dry, nullptr, particles, 0.0f, random, nullptr, other.Services());
            ASSERT_FALSE(cut.HasValue());
            EXPECT_NE(cut.Error().find("0x521db2"), std::string::npos) << cut.Error();
        }
    }
}

// 0x5224ec..0x52266a: the two groups of four trails reuse creation 0x5192a3. The full pool does not avoid the two
// intermediate rand(), the sound or the parent's swing.
TEST(GameModelEffectsTest, TheDarkLordAtThirteenDropsTwoTrailGroupsBeforeItsSoundAndSwing)
{
    constexpr std::uint32_t Seed = 19;
    constexpr std::int16_t OccupiedType = 0x19f;
    for (const std::size_t freeSlots : {std::size_t{8}, std::size_t{1}, std::size_t{0}})
    {
        SCOPED_TRACE(freeSlots);
        const std::uint16_t horseType = HorseType;
        glm::vec3 horseAngles{0.0f, 0.0f, 0.0f};
        const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = &horseAngles};
        ModelEffect quake = Earthquake(0, 13, horse);
        // The owner turns after the creation; the current position differs from the swing's start.
        horseAngles = {15.0f, 25.0f, 75.0f};
        quake.transform.position = {5100.75f, 6200.5f, 450.25f};
        const glm::vec3 before = quake.transform.position;
        QuakeScene scene;
        auto expected = std::make_unique<ModelEffects>();
        for (std::size_t slot = freeSlots; slot < scene.effects->Effects().size(); ++slot)
        {
            scene.effects->Effects()[slot].alive = true;
            scene.effects->Effects()[slot].type = OccupiedType;
            expected->Effects()[slot] = scene.effects->Effects()[slot];
        }
        Client::Crt::CrtRandom draws(Seed);
        static_cast<void>(draws.Next()); // The parent's prologue.
        const glm::vec3 offset = QuakeTurned({-25.0f, -40.0f, 0.0f}, horseAngles);
        glm::vec3 base;
        for (int axis = 0; axis < 3; ++axis)
            base[axis] = Narrow(Wide(before[axis]) + Wide(offset[axis]));
        for (int group = 0; group < 2; ++group)
        {
            const float spacing = group == 0 ? 50.0f : 30.0f;
            for (int child = 0; child < 4; ++child)
            {
                const glm::vec3 point{base.x, base.y, Narrow(Wide(base.z) - child * Wide(spacing))};
                static_cast<void>(Client::Effects::CreateSpiritTrail(
                    {.type = 0x104, .position = point, .angles = {}, .light = QuakeLight}, *expected));
            }
            if (group == 0)
            {
                base.x = Narrow(Wide(base.x) + (draws.Next() % 30 + 20));
                base.z = Narrow(Wide(base.z) + (draws.Next() % 500 - 250));
            }
        }
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(Seed);
        auto services = scene.Services();
        services.sound = [&](Client::Audio::SoundId sound)
        {
            scene.sounds.push_back(sound.Value());
            EXPECT_EQ(sound, Client::Audio::SoundId{0x96});
            EXPECT_EQ(random.State(), draws.State());
            EXPECT_EQ(quake.life, 13);
            EXPECT_EQ(quake.transform.position, before);
            ExpectSameEffects(*scene.effects, *expected);
        };
        const auto result =
            Client::Effects::StepGameModelEffect(quake, nullptr, particles, 0.0f, random, nullptr, services);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ExpectSameEffects(*scene.effects, *expected);
        EXPECT_EQ(scene.sounds, std::vector<std::int32_t>{0x96});
        EXPECT_EQ(random.State(), draws.State());
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, 3).State());
        EXPECT_TRUE(quake.alive);
        EXPECT_EQ(quake.life, 12);
        EXPECT_EQ(quake.transform.angles.x, 80.0f);
        EXPECT_EQ(quake.turnDirection, 42.0f);
        EXPECT_EQ(quake.previousPosition, before);
        ModelEffect plain = Earthquake(2, 13, horse);
        plain.transform.position = before;
        Client::Crt::CrtRandom plainRandom(Seed);
        ASSERT_TRUE(Client::Effects::StepGameModelEffect(plain, nullptr, particles, 0.0f, plainRandom).HasValue());
        EXPECT_EQ(quake.transform.position, plain.transform.position);
        EXPECT_EQ(quake.velocity, plain.velocity);
        EXPECT_FALSE(particles.All()[0].alive);
        for (std::size_t slot = 0; slot < freeSlots; ++slot)
        {
            const ModelEffect& child = scene.effects->Effects()[slot];
            EXPECT_EQ(child.type, 0x104);
            EXPECT_FALSE(child.hasOwner);
            EXPECT_EQ(child.ownerContext, nullptr);
            EXPECT_EQ(child.owner.effect, nullptr);
        }
    }
}

TEST(GameModelEffectsTest, TheSecondTrailGroupAddsTheSignedRandomOffsetAsAnInteger)
{
    const std::uint16_t horseType = HorseType;
    const glm::vec3 horseAngles(0.0f);
    const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = &horseAngles};
    ModelEffect quake = Earthquake(0, 13, horse);
    constexpr float NonzeroHeight = 0x1p-50f;
    quake.transform.position.z = NonzeroHeight;
    QuakeScene scene;
    Client::Particles::Particles particles;
    // 0x5225fa: the third draw with seed 48 gives 26750. Its integer Z shift is zero.
    Client::Crt::CrtRandom random(48);
    const auto result =
        Client::Effects::StepGameModelEffect(quake, nullptr, particles, 0.0f, random, nullptr, scene.Services());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    const ModelEffect& firstOfSecondGroup = scene.effects->Effects()[4];
    ASSERT_TRUE(firstOfSecondGroup.alive);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(firstOfSecondGroup.transform.position.z),
              std::bit_cast<std::uint32_t>(NonzeroHeight));
}

TEST(GameModelEffectsTest, TheSharedDispatcherStepsAndExpiresAnUnownedSpiritTrail)
{
    auto effects = std::make_unique<ModelEffects>();
    const auto slot = Client::Effects::CreateSpiritTrail(
        {.type = 0x104, .position = {100.0f, 200.0f, 1000.0f}, .angles = {}, .light = QuakeLight}, *effects);
    ASSERT_TRUE(slot.has_value());
    ModelEffect& child = effects->Effects()[*slot];
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1);
    for (int frame = 0; frame < 6; ++frame)
    {
        SCOPED_TRACE(frame);
        const glm::vec3 before = child.transform.position;
        const float fall = child.turnDirection;
        const auto result = Client::Effects::StepGameModelEffect(child, nullptr, particles, 0.0f, random);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(child.previousPosition, before);
        EXPECT_EQ(child.transform.position.z, Narrow(Wide(before.z) - Wide(fall)));
        EXPECT_EQ(child.turnDirection, Narrow(Wide(fall) + 60.0));
        EXPECT_EQ(child.objectState.light, Narrow((6.0 - frame) * Wide(0.05f)));
        EXPECT_EQ(child.life, 5 - frame);
        EXPECT_EQ(child.alive, frame < 5);
    }
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 6).State());
}

// 0x521e13: the real order among the eight attempts, their first spark, the pulse and the cracks.
TEST(GameModelEffectsTest, TheDarkLordEarthquakeEmitsItsBurstEvenWhenThePoolsAreFull)
{
    struct Capacity
    {
        bool jointsFull;
        bool particlesFull;
        bool lastEffectSlot;
        int draws;
    };
    constexpr std::array cases{Capacity{false, false, false, 69}, Capacity{true, true, false, 48},
                               Capacity{true, false, true, 53}, Capacity{false, true, true, 64}};
    for (const auto [jointsFull, particlesFull, lastEffectSlot, draws] : cases)
    {
        SCOPED_TRACE(draws);
        const std::uint16_t ownerType = HorseType;
        const glm::vec3 ownerAngles(0.0f);
        ModelEffect quake = Earthquake(0, 11, {.type = &ownerType, .effect = nullptr, .angles = &ownerAngles});
        QuakeScene scene;
        auto particles = std::make_unique<Client::Particles::Particles>();
        auto tail = std::make_unique<Client::Kernel::GameDrawTailState>();
        if (particlesFull)
            for (auto& particle : particles->All())
                particle.alive = true;
        if (jointsFull)
            for (auto& joint : tail->joints)
                joint.alive = true;
        const auto effects = scene.effects->Effects();
        const std::size_t burstIndex = lastEffectSlot ? effects.size() - 1 : 0;
        for (auto& occupied : effects.first(burstIndex))
            occupied.alive = true;
        Client::Crt::CrtRandom random(1);
        auto services = scene.Services();
        int calls = 0;
        services.joint = [&](const Client::Kernel::JointRequest& request) -> Client::Core::Expected<void, std::string>
        {
            EXPECT_EQ(request.type, 0x617);
            EXPECT_EQ(request.subtype, 0);
            EXPECT_EQ(request.scale, 10.0f);
            EXPECT_EQ(request.ownerContext, nullptr);
            EXPECT_EQ(request.key, -1);
            EXPECT_EQ(request.firstPosition, request.secondPosition);
            EXPECT_GE(request.angles.x, -60.0f);
            EXPECT_LT(request.angles.x, 0.0f);
            EXPECT_GE(request.angles.z, 465.0f);
            EXPECT_LT(request.angles.z, 495.0f);
            if (calls == 0)
            {
                EXPECT_EQ(request.angles, (glm::vec3{-13.0f, 0.0f, 469.0f}));
                EXPECT_EQ(request.firstPosition.x, 4965.5f);
                EXPECT_EQ(request.firstPosition.y, 5919.25f);
                EXPECT_EQ(request.firstPosition.z, 520.9765625f);
            }
            if (!particlesFull)
            {
                EXPECT_EQ(particles->All()[1].alive, calls > 0);
            }
            ++calls;
            const auto created = Client::Joints::CreateFaunaJoint(tail->joints, request, random);
            if (!created.HasValue())
                return Client::Core::Unexpected(created.Error());
            return {};
        };
        const auto stepped =
            Client::Effects::StepGameModelEffect(quake, nullptr, *particles, 0, random, nullptr, services);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(calls, 8);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, draws).State());
        EXPECT_EQ(quake.life, 10);
        const auto& burst = effects[burstIndex];
        EXPECT_TRUE(burst.alive);
        EXPECT_EQ(burst.type, Client::Effects::EarthquakeBurstEffect);
        EXPECT_EQ(burst.transform.position.z, Narrow(Wide(quake.start.z) + 12.0));
        EXPECT_EQ(burst.transform.angles, glm::vec3(0.0f));
        if (!lastEffectSlot)
        {
            EXPECT_EQ(effects[1].type, 0xfd);
            EXPECT_EQ(effects[2].transform.angles, glm::vec3(0.0f));
        }
        EXPECT_EQ(scene.sounds.size(), particlesFull ? 0u : 1u);
        if (!particlesFull)
        {
            EXPECT_EQ(particles->All()[1].type, Client::Particles::FairySparkParticle);
        }
    }
}

TEST(GameModelEffectsTest, TheEarthquakeBurstExpandsDriftsAndExpiresWithNegativeTerrainLight)
{
    ModelEffects effects;
    const auto slot = effects.CreateEarthquakeBurst({5000, 6000, 40}, {}, glm::vec3(0.2f));
    ASSERT_TRUE(slot);
    auto& burst = effects.Effects()[*slot];
    auto particles = std::make_unique<Client::Particles::Particles>();
    QuakeScene scene;
    for (auto& cell : scene.lighting->cells)
        cell = {10.0f, 10.0f, 10.0f};
    Client::Crt::CrtRandom random(1);
    auto services = scene.Services();
    int actionsPublished = 0;
    services.publishAction = [&](std::uint16_t type, std::uint8_t action)
    {
        EXPECT_EQ(type, Client::Effects::EarthquakeBurstEffect);
        EXPECT_EQ(action, burst.animation.action);
        EXPECT_EQ(burst.life, 15 - actionsPublished);
        ++actionsPublished;
    };
    for (int frame = 0; frame < 15; ++frame)
    {
        const auto before = burst.transform;
        const auto life = burst.life;
        const bool expanding = before.scale <= 2.0f;
        const auto stepped =
            Client::Effects::StepGameModelEffect(burst, nullptr, *particles, 0, random, nullptr, services);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(burst.transform.scale, Narrow(Wide(before.scale) + (expanding ? Wide(1.2f) : Wide(0.1f))));
        EXPECT_EQ(burst.transform.position.x, Narrow(Wide(before.position.x) - (expanding ? Wide(1.2f) : 1.0)));
        EXPECT_EQ(burst.transform.position.z, Narrow(Wide(before.position.z) - (expanding ? Wide(1.8f) : 1.5)));
        EXPECT_EQ(burst.objectState.light,
                  expanding ? 1.5f : Narrow(static_cast<double>(life) * Wide(std::bit_cast<float>(0x3d088889u))));
        EXPECT_EQ(burst.transform.position.y, before.position.y);
        EXPECT_EQ(burst.transform.angles, before.angles);
        EXPECT_EQ(burst.life, life - 1);
        EXPECT_EQ(burst.alive, frame < 14);
    }
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 15).State());
    EXPECT_EQ(actionsPublished, 15);
    const auto& center = scene.lighting->cells[60u * 256u + 50u];
    EXPECT_LT(center[0], 10.0f);
    EXPECT_EQ(center[0], center[1]);
    EXPECT_EQ(center[1], center[2]);
}

// Without the joint service, state 0 stops after its four draws of the first attempt; the other services stop at the
// address that needs them.
TEST(GameModelEffectsTest, TheEarthquakeCutsTheDarkLordStateAndWhatItLacks)
{
    const std::uint16_t horseType = HorseType;
    const glm::vec3 horseAngles(0.0f);
    const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = &horseAngles};
    {
        ModelEffect quake = Earthquake(0, 11, horse);
        QuakeScene scene;
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(1);
        const auto cut =
            Client::Effects::StepGameModelEffect(quake, nullptr, particles, 0.0f, random, nullptr, scene.Services());
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x521ef5"), std::string::npos) << cut.Error();
        EXPECT_EQ(cut.Error().find("no están portados"), std::string::npos) << cut.Error();
        EXPECT_TRUE(particles.All()[0].alive);
        EXPECT_EQ(scene.sounds, std::vector<std::int32_t>{0x40});
        EXPECT_FALSE(scene.effects->Effects()[0].alive);
    }
    const auto cutWith = [](std::uint8_t state, std::int32_t life, const ModelEffectOwner& owner,
                            const std::function<void(QuakeScene&, Client::Effects::ModelEffectStepServices&)>& take)
    {
        ModelEffect quake = Earthquake(state, life, owner);
        QuakeScene scene;
        auto services = scene.Services();
        take(scene, services);
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(1);
        const auto cut =
            Client::Effects::StepGameModelEffect(quake, nullptr, particles, 0.0f, random, nullptr, services);
        return cut.HasValue() ? std::string("sin corte") : cut.Error();
    };
    const ModelEffectOwner blind{.type = &horseType, .effect = nullptr, .angles = nullptr};
    EXPECT_NE(cutWith(2, 11, blind, [](QuakeScene&, auto&) {}).find("0x521cf8"), std::string::npos);
    EXPECT_NE(cutWith(2, 11, horse, [](QuakeScene&, auto& services) { services.map = nullptr; }).find("0x521d55"),
              std::string::npos);
    EXPECT_NE(cutWith(2, 11, horse, [](QuakeScene&, auto& services) { services.terrainHeight = {}; }).find("0x521dce"),
              std::string::npos);
    EXPECT_NE(cutWith(2, 11, horse, [](QuakeScene&, auto& services) { services.sound = {}; }).find("0x542a82"),
              std::string::npos);
    EXPECT_NE(cutWith(2, 11, horse, [](QuakeScene&, auto& services) { services.effects = nullptr; }).find("0x521fb9"),
              std::string::npos);
    EXPECT_NE(cutWith(2, 11, horse, [](QuakeScene&, auto& services) { services.terrainAttributes = nullptr; })
                  .find("0x52211d"),
              std::string::npos);
    EXPECT_NE(cutWith(2, 10, horse, [](QuakeScene&, auto& services) { services.sound = {}; }).find("0x52248b"),
              std::string::npos);
    EXPECT_NE(cutWith(2, 10, horse, [](QuakeScene&, auto& services) { services.terrainAttributes = nullptr; })
                  .find("0x522378"),
              std::string::npos);
    // Q-C425: 0x5f8290 does not mask; a cell outside the 256 × 256 stops.
    const auto outside = cutWith(3, 11, horse, [](QuakeScene&, auto&) {});
    EXPECT_EQ(outside, "sin corte");
    ModelEffect far = Earthquake(3, 11, horse);
    far.start = {5000.5f, -500.0f, 0.0f};
    QuakeScene scene;
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1);
    const auto cut =
        Client::Effects::StepGameModelEffect(far, nullptr, particles, 0.0f, random, nullptr, scene.Services());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52211d: la celda (49, -3) de 0x5f8290 cae fuera"), std::string::npos) << cut.Error();
}

// 0x5221fe..0x522493, life 10: four rings of five cracks 0x101 and 0x102 of size 100 coming out of the start. Each ring
// advances (rand() % 15) + 85; the first three alternate out (rand() % 30 + 50) and in (−50 − rand() % 30) and the
// fourth is decided by a new rand() (Q-C429); the turn adds (rand() % 10 + 62) · i. Only a cell with all three bits
// leaves the crack uncreated (a single bit does not, Q-C424); the crack faces 270° more. Then life at 0 and sound 0x97;
// the tail leaves it at −1 and it dies.
TEST(GameModelEffectsTest, TheEarthquakeAtTenOpensFourRingsOfCracks)
{
    const std::uint16_t horseType = HorseType;
    const glm::vec3 horseAngles(0.0f);
    const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = &horseAngles};
    ModelEffect quake = Earthquake(2, 10, horse);
    QuakeScene scene;

    struct RingCrack
    {
        glm::vec3 point;
        glm::vec3 local;
    };
    std::array<std::array<RingCrack, 5>, 4> plan{};
    Client::Crt::CrtRandom draws(11);
    static_cast<void>(draws.Next());
    std::array<glm::vec3, 5> points{};
    points.fill(quake.start);
    std::array<float, 5> sums{};
    int parity = 0;
    for (std::size_t ring = 0; ring < 4; ++ring)
    {
        const float reach = Narrow(static_cast<double>(draws.Next() % 15) + 85.0);
        if (ring == 3)
            parity = draws.Next();
        const bool outward = parity % 2 == 0;
        for (std::size_t crack = 0; crack < 5; ++crack)
        {
            const int push = outward ? draws.Next() % 30 + 50 : -50 - draws.Next() % 30;
            sums[crack] = Narrow(static_cast<double>(push) + Wide(sums[crack]));
            const int turn = (draws.Next() % 10 + 62) * static_cast<int>(crack);
            const glm::vec3 local{0.0f, 0.0f, Narrow(static_cast<double>(turn) + Wide(sums[crack]))};
            const glm::vec3 step = QuakeTurned({0.0f, reach, 0.0f}, local);
            for (int axis = 0; axis < 3; ++axis)
                points[crack][axis] = Narrow(Wide(step[axis]) + Wide(points[crack][axis]));
            plan[ring][crack] = {points[crack], local};
            // Only a created crack writes the ground's z; the cells are chosen below, before running.
            points[crack].z = Narrow(QuakeGround(points[crack].x, points[crack].y) + 3.0);
        }
        ++parity;
    }
    // Crack 2 of the first ring lands on a cell with all three bits, and 3 on one with a single bit.
    const std::size_t walled = QuakeCell(plan[0][2].point);
    const std::size_t marked = QuakeCell(plan[0][3].point);
    ASSERT_NE(walled, marked);
    scene.attributes->cells[walled] = 0x1c;
    scene.attributes->cells[marked] = 0x08;

    auto expected = std::make_unique<ModelEffects>();
    Client::Crt::CrtRandom replay(11);
    static_cast<void>(replay.Next());
    points.fill(quake.start);
    sums.fill(0.0f);
    parity = 0;
    std::size_t walledCracks = 0;
    for (std::size_t ring = 0; ring < 4; ++ring)
    {
        const float reach = Narrow(static_cast<double>(replay.Next() % 15) + 85.0);
        if (ring == 3)
            parity = replay.Next();
        const bool outward = parity % 2 == 0;
        for (std::size_t crack = 0; crack < 5; ++crack)
        {
            const int push = outward ? replay.Next() % 30 + 50 : -50 - replay.Next() % 30;
            sums[crack] = Narrow(static_cast<double>(push) + Wide(sums[crack]));
            const int turn = (replay.Next() % 10 + 62) * static_cast<int>(crack);
            glm::vec3 local{0.0f, 0.0f, Narrow(static_cast<double>(turn) + Wide(sums[crack]))};
            const glm::vec3 step = QuakeTurned({0.0f, reach, 0.0f}, local);
            for (int axis = 0; axis < 3; ++axis)
                points[crack][axis] = Narrow(Wide(step[axis]) + Wide(points[crack][axis]));
            if (scene.attributes->cells[QuakeCell(points[crack])] == 0x1c)
            {
                ++walledCracks;
                continue;
            }
            points[crack].z = Narrow(QuakeGround(points[crack].x, points[crack].y) + 3.0);
            local.z = Narrow(Wide(local.z) + 270.0);
            for (const std::int16_t type : {std::int16_t{0x101}, std::int16_t{0x102}})
                ASSERT_TRUE(
                    expected->CreateEarthquakeCrack(type, points[crack], local, QuakeLight, 0, horse, 100).has_value());
        }
        ++parity;
    }
    EXPECT_GE(walledCracks, 1u);

    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(11);
    const auto stepped =
        Client::Effects::StepGameModelEffect(quake, nullptr, particles, 0.0f, random, nullptr, scene.Services());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ExpectSameEffects(*scene.effects, *expected);
    EXPECT_EQ(scene.effects->Effects()[0].type, 0x101);
    EXPECT_EQ(scene.effects->Effects()[1].type, 0x102);
    EXPECT_EQ(scene.effects->Effects()[0].disturbanceState, 100u);
    EXPECT_EQ(scene.effects->Effects()[0].life, 40);
    EXPECT_EQ(random.State(), replay.State());
    EXPECT_EQ(scene.sounds, std::vector<std::int32_t>{0x97});
    EXPECT_FALSE(particles.All()[0].alive);
    EXPECT_FALSE(quake.alive);
    EXPECT_EQ(quake.life, -1);
}

// 0x522498..0x522765, the other lives: the pitch adds 80; the speed's y is sin((20 − phase) · step · π / 180) · 260
// with the phase at the life and step 15, or 12.5 and 18 between 10 and 15; +0xe4 drops 8 with the phase at 12.5 and
// otherwise rises 8 (at 15, it first changes sign); the position is the start plus the speed rotated by +0x28, with z
// plus +0xe4 and 200. One rand(), the prologue's.
TEST(GameModelEffectsTest, TheEarthquakeSwingsAroundItsStartBetweenItsBursts)
{
    struct SwingCase
    {
        std::int32_t life;
        double phase;
        double pace;
        float bob;
        double sine;
    };
    constexpr double Pi = std::bit_cast<double>(0x400921fafc8b007aull);
    constexpr double InverseDegrees = std::bit_cast<double>(0x3f76c16c16c16c17ull);
    for (const SwingCase& swing :
         {SwingCase{18, 18.0, 15.0, 58.0f, 130.0}, SwingCase{14, 12.5, 18.0, 42.0f, 183.8478},
          SwingCase{15, 12.5, 18.0, -58.0f, 183.8478}, SwingCase{19, 19.0, 15.0, 58.0f, 67.2929}})
    {
        SCOPED_TRACE(swing.life);
        ModelEffect quake = Earthquake(2, swing.life, {});
        quake.transform.angles = {10.0f, 0.0f, 375.0f};
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(1);
        ASSERT_TRUE(Client::Effects::StepGameModelEffect(quake, nullptr, particles, 0.0f, random).HasValue());
        EXPECT_EQ(quake.transform.angles, (glm::vec3{90.0f, 0.0f, 375.0f}));
        const double argument = (((20.0 - swing.phase) * swing.pace) * Pi) * InverseDegrees;
        const float sine = Narrow(SineTimes(argument, 260.0f));
        EXPECT_EQ(quake.velocity, (glm::vec3{0.0f, sine, 0.0f}));
        EXPECT_NEAR(quake.velocity.y, swing.sine, 1e-3);
        EXPECT_EQ(quake.turnDirection, swing.bob);
        const glm::vec3 moved = QuakeTurned(quake.velocity, quake.copiedAngles);
        EXPECT_EQ(quake.transform.position, (glm::vec3{Narrow(Wide(moved.x) + 5000.5), Narrow(Wide(moved.y) + 6000.25),
                                                       Narrow(((Wide(moved.z) + 300.0) + Wide(swing.bob)) + 200.0)}));
        EXPECT_EQ(quake.start, (glm::vec3{5000.5f, 6000.25f, 300.0f}));
        EXPECT_EQ(quake.previousPosition, (glm::vec3{5000.5f, 6000.25f, 300.0f}));
        EXPECT_EQ(quake.life, swing.life - 1);
        EXPECT_TRUE(quake.alive);
        EXPECT_TRUE(particles.All()[0].alive == false);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());
    }
}

namespace
{
// A crack as 0x518cf0 leaves it, with scale 1.5 and the given owner.
ModelEffect Crack(std::int16_t type, std::int32_t life, const ModelEffectOwner& owner)
{
    ModelEffect crack;
    crack.alive = true;
    crack.type = type;
    crack.life = life;
    crack.transform.scale = 1.5f;
    crack.transform.position = {5000.5f, 6000.25f, 300.0f};
    crack.transform.angles = {0.0f, 0.0f, 45.0f};
    crack.light = QuakeLight;
    crack.owner = owner;
    crack.hasOwner = owner.type != nullptr || owner.effect != nullptr || owner.angles != nullptr;
    crack.animation.action = 0xff;
    return crack;
}

float CrackGlow(double factor)
{
    return Narrow(factor);
}

const double CrackTenth = Wide(std::bit_cast<float>(0x3dcccccdu));
const double CrackThird = Wide(std::bit_cast<float>(0x3eaaaaabu));

// The dynamic light with radius 1 that a crack leaves with the first rand()'s local light.
void ExpectCrackLight(const QuakeScene& scene, const ModelEffect& crack, const glm::vec3& color)
{
    auto expected = std::make_unique<Client::Terrain::TerrainLighting>();
    ASSERT_TRUE(
        Client::Terrain::AddDynamicLight(expected.get(), crack.transform.position, color, 1, "test").HasValue());
    EXPECT_EQ(scene.lighting->cells, expected->cells);
}

float CrackLuminosity(std::uint32_t seed)
{
    Client::Crt::CrtRandom draws(seed);
    return Narrow(static_cast<double>((draws.Next() & 3) + 7) * CrackTenth);
}
} // namespace

// 0x52276a: the 0xfb glows life · 0.1f · 1/3f, sinks 0.5f with under 10 and, with more than 15 and a multiple of 3,
// shakes the camera (rand() % 8 − 4) · 0.1f. No dynamic light; the tail animates it and takes a life.
TEST(GameModelEffectsTest, TheFirstCrackGlowsSinksAndShakesTheCamera)
{
    const std::uint16_t horseType = HorseType;
    const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = nullptr};
    for (const std::int32_t life : {18, 16, 9})
    {
        SCOPED_TRACE(life);
        ModelEffect crack = Crack(0xfb, life, horse);
        QuakeScene scene;
        scene.shake = 7.0f;
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(4);
        ASSERT_TRUE(
            Client::Effects::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        EXPECT_EQ(crack.objectState.light, CrackGlow(static_cast<double>(life) * CrackTenth * CrackThird));
        EXPECT_EQ(crack.transform.position.z, life < 10 ? 299.5f : 300.0f);
        Client::Crt::CrtRandom draws(4);
        static_cast<void>(draws.Next());
        if (life == 18)
        {
            EXPECT_EQ(scene.shake, Narrow(static_cast<double>(draws.Next() % 8 - 4) * CrackTenth));
        }
        else
        {
            EXPECT_EQ(scene.shake, 7.0f);
        }
        EXPECT_EQ(random.State(), draws.State());
        EXPECT_EQ(scene.lighting->cells, std::make_unique<Client::Terrain::TerrainLighting>()->cells);
        EXPECT_EQ(crack.life, life - 1);
        EXPECT_EQ(crack.previousPosition, (glm::vec3{5000.5f, 6000.25f, 300.0f}));
    }
    ModelEffect crack = Crack(0xfb, 18, horse);
    QuakeScene scene;
    auto services = scene.Services();
    services.cameraShake = nullptr;
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(4);
    const auto cut = Client::Effects::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5227ca"), std::string::npos) << cut.Error();
}

// 0x5227d5 and 0x522b04: the 0xfd and the 0x100 glow life · 0.1f · 0.1f and sink with under 13; the 0xfe and the
// 0x101, life · 0.1f · 1/3f and with under 10. No own rand() or dynamic light.
TEST(GameModelEffectsTest, TheQuietCracksOnlyGlowAndSink)
{
    struct QuietCase
    {
        std::int16_t type;
        std::int32_t life;
        double third;
        bool sinks;
    };
    for (const QuietCase& quiet : {QuietCase{0xfd, 12, CrackTenth, true}, QuietCase{0x100, 13, CrackTenth, false},
                                   QuietCase{0xfe, 9, CrackThird, true}, QuietCase{0x101, 10, CrackThird, false}})
    {
        SCOPED_TRACE(quiet.type);
        ModelEffect crack = Crack(quiet.type, quiet.life, {});
        QuakeScene scene;
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(4);
        ASSERT_TRUE(
            Client::Effects::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        EXPECT_EQ(crack.objectState.light, CrackGlow(static_cast<double>(quiet.life) * CrackTenth * quiet.third));
        EXPECT_EQ(crack.transform.position.z, quiet.sinks ? 299.5f : 300.0f);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(4, 1).State());
        EXPECT_EQ(crack.life, quiet.life - 1);
        EXPECT_FALSE(scene.effects->Effects()[0].alive);
    }
}

// 0x5227f0: the 0xfc with scale above 50 loses its life; with 10 or more it glows (20 − life) · 0.1f; with less, life ·
// 0.1f, sinks below 5 and, with 5 or more, rand() % 10 at 0 releases a small stone 0xc8 + (rand() & 1) at rand() % 150
// turned rand() % 360. Always, +0x6c at life · −0.01f and the dynamic light with radius 1: red with an owner that is
// not 0x1b1.
TEST(GameModelEffectsTest, TheWideCrackDropsChipsAndLightsTheGroundRed)
{
    const std::uint16_t horseType = HorseType;
    const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = nullptr};
    {
        ModelEffect crack = Crack(0xfc, 15, horse);
        QuakeScene scene;
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(6);
        ASSERT_TRUE(
            Client::Effects::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        EXPECT_EQ(crack.objectState.light, CrackGlow(5.0 * CrackTenth));
        EXPECT_EQ(crack.objectState.uv.x, Narrow(15.0 * Wide(std::bit_cast<float>(0xbc23d70au))));
        EXPECT_EQ(crack.transform.position.z, 300.0f);
        ExpectCrackLight(scene, crack, {CrackLuminosity(6), 0.0f, 0.0f});
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(6, 1).State());
        EXPECT_EQ(crack.life, 14);
    }
    for (const bool chips : {true, false})
    {
        SCOPED_TRACE(chips);
        const std::uint32_t seed = chips ? SeedWhereDraw(1, [](int value) { return value % 10 == 0; })
                                         : SeedWhereDraw(1, [](int value) { return value % 10 != 0; });
        // The owner is the earthquake that created it: its type 0xfa is not 0x1b1.
        ModelEffect quake = Earthquake(2, 5, {});
        const ModelEffectOwner self{.type = nullptr, .effect = &quake, .angles = &quake.transform.angles};
        ModelEffect crack = Crack(0xfc, 7, self);
        QuakeScene scene;
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(seed);
        ASSERT_TRUE(
            Client::Effects::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        EXPECT_EQ(crack.objectState.light, CrackGlow(7.0 * CrackTenth));
        Client::Crt::CrtRandom draws(seed);
        static_cast<void>(draws.Next());
        auto expected = std::make_unique<ModelEffects>();
        if (draws.Next() % 10 == 0)
        {
            const glm::vec3 reach{0.0f, static_cast<float>(draws.Next() % 150), 0.0f};
            const glm::vec3 turn{0.0f, 0.0f, static_cast<float>(draws.Next() % 360)};
            const glm::vec3 offset = QuakeTurned(reach, turn);
            const glm::vec3 point{Narrow(Wide(offset.x) + 5000.5), Narrow(Wide(offset.y) + 6000.25),
                                  Narrow(Wide(offset.z) + 300.0)};
            const auto type = static_cast<std::int16_t>(0xc8 + draws.Next() % 2);
            ASSERT_TRUE(expected->CreateStoneChip(type, point, crack.transform.angles, QuakeLight, draws).has_value());
        }
        EXPECT_EQ(scene.effects->Effects()[0].alive, chips);
        ExpectSameEffects(*scene.effects, *expected);
        EXPECT_EQ(random.State(), draws.State());
        ExpectCrackLight(scene, crack, {CrackLuminosity(seed), 0.0f, 0.0f});
    }
    {
        ModelEffect crack = Crack(0xfc, 3, horse);
        crack.transform.scale = 50.5f;
        QuakeScene scene;
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(6);
        ASSERT_TRUE(
            Client::Effects::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        // Life at 0: sunk, no glow, +0x6c at −0 (0 · −0.01f) and no small-stone rand(); the tail kills it.
        EXPECT_EQ(crack.objectState.light, 0.0f);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(crack.objectState.uv.x), 0x80000000u);
        EXPECT_EQ(crack.transform.position.z, 299.5f);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(6, 1).State());
        EXPECT_FALSE(crack.alive);
        EXPECT_EQ(crack.life, -1);
    }
    {
        const std::uint32_t seed = SeedWhereDraw(1, [](int value) { return value % 10 == 0; });
        ModelEffect crack = Crack(0xfc, 7, horse);
        QuakeScene scene;
        auto services = scene.Services();
        services.effects = nullptr;
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(seed);
        const auto cut =
            Client::Effects::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, services);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x522a94"), std::string::npos) << cut.Error();
    }
    {
        ModelEffect crack = Crack(0xfc, 15, horse);
        QuakeScene scene;
        auto services = scene.Services();
        services.lighting = nullptr;
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(6);
        const auto cut =
            Client::Effects::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, services);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x522af7"), std::string::npos) << cut.Error();
    }
    {
        // An owner without a type to read stops at the read at 0x522ab2.
        const glm::vec3 angles(0.0f);
        ModelEffect crack = Crack(0xfc, 15, {.type = nullptr, .effect = nullptr, .angles = &angles});
        QuakeScene scene;
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(6);
        const auto cut =
            Client::Effects::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services());
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x522ab2"), std::string::npos) << cut.Error();
    }
}

// 0x52294c: the 0xff glows (40 − life) · 0.1f with 30 or more; otherwise life · 0.1f, sinks below 15 and, with 5 or
// more, rand() % 15 at 0 releases a small stone. The light is blue with a 0x1b1 owner and white without an owner
// (Q-C427).
TEST(GameModelEffectsTest, TheLongCrackLightsBlueForTheSeparateOwnerAndWhiteWithoutOne)
{
    const std::uint16_t separate = SeparateType;
    {
        ModelEffect crack = Crack(0xff, 35, {.type = &separate, .effect = nullptr, .angles = nullptr});
        QuakeScene scene;
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(8);
        ASSERT_TRUE(
            Client::Effects::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        EXPECT_EQ(crack.objectState.light, CrackGlow(5.0 * CrackTenth));
        EXPECT_EQ(crack.transform.position.z, 300.0f);
        ExpectCrackLight(scene, crack, {0.0f, 0.0f, CrackLuminosity(8)});
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(8, 1).State());
    }
    for (const bool chips : {true, false})
    {
        SCOPED_TRACE(chips);
        const std::uint32_t seed = chips ? SeedWhereDraw(1, [](int value) { return value % 15 == 0; })
                                         : SeedWhereDraw(1, [](int value) { return value % 15 != 0; });
        ModelEffect crack = Crack(0xff, 12, {});
        QuakeScene scene;
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(seed);
        ASSERT_TRUE(
            Client::Effects::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        EXPECT_EQ(crack.objectState.light, CrackGlow(12.0 * CrackTenth));
        EXPECT_EQ(crack.transform.position.z, 299.5f);
        EXPECT_EQ(scene.effects->Effects()[0].alive, chips);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(seed, chips ? 2 + 3 + 5 : 2).State());
        ExpectCrackLight(scene, crack, glm::vec3(1.0f));
    }
}

// 0x522b31: the 0x102 glows (40 − life) · 0.1f with 30 or more and otherwise life · 0.1f, sinks below 15 and with 13 or
// more draws a rand() nobody uses (Q-C428). The light goes through 0x5212ce: red with the horse.
TEST(GameModelEffectsTest, TheLastCrackDrawsARandomItNeverUses)
{
    const std::uint16_t horseType = HorseType;
    const ModelEffectOwner horse{.type = &horseType, .effect = nullptr, .angles = nullptr};
    struct LastCase
    {
        std::int32_t life;
        double glow;
        bool sinks;
        int draws;
    };
    for (const LastCase& last : {LastCase{33, 7.0, false, 2}, LastCase{20, 20.0, false, 2}, LastCase{13, 13.0, true, 2},
                                 LastCase{12, 12.0, true, 1}})
    {
        SCOPED_TRACE(last.life);
        ModelEffect crack = Crack(0x102, last.life, horse);
        QuakeScene scene;
        Client::Particles::Particles particles;
        Client::Crt::CrtRandom random(2);
        ASSERT_TRUE(
            Client::Effects::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, scene.Services())
                .HasValue());
        EXPECT_EQ(crack.objectState.light, CrackGlow(last.glow * CrackTenth));
        EXPECT_EQ(crack.transform.position.z, last.sinks ? 299.5f : 300.0f);
        EXPECT_EQ(random.State(), Client::Tests::AfterDraws(2, last.draws).State());
        ExpectCrackLight(scene, crack, {CrackLuminosity(2), 0.0f, 0.0f});
    }
    ModelEffect crack = Crack(0x102, 20, horse);
    QuakeScene scene;
    auto services = scene.Services();
    services.lighting = nullptr;
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(2);
    const auto cut = Client::Effects::StepGameModelEffect(crack, nullptr, particles, 0.0f, random, nullptr, services);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5212da"), std::string::npos) << cut.Error();
}

TEST(PiercingShotTest, NativeBirthOffsetsTheFirstFreeSlotWithoutRandom)
{
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    const auto slot = effects->CreatePiercingShot(
        {.type = 0x621, .position = {10, 20, 30}, .angles = {}, .light = {0.2f, 0.4f, 0.6f}});
    ASSERT_TRUE(slot);
    EXPECT_EQ(*slot, 0u);
    const auto& effect = effects->Effects()[*slot];
    EXPECT_EQ(effect.life, 10);
    EXPECT_EQ(effect.transform.position, (glm::vec3{10, -80, 180}));
    EXPECT_EQ(effect.light, (glm::vec3{0.2f, 0.4f, 0.6f}));
    EXPECT_FLOAT_EQ(effect.transform.scale, 0.9f);
}

TEST(PiercingShotTest, ThreeEmissionsRetainActorRgbAndSpendSixteenDrawsEvenWithFullPools)
{
    using namespace Client;
    for (const int free : {0, 1, 3})
    {
        SCOPED_TRACE(free);
        auto effects = std::make_unique<Effects::ModelEffects>();
        Particles::Particles particles;
        Particles::FrameSprites sprites;
        for (auto& sprite : sprites.Sprites())
            sprite.alive = true;
        for (auto& particle : particles.All())
            particle.alive = true;
        for (int index = 0; index < free; ++index)
        {
            sprites.Sprites()[static_cast<std::size_t>(index)].alive = false;
            particles.All()[static_cast<std::size_t>(index)].alive = false;
        }
        const auto slot =
            effects->CreatePiercingShot({.type = 0x621, .position = {10, 20, 30}, .light = {0.2f, 0.4f, 0.6f}});
        ASSERT_TRUE(slot);
        auto& effect = effects->Effects()[*slot];
        effect.life = 1;
        Crt::CrtRandom random(17);
        Crt::CrtRandom expected(17);
        static_cast<void>(expected.Next());
        const Effects::ModelEffectStepServices services{.sprites = &sprites, .effects = effects.get()};
        const auto stepped = Effects::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_FALSE(effect.alive);
        for (int index = 0; index < 3; ++index)
        {
            const glm::vec3 angles{static_cast<float>(expected.Next() % 360), 0,
                                   static_cast<float>(expected.Next() % 360)};
            const auto offset = Math::OriginalVectorRotate(glm::vec3{0, 120, 0}, Math::OriginalAngleMatrix(angles));
            const float rotation = static_cast<float>(expected.Next() % 360);
            const float spriteScale =
                static_cast<float>(static_cast<double>((expected.Next() & 7) + 8) * static_cast<double>(0.3f));
            const float particleScale =
                static_cast<float>(static_cast<double>(expected.Next() % 50 + 10) * static_cast<double>(0.01f));
            if (index >= free)
                continue;
            const auto& sprite = sprites.Sprites()[static_cast<std::size_t>(index)];
            const auto& particle = particles.All()[static_cast<std::size_t>(index)];
            EXPECT_EQ(sprite.light, effect.light);
            EXPECT_EQ(sprite.position, effect.transform.position);
            EXPECT_FLOAT_EQ(sprite.rotation, rotation);
            EXPECT_FLOAT_EQ(sprite.scale, spriteScale);
            EXPECT_EQ(particle.type, 0x5c6);
            EXPECT_EQ(particle.subtype, 2);
            EXPECT_EQ(particle.life, 15);
            EXPECT_EQ(particle.light, glm::vec3(1));
            EXPECT_EQ(particle.angles, glm::vec3(0));
            EXPECT_EQ(particle.start, particle.position);
            EXPECT_EQ(particle.typedOwner,
                      (Particles::ParticleOwner{Particles::ParticleOwnerKind::ModelEffect, *slot}));
            EXPECT_FLOAT_EQ(particle.scale, particleScale);
            for (int axis = 0; axis < 3; ++axis)
            {
                const float position =
                    static_cast<float>(static_cast<double>(offset[axis]) + effect.transform.position[axis]);
                EXPECT_FLOAT_EQ(particle.position[axis], position);
                EXPECT_FLOAT_EQ(particle.velocity[axis],
                                static_cast<float>((static_cast<double>(effect.transform.position[axis]) - position) *
                                                   static_cast<double>(0.1f)));
            }
        }
        EXPECT_EQ(random.State(), expected.State());
    }
}

TEST(PiercingShotTest, BirthUsesTheFirstFreePhysicalSlotAndLeavesRetainedFields)
{
    using namespace Client;
    auto effects = std::make_unique<Effects::ModelEffects>();
    for (auto& effect : effects->Effects())
        effect.alive = true;
    EXPECT_FALSE(effects->CreatePiercingShot({.type = 0x621}));
    auto& retained = effects->Effects()[37];
    retained.alive = false;
    retained.start = {11, 22, 33};
    retained.copiedAngles = {44, 55, 66};
    int actor = 0;
    const auto slot =
        effects->CreatePiercingShot({.type = 0x621, .position = {10, 20, 30}, .angles = {15, 25, 35}, .owner = &actor});
    ASSERT_TRUE(slot);
    EXPECT_EQ(*slot, 37u);
    EXPECT_EQ(retained.start, (glm::vec3{11, 22, 33}));
    EXPECT_EQ(retained.copiedAngles, (glm::vec3{44, 55, 66}));
    EXPECT_EQ(retained.ownerContext, &actor);
    const auto offset =
        Math::OriginalVectorRotate(glm::vec3{0, -100, 0}, Math::OriginalAngleMatrix(glm::vec3{15, 25, 35}));
    EXPECT_FLOAT_EQ(retained.transform.position.x, static_cast<float>(static_cast<double>(offset.x) + 10));
    EXPECT_FLOAT_EQ(retained.transform.position.y, static_cast<float>(static_cast<double>(offset.y) + 20));
    EXPECT_FLOAT_EQ(retained.transform.position.z, static_cast<float>(static_cast<double>(offset.z) + 30 + 150));
}

TEST(PiercingShotTest, ParentResolverReadsTheSamePhysicalSlotAfterReuse)
{
    using namespace Client;
    auto world = std::make_unique<Kernel::WorldState>();
    Particles::Particle particle;
    particle.type = Particles::SparkParticle;
    particle.subtype = Particles::PiercingSparkSubtype;
    particle.typedOwner = {Particles::ParticleOwnerKind::ModelEffect, 37};
    const auto resolve = Effects::ParticleOwnerBonesOf(*world);
    EXPECT_FALSE(resolve(particle));
    auto& parent = world->Map().effects.Effects()[37];
    parent.alive = true;
    parent.transform.position = {10, 20, 30};
    ASSERT_TRUE(resolve(particle));
    EXPECT_EQ(*resolve(particle), parent.transform.position);
    parent.alive = false;
    EXPECT_FALSE(resolve(particle));
    parent.alive = true;
    parent.type = Effects::TeleportEffect;
    parent.transform.position = {100, 200, 300};
    ASSERT_TRUE(resolve(particle));
    EXPECT_EQ(*resolve(particle), parent.transform.position);
    particle.subtype = 0;
    EXPECT_FALSE(resolve(particle));
    particle.subtype = Particles::PiercingSparkSubtype;
    particle.typedOwner.identity = world->Map().effects.Effects().size();
    EXPECT_FALSE(resolve(particle));
}
