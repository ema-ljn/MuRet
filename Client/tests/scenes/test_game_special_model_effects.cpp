#include "Scenes/GameDrawTail.h"

#include "Core/Expected.h"
#include "Render/Models/ObjectState.h"
#include "World/ModelEffects.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skinning.h"
#include <vector>
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "World/TerrainLighting.h"
#include "World/Sprites.h"
#include "World/ITerrainAttributeQuerySource.h"
#include "Data/Terrain/TerrainAttributes.h"
#include <memory>
#include <gtest/gtest.h>
#include <glm/ext/vector_float2.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace
{
class NoModels final : public Client::Scenes::IModelLibrary
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
};
class TriangleModels final : public Client::Scenes::IModelLibrary
{
public:
    TriangleModels()
    {
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "plain.jpg";
        model.meshes = {mesh};
        Client::Render::Models::SkeletonBone bone;
        bone.actions = {{Client::Render::Models::BoneKey{}}};
        skeleton.bones = {bone};
        skeleton.actions = {Client::Render::Models::ActionInfo{.keyCount = 1}};
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
        return textures;
    }
    Client::Data::Models::Model model;
    Client::Render::Models::Skeleton skeleton;
    std::array<Client::Render::TextureSlot, 1> textures{Client::Render::TextureSlot{0x400}};
};
class AttributeSource final : public Client::World::ITerrainAttributeQuerySource
{
public:
    float ReadScale() override
    {
        ++reads;
        return -0.01f;
    }
    const Client::Data::Terrain::TerrainAttributes& Attributes() override
    {
        return *table;
    }
    std::unique_ptr<Client::Data::Terrain::TerrainAttributes> table =
        std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    int reads = 0;
};

Client::World::ModelEffect ItemEffect(std::int16_t type, const std::int16_t& itemType, const std::uint8_t& itemLevel)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = type;
    effect.life = 11;
    effect.itemType = &itemType;
    effect.itemLevel = &itemLevel;
    effect.transform.position = {1000, 1000, 300};
    effect.transform.scale = 3;
    effect.light = {0.7f, 0.8f, 0.9f};
    effect.alpha = 0.4f;
    effect.vertexLight = false;
    effect.animation = {3, 4.5f, 2, 7};
    effect.speed = 9;
    effect.turnPending = -3;
    effect.turnDirection = -1;
    effect.objectState = {4, 0.2f, {0.4f, 0.6f}, 5};
    return effect;
}

void ExpectItemInitialization(const Client::World::ModelEffect& effect, float scale, float turnDirection)
{
    EXPECT_EQ(effect.light, glm::vec3(0.3f));
    EXPECT_TRUE(effect.vertexLight);
    EXPECT_FLOAT_EQ(effect.speed, 0);
    EXPECT_FLOAT_EQ(effect.turnPending, 50);
    EXPECT_FLOAT_EQ(effect.turnDirection, turnDirection);
    EXPECT_FLOAT_EQ(effect.alpha, 1);
    EXPECT_FLOAT_EQ(effect.transform.scale, scale);
    EXPECT_EQ(effect.animation.action, 0);
    EXPECT_FLOAT_EQ(effect.animation.frame, 0);
    EXPECT_EQ(effect.animation.previousAction, 0);
    EXPECT_EQ(effect.animation.previousKey, 0);
}

void ExpectItemPose(const Client::Scenes::DrawnObject& object, float scale)
{
    EXPECT_TRUE(object.vertexLight);
    EXPECT_FLOAT_EQ(object.alpha, 1);
    EXPECT_FLOAT_EQ(object.transform.scale, scale);
    EXPECT_EQ(object.animation.action, 0);
    EXPECT_FLOAT_EQ(object.animation.frame, 0);
    EXPECT_EQ(object.animation.previousAction, 0);
    EXPECT_EQ(object.animation.previousKey, 0);
}

} // namespace
TEST(GameSpecialModelEffectsTest, MissingSpritesPreservesTheFirstSubtypeOneRotationAndWhiteLight)
{
    NoModels models;
    Client::Render::ModelBatch batch;
    std::array<Client::World::ModelEffect, 2> effects{};
    auto& effect = effects[0];
    effect.alive = true;
    effect.type = 0x5e8;
    effect.subtype = 1;
    effect.transform.angles.z = 10;
    effect.light = {0.1f, 0.2f, 0.3f};
    effects[1].alive = true;
    effects[1].type = 0x5ea;
    const auto result = Client::Scenes::DrawGameModelEffects(batch, models, effects, {}, nullptr, {}, 0);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x52d99e"), std::string::npos);
    EXPECT_FLOAT_EQ(effect.transform.angles.z, 9.9f);
    EXPECT_EQ(effect.light, glm::vec3(1));
    EXPECT_TRUE(effect.seen);
    EXPECT_FALSE(effects[1].seen);
}

TEST(GameSpecialModelEffectsTest, TwentySpriteSamplesAccumulateRealDynamicLightWithoutModels)
{
    NoModels models;
    Client::Render::ModelBatch batch;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    auto light = std::make_unique<Client::World::TerrainLighting>();
    Client::World::FrameSprites sprites;
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = 0x5e9;
    effect.transform.position = {1000, 1000, 40};
    effect.transform.scale = 2;
    effect.light = {0.5f, 0.2f, 0.1f};
    effect.velocity = {100, 0, 0};
    Client::Scenes::ModelEffectDrawResources resources{*heights};
    resources.sprites = &sprites;
    resources.dynamicLighting = light.get();
    ASSERT_TRUE(
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), {}, nullptr, {}, 0, resources)
            .HasValue());
    for (std::size_t i = 0; i < 20; ++i)
    {
        EXPECT_TRUE(sprites.Sprites()[i].alive);
        EXPECT_EQ(sprites.Sprites()[i].texture, 0x5c6);
        EXPECT_FLOAT_EQ(sprites.Sprites()[i].position.x, static_cast<float>(1000 + 100 * i));
        EXPECT_FLOAT_EQ(sprites.Sprites()[i].scale, 2);
    }
    EXPECT_FALSE(sprites.Sprites()[20].alive);
    EXPECT_GT(light->cells[10 * 256 + 11][0], 0);
    EXPECT_TRUE(batch.Vertices().empty());
}

TEST(GameSpecialModelEffectsTest, SubtypeOneMutatesTwentyTimesEvenWhenSpritesAreFull)
{
    NoModels models;
    Client::Render::ModelBatch batch;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    auto light = std::make_unique<Client::World::TerrainLighting>();
    Client::World::FrameSprites sprites;
    for (auto& sprite : sprites.Sprites())
        sprite.alive = true;
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = 0x5ea;
    effect.subtype = 1;
    effect.transform.position = {1000, 1000, 0};
    effect.transform.angles.z = 10;
    Client::Scenes::ModelEffectDrawResources resources{*heights};
    resources.sprites = &sprites;
    resources.dynamicLighting = light.get();
    ASSERT_TRUE(
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), {}, nullptr, {}, 0, resources)
            .HasValue());
    EXPECT_NEAR(effect.transform.angles.z, 8, 0.00001f);
    EXPECT_EQ(effect.light, (glm::vec3{0.2f, 0, 0}));
    EXPECT_FALSE(batch.Vertices().empty());
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Opaque);
}

TEST(GameSpecialModelEffectsTest, SiegeBarrierReturnsTheEntirePassAfterItsFirstSprite)
{
    NoModels models;
    Client::Render::ModelBatch batch;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    auto light = std::make_unique<Client::World::TerrainLighting>();
    Client::World::FrameSprites sprites;
    AttributeSource attributes;
    attributes.table->cells[10 * 256 + 11] = 0x100;
    std::array<Client::World::ModelEffect, 2> effects{};
    effects[0].alive = true;
    effects[0].type = 0x5e8;
    effects[0].transform.position = {1050, 1050, 0};
    effects[0].velocity = {100, 0, 0};
    effects[0].light = glm::vec3(1);
    effects[1].alive = true;
    effects[1].type = 0x5ea;
    Client::Scenes::ModelEffectDrawResources resources{*heights};
    resources.sprites = &sprites;
    resources.dynamicLighting = light.get();
    resources.attributes = &attributes;
    resources.siegeActive = true;
    ASSERT_TRUE(Client::Scenes::DrawGameModelEffects(batch, models, effects, {}, nullptr, {}, 0, resources).HasValue());
    EXPECT_TRUE(sprites.Sprites()[0].alive);
    EXPECT_FALSE(sprites.Sprites()[1].alive);
    EXPECT_EQ(attributes.reads, 2);
    EXPECT_FALSE(effects[1].seen);
    EXPECT_FLOAT_EQ(light->cells[10 * 256 + 11][0], 0);
}

TEST(GameSpecialModelEffectsTest, MissingDynamicLightPreservesTheSpriteAndStopsBeforeTheSecondEffect)
{
    NoModels models;
    Client::Render::ModelBatch batch;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::World::FrameSprites sprites;
    std::array<Client::World::ModelEffect, 2> effects{};
    for (auto& effect : effects)
    {
        effect.alive = true;
        effect.type = 0x5e8;
    }
    Client::Scenes::ModelEffectDrawResources resources{*heights};
    resources.sprites = &sprites;
    const auto result = Client::Scenes::DrawGameModelEffects(batch, models, effects, {}, nullptr, {}, 0, resources);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x52da6a"), std::string::npos);
    EXPECT_TRUE(sprites.Sprites()[0].alive);
    EXPECT_FALSE(sprites.Sprites()[1].alive);
    EXPECT_FALSE(effects[1].seen);
}

TEST(GameSpecialModelEffectsTest, FallenItemGuardDoesNotRequireItsOwnerOrResources)
{
    NoModels models;
    Client::Render::ModelBatch batch;
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = 0xfa;
    effect.life = 10;
    ASSERT_TRUE(
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), {}, nullptr, {}, 0).HasValue());
    EXPECT_TRUE(batch.Vertices().empty());
    effect.life = 11;
    effect.state = 1;
    ASSERT_TRUE(
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), {}, nullptr, {}, 0).HasValue());
}

TEST(GameSpecialModelEffectsTest, SpinningItemRequiresItsOwnerBeforeStoringAngularVelocity)
{
    NoModels models;
    Client::Render::ModelBatch batch;
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = 0xf4;
    effect.velocity.z = 60;
    const auto result = Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), {}, nullptr, {}, 0);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x52d294"), std::string::npos);
    EXPECT_FLOAT_EQ(effect.velocity.z, 60);
}

TEST(GameSpecialModelEffectsTest, SpinningItemPublishesItsTemporaryPoseAndRestoresOnlyTypeAndTransform)
{
    TriangleModels models;
    Client::Scenes::FaunaDrawCarry carry;
    Client::Render::ModelBatch batch;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Render::Models::CrtRandom random(3);
    Client::Render::Models::CrtRandom expected(3);
    std::int16_t itemType = 0;
    std::uint8_t itemLevel = 0;
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = 0xf4;
    effect.transform.position = {100, 200, 300};
    effect.transform.angles = {10, 20, 30};
    effect.velocity.z = 60;
    effect.itemType = &itemType;
    effect.itemLevel = &itemLevel;
    Client::Scenes::ModelEffectDrawResources resources{*heights};
    resources.random = &random;
    resources.onPose = [&](const Client::Scenes::DrawnObject& object)
    {
        EXPECT_EQ(object.model, 0x212);
        EXPECT_EQ(object.transform.position, (glm::vec3{100, 200, 400}));
        EXPECT_EQ(object.transform.angles, (glm::vec3{10, 90, 60}));
        return Client::Scenes::PublishFaunaPose(carry, models, object, true);
    };
    resources.posedMeshes = [&] { return std::span<const Client::Render::Models::SkinnedMesh>(carry.meshes); };
    ASSERT_TRUE(
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), {}, nullptr, {}, 0, resources)
            .HasValue());
    EXPECT_EQ(batch.Vertices().size(), 3u);
    EXPECT_EQ(effect.type, 0xf4);
    EXPECT_EQ(effect.transform.position, (glm::vec3{100, 200, 300}));
    EXPECT_EQ(effect.transform.angles, (glm::vec3{10, 20, 30}));
    EXPECT_FLOAT_EQ(effect.velocity.z, 30);
    EXPECT_EQ(random.State(), expected.State());
}

TEST(GameSpecialModelEffectsTest, BloodCastleBodyUsesThePublishedPoseAndAdditiveFlagFortyTwo)
{
    TriangleModels models;
    Client::Scenes::FaunaDrawCarry carry;
    Client::Render::ModelBatch batch;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = 0x10f;
    effect.life = 10;
    effect.light = {0.5f, 0.6f, 0.7f};
    effect.vertexLight = false;
    effect.alpha = 0;
    effect.objectState.light = 0.3f;
    Client::Scenes::ModelEffectDrawResources resources{*heights};
    resources.onPose = [&](const Client::Scenes::DrawnObject& object)
    {
        EXPECT_FLOAT_EQ(effect.objectState.light, 0.3f);
        return Client::Scenes::PublishFaunaPose(carry, models, object, true);
    };
    resources.posedMeshes = [&] { return std::span<const Client::Render::Models::SkinnedMesh>(carry.meshes); };
    const auto result =
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), {}, nullptr, {}, 0, resources);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.25f);
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Additive);
    EXPECT_FLOAT_EQ(effect.objectState.light, 1);
}

TEST(GameSpecialModelEffectsTest, SpinningItemSamplesTerrainAfterPublishingItsPose)
{
    TriangleModels models;
    Client::Scenes::FaunaDrawCarry carry;
    Client::Render::ModelBatch batch;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    auto light = std::make_unique<Client::World::TerrainLighting>();
    std::int16_t itemType = 0;
    std::uint8_t itemLevel = 0;
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = 0xf4;
    models.model.meshes[0].normals[0].normal = {};
    effect.transform.position = {1000, 1000, 0};
    effect.itemType = &itemType;
    effect.itemLevel = &itemLevel;
    Client::Scenes::ModelEffectDrawResources resources{*heights};
    resources.onPose = [&](const Client::Scenes::DrawnObject& object)
    {
        light->cells.fill({1, 1, 1});
        return Client::Scenes::PublishFaunaPose(carry, models, object, true);
    };
    resources.posedMeshes = [&] { return std::span<const Client::Render::Models::SkinnedMesh>(carry.meshes); };
    ASSERT_TRUE(
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), {}, light.get(), {}, 0, resources)
            .HasValue());
    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 1.3f * 0.4f);
}

TEST(GameSpecialModelEffectsTest, FallenItemSamplesTerrainBeforePublishingItsInitializedPose)
{
    TriangleModels models;
    models.model.meshes[0].normals[0].normal = {};
    Client::Scenes::FaunaDrawCarry carry;
    Client::Render::ModelBatch batch;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    auto light = std::make_unique<Client::World::TerrainLighting>();
    std::int16_t itemType = 0;
    std::uint8_t itemLevel = 0;
    auto effect = ItemEffect(0xfa, itemType, itemLevel);
    Client::Scenes::ModelEffectDrawResources resources{*heights};
    resources.onPose = [&](const Client::Scenes::DrawnObject& object)
    {
        ExpectItemInitialization(effect, 0.8f, -1);
        ExpectItemPose(object, 0.8f);
        EXPECT_EQ(object.color, glm::vec3(0.3f));
        light->cells.fill({1, 1, 1});
        return Client::Scenes::PublishFaunaPose(carry, models, object, true);
    };
    resources.posedMeshes = [&] { return std::span<const Client::Render::Models::SkinnedMesh>(carry.meshes); };
    const auto result =
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), {}, light.get(), {}, 0, resources);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.3f * 0.4f);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.green, 0.3f * 0.4f);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.blue, 0.3f * 0.4f);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.alpha, 0.4f);
    ExpectItemInitialization(effect, 0.8f, -1);
    EXPECT_EQ(effect.type, 0xfa);
}

TEST(GameSpecialModelEffectsTest, ItemInitializationIsDurableAndCapturedAlphaControlsDrawing)
{
    for (const std::int16_t type : std::array<std::int16_t, 2>{0xf4, 0xfa})
    {
        for (const float alpha : {0.4f, 0.0f})
        {
            SCOPED_TRACE(type);
            SCOPED_TRACE(alpha);
            TriangleModels models;
            models.model.meshes[0].normals[0].normal = {};
            Client::Scenes::FaunaDrawCarry carry;
            Client::Render::ModelBatch batch;
            auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
            std::int16_t itemType = 0;
            std::uint8_t itemLevel = 0;
            auto effect = ItemEffect(type, itemType, itemLevel);
            effect.alpha = alpha;
            const float direction = type == 0xfa ? -1.0f : 0.0f;
            int poses = 0;
            Client::Scenes::ModelEffectDrawResources resources{*heights};
            resources.onPose = [&](const Client::Scenes::DrawnObject& object)
            {
                ++poses;
                ExpectItemInitialization(effect, 0.8f, direction);
                ExpectItemPose(object, 0.8f);
                EXPECT_EQ(object.state, Client::Render::Models::ObjectState{});
                return Client::Scenes::PublishFaunaPose(carry, models, object, true);
            };
            resources.posedMeshes = [&] { return std::span<const Client::Render::Models::SkinnedMesh>(carry.meshes); };
            const auto result = Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), {}, nullptr,
                                                                     {}, 0, resources);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(poses, 1);
            ExpectItemInitialization(effect, 0.8f, direction);
            EXPECT_EQ(effect.type, type);
            EXPECT_EQ(effect.transform.position, (glm::vec3{1000, 1000, 300}));
            EXPECT_EQ(effect.transform.angles, glm::vec3(0));
            EXPECT_FLOAT_EQ(effect.velocity.z, type == 0xf4 ? -30.0f : 0.0f);
            if (alpha == 0)
            {
                EXPECT_TRUE(batch.Vertices().empty());
                continue;
            }
            ASSERT_EQ(batch.Vertices().size(), 3u);
            EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.3f * 0.4f);
            EXPECT_FLOAT_EQ(batch.Vertices()[0].color.green, 0.3f * 0.4f);
            EXPECT_FLOAT_EQ(batch.Vertices()[0].color.blue, 0.3f * 0.4f);
            EXPECT_FLOAT_EQ(batch.Vertices()[0].color.alpha, alpha);
        }
    }
}

TEST(GameSpecialModelEffectsTest, PoseFailureRetainsInitializationAndCallerSpecificDirectionAndTransform)
{
    constexpr std::array<std::uint16_t, 9> Models{0x212, 0x811, 0x812, 0xe11, 0xe12, 0x1a15, 0x1e13, 0x1e27, 0x2012};
    constexpr std::array<float, Models.size()> Scales{0.8f, 0.8f, 0.7f, 0.7f, 0.8f, 0.5f, 1, 0.5f, 0.7f};
    for (const std::int16_t type : std::array<std::int16_t, 2>{0xf4, 0xfa})
    {
        for (std::size_t index = 0; index < Models.size(); ++index)
        {
            SCOPED_TRACE(type);
            SCOPED_TRACE(Models[index]);
            TriangleModels models;
            Client::Render::ModelBatch batch;
            auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
            std::int16_t itemType = static_cast<std::int16_t>(Models[index] - 0x212);
            std::uint8_t itemLevel = 0;
            auto effect = ItemEffect(type, itemType, itemLevel);
            const float direction = type == 0xfa ? -1.0f : 0.0f;
            Client::Scenes::ModelEffectDrawResources resources{*heights};
            resources.onPose =
                [&](const Client::Scenes::DrawnObject& object) -> Client::Core::Expected<void, std::string>
            {
                ExpectItemInitialization(effect, Scales[index], direction);
                ExpectItemPose(object, Scales[index]);
                EXPECT_EQ(effect.type, Models[index]);
                return Client::Core::Unexpected(std::string("pose detenida"));
            };
            const auto result = Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), {}, nullptr,
                                                                     {}, 0, resources);
            ASSERT_FALSE(result.HasValue());
            EXPECT_EQ(result.Error(), "pose detenida");
            ExpectItemInitialization(effect, Scales[index], direction);
            EXPECT_EQ(effect.type, Models[index]);
            EXPECT_FLOAT_EQ(effect.transform.position.z, type == 0xf4 ? 400.0f : 300.0f);
            EXPECT_FLOAT_EQ(effect.transform.angles.y, type == 0xf4 ? 90.0f : 0.0f);
            EXPECT_FLOAT_EQ(effect.velocity.z, type == 0xf4 ? -30.0f : 0.0f);
            EXPECT_TRUE(batch.Vertices().empty());
        }
    }
}

TEST(GameSpecialModelEffectsTest, MissingCrtRetainsTheCommonInitializationAndPreRandomBlendStore)
{
    for (const std::int16_t type : std::array<std::int16_t, 2>{0xf4, 0xfa})
    {
        for (const std::uint16_t model : std::array<std::uint16_t, 4>{0x220, 0xc19, 0xc17, 0xe20})
        {
            SCOPED_TRACE(type);
            SCOPED_TRACE(model);
            TriangleModels models;
            Client::Render::ModelBatch batch;
            std::int16_t itemType = static_cast<std::int16_t>(model - 0x212);
            std::uint8_t itemLevel = 0;
            auto effect = ItemEffect(type, itemType, itemLevel);
            const auto result =
                Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), {}, nullptr, {}, 0);
            ASSERT_FALSE(result.HasValue());
            EXPECT_NE(result.Error().find("0x607b60"), std::string::npos);
            ExpectItemInitialization(effect, model >= 0x812 && model < 0xe12 ? 0.7f : 0.8f, 0);
            EXPECT_EQ(effect.type, model);
            EXPECT_EQ(effect.objectState.blendMesh, model == 0xc17 ? 2 : 1);
            EXPECT_EQ(effect.objectState.hiddenMesh, Client::Render::Models::NoHiddenMesh);
            EXPECT_FLOAT_EQ(effect.objectState.light, 1);
            EXPECT_EQ(effect.objectState.uv, glm::vec2(0));
            EXPECT_FLOAT_EQ(effect.transform.position.z, type == 0xf4 ? 400.0f : 300.0f);
            EXPECT_TRUE(batch.Vertices().empty());
        }
    }
}

TEST(GameSpecialModelEffectsTest, FallenGlowStoresItsNativeAlphaWhileDrawingWithTheCapturedAlpha)
{
    TriangleModels models;
    Client::Scenes::FaunaDrawCarry carry;
    Client::Render::ModelBatch batch;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    std::int16_t itemType = 0x1e23 - 0x212;
    std::uint8_t itemLevel = 0;
    auto effect = ItemEffect(0xfa, itemType, itemLevel);
    Client::Scenes::ModelEffectDrawResources resources{*heights};
    resources.onPose = [&](const Client::Scenes::DrawnObject& object)
    {
        ExpectItemInitialization(effect, 0.8f, -1);
        ExpectItemPose(object, 0.8f);
        return Client::Scenes::PublishFaunaPose(carry, models, object, true);
    };
    resources.posedMeshes = [&] { return std::span<const Client::Render::Models::SkinnedMesh>(carry.meshes); };
    const auto result =
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), {}, nullptr, {}, 0, resources);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.alpha, 0.4f);
    EXPECT_FLOAT_EQ(effect.alpha, 2);
    EXPECT_EQ(effect.objectState.blendMesh, 1);
    EXPECT_FLOAT_EQ(effect.objectState.light, 15.65f);
    EXPECT_EQ(effect.type, 0xfa);
}

TEST(GameSpecialModelEffectsTest, FallenGlowRetainsItsNativeAlphaWhenTheSecondMeshHasNoTexture)
{
    TriangleModels models;
    models.model.meshes.push_back(models.model.meshes[0]);
    Client::Scenes::FaunaDrawCarry carry;
    Client::Render::ModelBatch batch;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    std::int16_t itemType = 0x1e23 - 0x212;
    std::uint8_t itemLevel = 0;
    auto effect = ItemEffect(0xfa, itemType, itemLevel);
    effect.alpha = 0.3f;
    Client::Scenes::ModelEffectDrawResources resources{*heights};
    resources.onPose = [&](const Client::Scenes::DrawnObject& object)
    {
        ExpectItemInitialization(effect, 0.8f, -1);
        ExpectItemPose(object, 0.8f);
        return Client::Scenes::PublishFaunaPose(carry, models, object, true);
    };
    resources.posedMeshes = [&]
    {
        EXPECT_FLOAT_EQ(effect.alpha, 1);
        EXPECT_EQ(effect.light, glm::vec3(0.3f));
        return std::span<const Client::Render::Models::SkinnedMesh>(carry.meshes);
    };
    const auto result =
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), {}, nullptr, {}, 0, resources);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("4ecc30: falta la textura"), std::string::npos);
    EXPECT_FLOAT_EQ(effect.alpha, 2);
    EXPECT_EQ(effect.objectState.blendMesh, 1);
    EXPECT_FLOAT_EQ(effect.objectState.light, 15.65f);
    EXPECT_EQ(effect.light, glm::vec3(15.65f * 0.2f));
    EXPECT_EQ(effect.type, 0x1e23);
    ASSERT_EQ(batch.Vertices().size(), 3u);
    for (const auto& vertex : batch.Vertices())
        EXPECT_FLOAT_EQ(vertex.color.alpha, 0.3f);
}

TEST(GameSpecialModelEffectsTest, FallenWingPublishesItsRenderItemLightBackToTheEffect)
{
    TriangleModels models;
    Client::Scenes::FaunaDrawCarry carry;
    Client::Render::ModelBatch batch;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    std::int16_t itemType = 0x1a16 - 0x212;
    std::uint8_t itemLevel = 0;
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = 0xfa;
    effect.life = 11;
    effect.itemType = &itemType;
    effect.itemLevel = &itemLevel;
    Client::Scenes::ModelEffectDrawResources resources{*heights};
    resources.onPose = [&](const Client::Scenes::DrawnObject& object)
    { return Client::Scenes::PublishFaunaPose(carry, models, object, true); };
    resources.posedMeshes = [&] { return std::span<const Client::Render::Models::SkinnedMesh>(carry.meshes); };
    const auto result =
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), {}, nullptr, {}, 0, resources);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FLOAT_EQ(effect.objectState.light, 1.1f);
    EXPECT_EQ(effect.type, 0xfa);
}
