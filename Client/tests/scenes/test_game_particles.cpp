#include "Scenes/GameParticles.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Textures/Texture.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/TextureMetadata.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/Particles.h"
#include "World/TerrainCulling.h"
#include "World/WorldObjects.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

// P-C55 (a): contrato estático de 603011 y de los calls 637128/637558. Sin ejecutar el PE.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particle;
using Client::World::Particles;
using Client::World::WorldObject;
WorldObject Fountain()
{
    WorldObject object;
    object.type = Client::World::Waterspout;
    object.transform.angles = {11.0f, 22.0f, 33.0f};
    object.light = {0.2f, 0.4f, 0.6f};
    object.state.light = 0.25f;
    object.state.uv = {0.3f, 0.5f};
    return object;
}
Particle Smoke()
{
    Particle p;
    p.alive = true;
    p.type = Client::World::BreathParticle;
    p.texture = Client::World::BreathParticle;
    p.life = 16;
    p.moves = true;
    p.scale = 0.5f;
    p.light = {0.25f, 0.5f, 0.75f};
    p.position = {10.0f, 20.0f, 1000.0f};
    return p;
}
class SmokeTextures final : public Client::Scenes::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    std::optional<Client::Scenes::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override
    {
        if (selector != Client::World::BreathParticle && selector != 0x999)
        {
            return std::nullopt;
        }
        Client::Scenes::TextureMetadata metadata(Client::Data::Textures::Texture{});
        metadata.size = {32.0f, 16.0f};
        metadata.components = 3;
        return metadata;
    }
    std::optional<Client::Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override
    {
        return selector == Client::World::BreathParticle
                   ? std::optional(Client::Render::TextureSlot{Client::World::BreathParticle})
                   : std::nullopt;
    }
};

class FountainModels final : public Client::Scenes::IModelLibrary
{
public:
    FountainModels()
    {
        skeleton.actions.push_back({true, 0.0f, 1, false});
        skeleton.bones.resize(5);
        for (std::size_t i = 0; i < skeleton.bones.size(); ++i)
        {
            Client::Render::Models::BoneKey key;
            key.position = {static_cast<float>(i * 10u), 0.0f, 0.0f};
            skeleton.bones[i].actions = {{key}};
        }
    }
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return &model;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return &skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    Client::Data::Models::Model model;
    Client::Render::Models::Skeleton skeleton;
};
} // namespace

TEST(GameParticlesTest, WaterspoutInterleavesOffsetsPoseReadsAndConstructorRandoms)
{
    WorldObject object = Fountain();
    Particles particles;
    CrtRandom random(0);
    std::vector<std::size_t> bones;
    std::vector<glm::vec3> offsets;
    std::vector<std::uint32_t> randomAtPose;
    const auto point = [&](std::size_t bone, const glm::vec3& offset) -> Client::Core::Expected<glm::vec3, std::string>
    {
        bones.push_back(bone);
        offsets.push_back(offset);
        randomAtPose.push_back(random.State());
        return glm::vec3(100.0f, 200.0f, 300.0f) + offset;
    };
    const float clock = 1234.0f;
    const auto result = Client::Scenes::StepGameWaterspoutFrame(object, particles, random, clock, point);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(bones, (std::vector<std::size_t>{1, 4}));
    EXPECT_EQ(offsets, (std::vector<glm::vec3>{{-9.0f, -20.0f, 6.0f}, {5.0f, -80.0f, -3.0f}}));
    EXPECT_EQ(randomAtPose, (std::vector<std::uint32_t>{0xd2f65b55u, 0x20ad96a9u}));
    EXPECT_EQ(random.State(), 0xa8d2826bu);
    EXPECT_EQ(object.state.light, 1.0f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(object.state.uv.y), 0xbe6f9db3u);
    EXPECT_EQ(object.state.uv.x, 0.3f);
    for (std::size_t i = 0; i < 2; ++i)
    {
        const Particle& p = particles.All()[i];
        EXPECT_TRUE(p.alive);
        EXPECT_EQ(p.type, Client::World::BreathParticle);
        EXPECT_EQ(p.texture, Client::World::BreathParticle);
        EXPECT_EQ(p.subtype, 0);
        EXPECT_EQ(p.owner, 0u);
        EXPECT_EQ(p.life, 16);
        EXPECT_EQ(p.position, (i == 0 ? glm::vec3(91.0f, 180.0f, 306.0f) : glm::vec3(105.0f, 120.0f, 297.0f)));
        EXPECT_EQ(p.start, p.position);
        EXPECT_EQ(p.angles, (i == 0 ? glm::vec3(215.0f, 22.0f, 33.0f) : glm::vec3(10.0f, 22.0f, 33.0f)));
        EXPECT_EQ(p.rotation, 154.0f);
        EXPECT_EQ(p.light, object.light);
        EXPECT_EQ(p.verticalSpeed, 0.0f);
        EXPECT_EQ(p.velocity, glm::vec3(0.0f));
    }
    EXPECT_FALSE(particles.All()[2].alive);
}

TEST(GameParticlesTest, OddGuardNeedsNoPoseAndInvalidClockStopsBeforeTheGuard)
{
    WorldObject object = Fountain();
    Particles particles;
    CrtRandom random(1);
    const float clock = 1234.0f;
    ASSERT_TRUE(Client::Scenes::StepGameWaterspoutFrame(object, particles, random, clock, {}).HasValue());
    EXPECT_FALSE(particles.Any());
    EXPECT_EQ(random.State(), 0x29e2c0u);
    object.state.uv.y = 0.5f;
    const float infinity = std::numeric_limits<float>::infinity();
    const std::uint32_t before = random.State();
    const auto failed = Client::Scenes::StepGameWaterspoutFrame(object, particles, random, infinity, {});
    ASSERT_FALSE(failed.HasValue());
    EXPECT_NE(failed.Error().find("0x60301e"), std::string::npos);
    EXPECT_EQ(object.state.light, 1.0f);
    EXPECT_EQ(object.state.uv.y, 0.5f);
    EXPECT_EQ(random.State(), before);
}

TEST(GameParticlesTest, FullPoolConsumesBothOffsetsButNoConstructorRandoms)
{
    WorldObject object = Fountain();
    Particles particles;
    for (Particle& p : particles.All())
    {
        p.alive = true;
    }
    CrtRandom random(0);
    const float clock = 0.0f;
    std::vector<std::size_t> bones;
    const auto point = [&](std::size_t bone, const glm::vec3& offset) -> Client::Core::Expected<glm::vec3, std::string>
    {
        bones.push_back(bone);
        return offset;
    };
    ASSERT_TRUE(Client::Scenes::StepGameWaterspoutFrame(object, particles, random, clock, point).HasValue());
    EXPECT_EQ(random.State(), 0xa2974c77u);
    EXPECT_EQ(bones, (std::vector<std::size_t>{1, 4}));
    particles.All().back().alive = false;
    CrtRandom oneFree(0);
    ASSERT_TRUE(Client::Scenes::StepGameWaterspoutFrame(object, particles, oneFree, clock, point).HasValue());
    EXPECT_TRUE(particles.All().back().alive);
    EXPECT_EQ(particles.All().back().type, Client::World::BreathParticle);
    EXPECT_EQ(oneFree.State(), 0x20ad96a9u); // Solo la primera alcanza a crearse: siete sorteos.
}

TEST(GameParticlesTest, EachConstructorReadsTheLiveClockAfterItsBoneCallback)
{
    WorldObject object = Fountain();
    Particles particles;
    CrtRandom random(0);
    float clock = 1234.0f;
    const auto point = [&clock](std::size_t bone,
                                const glm::vec3& offset) -> Client::Core::Expected<glm::vec3, std::string>
    {
        clock = bone == 1 ? 2000.0f : 4000.0f;
        return offset;
    };
    ASSERT_TRUE(Client::Scenes::StepGameWaterspoutFrame(object, particles, random, clock, point).HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(object.state.uv.y), 0xbe6f9db3u);
    EXPECT_EQ(particles.All()[0].rotation, 200.0f);
    EXPECT_EQ(particles.All()[1].rotation, 40.0f);
}

TEST(GameParticlesTest, SecondBoneFailureKeepsTheFirstParticleAndTheRandomPrefix)
{
    WorldObject object = Fountain();
    Particles particles;
    CrtRandom random(0);
    const float clock = 0.0f;
    const auto point = [](std::size_t bone, const glm::vec3& offset) -> Client::Core::Expected<glm::vec3, std::string>
    {
        if (bone == 4)
        {
            return Client::Core::Unexpected(std::string("falta el hueso retenido"));
        }
        return offset;
    };
    const auto failed = Client::Scenes::StepGameWaterspoutFrame(object, particles, random, clock, point);
    ASSERT_FALSE(failed.HasValue());
    EXPECT_NE(failed.Error().find("0x603121"), std::string::npos);
    EXPECT_EQ(random.State(), 0x20ad96a9u);
    EXPECT_TRUE(particles.All()[0].alive);
    EXPECT_FALSE(particles.All()[1].alive);
}

TEST(GameParticlesTest, StepConsumesWindEvenWhenEmptyAndMovesTheLastDeadTick)
{
    Particles particles;
    CrtRandom random;
    CrtRandom expected;
    constexpr std::int32_t Map = 0;
    ASSERT_TRUE(Client::Scenes::StepGameParticles(particles, random, Map).HasValue());
    static_cast<void>(expected.Next());
    static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
    particles.All()[0] = Smoke();
    particles.All()[0].velocity = {1.0f, 0.0f, 0.0f};
    for (int tick = 0; tick < 16; ++tick)
    {
        const float previousZ = particles.All()[0].position.z;
        ASSERT_TRUE(Client::Scenes::StepGameParticles(particles, random, Map).HasValue());
        EXPECT_GT(particles.All()[0].position.z, previousZ);
        EXPECT_EQ(particles.All()[0].alive, tick < 15);
        static_cast<void>(expected.Next());
        static_cast<void>(expected.Next());
    }
    EXPECT_EQ(particles.All()[0].position.x, 26.0f);
    EXPECT_EQ(particles.All()[0].life, 0);
    EXPECT_EQ(random.State(), expected.State());
}

TEST(GameParticlesTest, UnsupportedStepKeepsWindAndTheAlreadyAdvancedSlots)
{
    Particles particles;
    particles.All()[0] = Smoke();
    particles.All()[1] = Smoke();
    particles.All()[1].type = 0x999;
    particles.All()[2] = Smoke();
    CrtRandom random;
    const std::int32_t map = 2;
    const auto failed = Client::Scenes::StepGameParticles(particles, random, map);
    ASSERT_FALSE(failed.HasValue());
    EXPECT_NE(failed.Error().find("0x637128"), std::string::npos);
    EXPECT_EQ(particles.All()[0].life, 15);
    EXPECT_EQ(particles.All()[0].scale, 0.55f);
    EXPECT_EQ(particles.All()[1].life, 15);
    EXPECT_EQ(particles.All()[2].life, 16);
    EXPECT_NE(random.State(), 1u);
}

TEST(GameParticlesTest, DrawUsesModeZeroAndEyeCoordinatesWithoutKillingParticles)
{
    Particles particles;
    particles.All()[0] = Smoke();
    particles.All()[1] = Smoke();
    particles.All()[1].position.z = 100.0f;
    SmokeTextures textures;
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto view = Client::Render::Models::OriginalAngleMatrix(glm::vec3(0.0f));
    const auto drawn = Client::Scenes::DrawGameParticles(batch, particles, textures, view, 0.0f);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_EQ(batch.Vertices().size(), 12u);
    EXPECT_EQ(batch.Vertices()[0].position, glm::vec3(2.0f, 16.0f, 1000.0f));
    EXPECT_EQ(batch.Vertices()[0].uv, glm::vec2(0.0f, 1.0f));
    EXPECT_EQ(batch.Vertices()[0].color, (Client::Render::Color{0.25f, 0.5f, 0.75f, 1.0f}));
    EXPECT_EQ(batch.State().space, Client::Render::ModelCoordinateSpace::Eye);
    EXPECT_TRUE(particles.All()[0].alive);
    EXPECT_TRUE(particles.All()[1].alive);
}

TEST(GameParticlesTest, DrawFailureKeepsTheGeometryAndReportsAddressAndSlot)
{
    Particles particles;
    particles.All()[0] = Smoke();
    particles.All()[1] = Smoke();
    particles.All()[1].type = 0x999;
    particles.All()[2] = Smoke();
    SmokeTextures textures;
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto view = Client::Render::Models::OriginalAngleMatrix(glm::vec3(0.0f));
    const auto failed = Client::Scenes::DrawGameParticles(batch, particles, textures, view, 0.0f);
    ASSERT_FALSE(failed.HasValue());
    EXPECT_NE(failed.Error().find("0x637558"), std::string::npos);
    EXPECT_NE(failed.Error().find("lugar 1"), std::string::npos);
    EXPECT_EQ(batch.Vertices().size(), 6u);
}

TEST(GameParticlesTest, TheGameObjectPassEmitsFromThePosedBoneMatrixAfterItsCommonRandom)
{
    FountainModels models;
    std::array objects{Fountain()};
    WorldObject& object = objects[0];
    object.transform.position = {100.0f, 200.0f, 300.0f};
    object.transform.angles = glm::vec3(0.0f);
    object.transform.scale = 2.0f;
    Particles particles;
    CrtRandom random(2);
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto drawn = Client::Scenes::DrawGameWorldObjects(
        batch, models, objects, Client::World::TerrainPolygon{.everything = true}, nullptr, {},
        Client::Scenes::GameObjectPass{.map = 0, .random = &random, .worldClock = 1234.0f, .particles = &particles});
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_TRUE(particles.All()[0].alive);
    ASSERT_TRUE(particles.All()[1].alive);
    CrtRandom expected(2);
    static_cast<void>(expected.Next()); // 0x602ef4, antes de 0x60303c.
    ASSERT_EQ(expected.Next() % 2, 0);
    for (std::size_t i = 0; i < 2; ++i)
    {
        const float x = static_cast<float>((expected.Next() % 32) - 16);
        const float z = static_cast<float>((expected.Next() % 32) - 16);
        const float boneX = i == 0 ? 120.0f : 180.0f;
        const float y = i == 0 ? 160.0f : 40.0f;
        EXPECT_EQ(particles.All()[i].position, glm::vec3(boneX + (2.0f * x), y, 300.0f + (2.0f * z)));
        static_cast<void>(expected.Next());
        static_cast<void>(expected.Next());
    }
    EXPECT_EQ(random.State(), expected.State());
}

TEST(GameParticlesTest, AnUnwrittenBoneStopsWithoutInventingItsRetainedPose)
{
    FountainModels models;
    models.skeleton.bones[4].dummy = true;
    std::array objects{Fountain()};
    Particles particles;
    CrtRandom random(2);
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto failed = Client::Scenes::DrawGameWorldObjects(
        batch, models, objects, Client::World::TerrainPolygon{.everything = true}, nullptr, {},
        Client::Scenes::GameObjectPass{.map = 0, .random = &random, .particles = &particles});
    ASSERT_FALSE(failed.HasValue());
    EXPECT_NE(failed.Error().find("0x603121"), std::string::npos);
    EXPECT_TRUE(particles.All()[0].alive);
    EXPECT_FALSE(particles.All()[1].alive);
    CrtRandom expected(2);
    for (int i = 0; i < 8; ++i)
    {
        static_cast<void>(expected.Next());
    }
    EXPECT_EQ(random.State(), expected.State());
}

TEST(GameParticlesTest, AnIntermediateObjectDrawDoesNotEmitOrConsumeRandom)
{
    FountainModels models;
    std::array objects{Fountain()};
    Particles particles;
    CrtRandom random(2);
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Client::Scenes::DrawGameWorldObjects(
                    batch, models, objects, Client::World::TerrainPolygon{.everything = true}, nullptr, {},
                    Client::Scenes::GameObjectPass{
                        .map = 0, .originalFrame = false, .random = &random, .particles = &particles})
                    .HasValue());
    EXPECT_FALSE(particles.Any());
    EXPECT_EQ(random.State(), 2u);
}

TEST(GameParticlesTest, TheFountainReadsThePreviousWorldMatricesForDummyAndTransparentBones)
{
    for (const bool transparent : {false, true})
    {
        SCOPED_TRACE(transparent);
        FountainModels models;
        Client::Scenes::FaunaDrawCarry carry;
        Client::Scenes::DrawnObject previous;
        previous.model = 0x12;
        previous.transform.position = {100.0f, 200.0f, 300.0f};
        previous.transform.scale = 2.0f;
        ASSERT_TRUE(Client::Scenes::PublishFaunaPose(carry, models, previous, true).HasValue());
        models.skeleton.bones[1].dummy = !transparent;
        models.skeleton.bones[4].dummy = !transparent;
        std::array objects{Fountain()};
        objects[0].transform.position = {1000.0f, 2000.0f, 3000.0f};
        objects[0].transform.scale = 7.0f;
        objects[0].alpha = transparent ? 0.005f : 1.0f;
        Particles particles;
        CrtRandom random(2);
        Client::Render::ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        const auto result = Client::Scenes::DrawGameWorldObjects(
            batch, models, objects, Client::World::TerrainPolygon{.everything = true}, nullptr, {},
            Client::Scenes::GameObjectPass{.map = 0,
                                           .random = &random,
                                           .particles = &particles,
                                           .onPose = [&](const Client::Scenes::DrawnObject& drawn)
                                           { return Client::Scenes::PublishFaunaPose(carry, models, drawn, true); },
                                           .retainedPose = &carry});
        ASSERT_TRUE(result.HasValue()) << result.Error();
        CrtRandom expected(2);
        static_cast<void>(expected.Next()); // Común del objeto, antes del guard de la fuente.
        ASSERT_EQ(expected.Next() % 2, 0);
        for (std::size_t i = 0; i < 2; ++i)
        {
            const float x = static_cast<float>((expected.Next() % 32) - 16);
            const float z = static_cast<float>((expected.Next() % 32) - 16);
            const float boneX = i == 0 ? 120.0f : 180.0f;
            const float y = i == 0 ? 160.0f : 40.0f;
            ASSERT_TRUE(particles.All()[i].alive);
            EXPECT_EQ(particles.All()[i].position, glm::vec3(boneX + (2.0f * x), y, 300.0f + (2.0f * z)));
            static_cast<void>(expected.Next());
            static_cast<void>(expected.Next());
        }
        EXPECT_EQ(random.State(), expected.State());
    }
}

TEST(GameParticlesTest, AFullFountainPoolStillReadsTheRetainedBonesAtTheirNativeRandomPrefix)
{
    for (const bool missingSecondBone : {false, true})
    {
        SCOPED_TRACE(missingSecondBone);
        FountainModels models;
        Client::Scenes::FaunaDrawCarry carry;
        Client::Scenes::DrawnObject previous;
        previous.model = 0x12;
        previous.transform.position = {100.0f, 200.0f, 300.0f};
        ASSERT_TRUE(Client::Scenes::PublishFaunaPose(carry, models, previous, true).HasValue());
        if (missingSecondBone)
            carry.bones.resize(2);
        std::array objects{Fountain()};
        objects[0].alpha = 0.005f;
        Particles particles;
        for (auto& particle : particles.All())
        {
            particle.alive = true;
            particle.type = 0x777;
        }
        CrtRandom random(2);
        Client::Render::ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        const auto result = Client::Scenes::DrawGameWorldObjects(
            batch, models, objects, Client::World::TerrainPolygon{.everything = true}, nullptr, {},
            Client::Scenes::GameObjectPass{
                .map = 0, .random = &random, .particles = &particles, .retainedPose = &carry});
        if (missingSecondBone)
        {
            ASSERT_FALSE(result.HasValue());
            EXPECT_NE(result.Error().find("0x603121"), std::string::npos);
        }
        else
            ASSERT_TRUE(result.HasValue()) << result.Error();
        CrtRandom expected(2);
        for (int i = 0; i < 6; ++i)
            static_cast<void>(expected.Next()); // Común, guard y dos parejas X/Z; ningún constructor.
        EXPECT_EQ(random.State(), expected.State());
        for (const auto& particle : particles.All())
            EXPECT_EQ(particle.type, 0x777);
    }
}

TEST(GameParticlesTest, AnEmptyRetainedFountainLoanStopsAfterTheFirstOffsetInsteadOfReposing)
{
    FountainModels models;
    Client::Scenes::FaunaDrawCarry carry;
    std::array objects{Fountain()};
    Particles particles;
    CrtRandom random(2);
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto result = Client::Scenes::DrawGameWorldObjects(
        batch, models, objects, Client::World::TerrainPolygon{.everything = true}, nullptr, {},
        Client::Scenes::GameObjectPass{.map = 0, .random = &random, .particles = &particles, .retainedPose = &carry});
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x6030a5"), std::string::npos);
    EXPECT_FALSE(particles.Any());
    CrtRandom expected(2);
    for (int i = 0; i < 4; ++i)
        static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
}
