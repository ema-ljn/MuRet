#include "Crt/CrtRandom.h"
#include "Models/IModelLibrary.h"
#include "Effects/ModelEffectDrawing.h"
#include "Particles/SpriteDrawing.h"
#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Items/GameFloorItems.h"
#include "Render/TextureSlot.h"
#include "Render/Pass3D.h"
#include "Kernel/JointRequest.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Kernel/DrawTailState.h"
#include "Scenes/GameDrawTail.h"
#include "Joints/JointDrawing.h"
#include "Scenes/GameProjection.h"
#include "Effects/GameParticles.h"
#include "Joints/FaunaJointCreation.h"
#include "Fauna/FaunaObject.h"
#include "Actors/Drawing/PosePublishing.h"
#include "Kernel/PoseBuffer.h"
#include "Models/Skinning.h"
#include "Particles/Particles.h"
#include "Render/Billboard.h"
#include "Weather/Leaves.h"
#include "Particles/Sprites.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Effects/ModelEffects.h"
#include "Effects/WizardModelEffects.h"
#include "Effects/GroundEffectDrawing.h"
#include "Terrain/SphereFrustum.h"
#include "Kernel/WorldObject.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <limits>
#include <memory>
#include <optional>
#include <vector>
#include <glm/ext/vector_int2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <string>
#include <glm/ext/matrix_float4x4.hpp>

namespace
{
class LookupCounter final : public Client::Models::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        ++lookups;
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
    mutable int lookups = 0;
};
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
        mesh.textureName = "plain.jpg";
        model.meshes = {mesh};
        Client::Models::SkeletonBone bone;
        bone.actions = {{Client::Models::BoneKey{}}};
        skeleton.bones = {bone};
        skeleton.actions = {Client::Models::ActionInfo{.keyCount = 1}};
    }
    const Client::Data::Models::Model* Find(Client::Models::ModelId id) const override
    {
        return missingModel == id.Value() ? nullptr : &model;
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
    std::optional<std::uint16_t> missingModel;
};
class TwoMeshModels final : public Client::Models::IModelLibrary
{
public:
    TwoMeshModels()
    {
        for (std::int16_t texture = 0; texture < 2; ++texture)
        {
            Client::Data::Models::Mesh mesh;
            mesh.texture = texture;
            mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
            mesh.normals = {{0, {0, 0, 1}, 0}};
            mesh.texCoords = {{0, 0}};
            mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
            mesh.textureName = "plain.jpg";
            model.meshes.push_back(mesh);
        }
        Client::Models::SkeletonBone bone;
        bone.actions = {{Client::Models::BoneKey{}}};
        skeleton.bones = {bone};
        skeleton.actions = {Client::Models::ActionInfo{.keyCount = 1}};
    }
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
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
    std::array<Client::Render::TextureSlot, 2> textures{Client::Render::TextureSlot{0x400},
                                                        Client::Render::TextureSlot{0x401}};
};
class RetainedDummyModels final : public Client::Models::IModelLibrary
{
public:
    RetainedDummyModels()
    {
        producer.skeleton.bones[0].actions[0][0].position = {7, 8, 9};
        consumer.skeleton.bones[0].dummy = true;
    }
    const Client::Data::Models::Model* Find(Client::Models::ModelId type) const override
    {
        return Selected(type.Value()).Find(type);
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId type) const override
    {
        return Selected(type.Value()).SkeletonOf(type);
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId type) const override
    {
        return Selected(type.Value()).MeshTextures(type);
    }

private:
    const TriangleModels& Selected(std::uint16_t type) const
    {
        return type == 0x212 || type == 0xc2 ? producer : consumer;
    }
    TriangleModels producer;
    TriangleModels consumer;
};
} // namespace

// 0x52da84
TEST(GameDrawTailTest, GroundOnlyEffectsPublishVisibilityWithoutLookingForModels)
{
    Client::Render::ModelBatch batch;
    LookupCounter models;
    std::array<Client::Effects::ModelEffect, 3> effects{};
    for (std::size_t i = 0; i < effects.size(); ++i)
    {
        effects[i].alive = true;
        effects[i].type = static_cast<std::int16_t>(0x61d + i);
    }
    Client::Effects::DrawModelEffects(batch, models, effects, Client::Terrain::SphereFrustum{}, nullptr, {}, 0.0f);
    EXPECT_EQ(models.lookups, 0);
    for (const auto& effect : effects)
        EXPECT_TRUE(effect.seen);
    EXPECT_TRUE(batch.Vertices().empty());
}

TEST(GameDrawTailTest, LorenciaLeavesIgnoreScaleAndPublishTheEmptyPassBlend)
{
    Client::Render::ModelBatch batch;
    Client::Weather::Leaves leaves;
    const auto view = Client::Render::ViewRows(glm::mat4(1.0f));
    ASSERT_TRUE(Client::Scenes::DrawGameLeaves(batch, leaves, 0, view).HasValue());
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Alpha);
    ASSERT_FALSE(batch.Commands().empty());
    EXPECT_EQ(batch.Commands().size(), 5u);
    auto& leaf = leaves.All()[0];
    leaf.alive = true;
    leaf.type = 100;
    leaf.scale = 99.0f;
    ASSERT_TRUE(Client::Scenes::DrawGameLeaves(batch, leaves, 0, view).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, -3.0f);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.z, -3.0f);
}

TEST(GameDrawTailTest, DeviasLeavesUseTheWorldViewAndTheirOwnLight)
{
    Client::Render::ModelBatch batch;
    Client::Weather::Leaves leaves;
    auto& leaf = leaves.All()[0];
    leaf.alive = true;
    leaf.scale = 8;
    leaf.light = {0.2f, 0.3f, 0.4f};
    auto view = Client::Render::ViewRows(glm::mat4(1.0f));
    view[3] = 20;
    ASSERT_TRUE(Client::Scenes::DrawGameLeaves(batch, leaves, 2, view).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 16);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.2f);
    // The 0x621400 of 0x5306a0 does not push an identity: it keeps the caller's modelview.
    EXPECT_EQ(batch.State().space, Client::Render::ModelCoordinateSpace::World);
}

TEST(GameDrawTailTest, RibbonsChangeBlendBeforeCheckingTheirPointCount)
{
    Client::Render::ModelBatch batch;
    Client::Kernel::RibbonState ribbon;
    ribbon.alive = true;
    std::uint16_t equipment = 0;
    ribbon.ownerEquipment = &equipment;
    ribbon.type = 3;
    ASSERT_TRUE(Client::Scenes::DrawRibbons(batch, std::span(&ribbon, 1)).HasValue());
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Additive);
    EXPECT_FALSE(batch.Commands().empty());
    EXPECT_TRUE(batch.Vertices().empty());
    ribbon.count = 2;
    ribbon.light = {1, 1, 1};
    ribbon.first[0] = {1, 2, 3};
    ribbon.second[0] = {4, 5, 6};
    ribbon.second[1] = {7, 8, 9};
    ribbon.first[1] = {10, 11, 12};
    ASSERT_TRUE(Client::Scenes::DrawRibbons(batch, std::span(&ribbon, 1)).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_EQ(batch.State().texture->value, 0x639);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 1);
    EXPECT_FLOAT_EQ(batch.Vertices()[2].color.red, 0.5f);
    EXPECT_TRUE(ribbon.alive);
}

TEST(GameDrawTailTest, MissingRibbonOwnerFailsBeforeChangingBlend)
{
    Client::Render::ModelBatch batch;
    Client::Kernel::RibbonState ribbon;
    ribbon.alive = true;
    EXPECT_FALSE(Client::Scenes::DrawRibbons(batch, std::span(&ribbon, 1)).HasValue());
    EXPECT_TRUE(batch.Commands().empty());
}

TEST(GameDrawTailTest, DamageKeepsItsUnconditionalStateAndReportsItsLiveFrontier)
{
    Client::Render::ModelBatch batch;
    Client::Kernel::DamageNumberState damage;
    ASSERT_TRUE(Client::Scenes::DrawDamage(batch, std::span(&damage, 1), 0).HasValue());
    EXPECT_FALSE(batch.State().depthTest);
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Alpha);
    damage.alive = true;
    damage.position.z = 351;
    EXPECT_TRUE(Client::Scenes::DrawDamage(batch, std::span(&damage, 1), 1).HasValue());
    EXPECT_FALSE(Client::Scenes::DrawDamage(batch, std::span(&damage, 1), 0).HasValue());
    EXPECT_TRUE(damage.alive);
}

TEST(GameDrawTailTest, DamageDigitsUseTheAtlasAndKeepTheirSlotsAlive)
{
    Client::Render::ModelBatch batch;
    Client::Kernel::DamageNumberState damage;
    damage.alive = true;
    damage.number = 12;
    damage.scale = 16;
    damage.speed = 2;
    damage.light = {1, 0.5f, 0.25f};
    const auto view = Client::Render::ViewRows(glm::mat4(1.0f));
    ASSERT_TRUE(Client::Scenes::DrawDamage(batch, std::span(&damage, 1), 0, view).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 12u);
    EXPECT_EQ(batch.State().texture->value, 1);
    EXPECT_FALSE(batch.State().depthTest);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].uv.x, 0.0625f);
    EXPECT_FLOAT_EQ(batch.Vertices()[6].uv.x, 0.125f);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.alpha, 0.8f);
    EXPECT_TRUE(damage.alive);
}

TEST(GameDrawTailTest, DamageStepDelaysMotionUntilTheTimerIsNegative)
{
    Client::Kernel::DamageNumberState damage;
    damage.alive = true;
    damage.timer = 1;
    damage.rise = true;
    damage.speed = 10;
    damage.scale = 50;
    Client::Scenes::StepDamage(std::span(&damage, 1));
    EXPECT_FLOAT_EQ(damage.position.z, 0);
    EXPECT_EQ(damage.timer, 0);
    Client::Scenes::StepDamage(std::span(&damage, 1));
    EXPECT_FLOAT_EQ(damage.position.z, 10);
    EXPECT_FLOAT_EQ(damage.speed, 9.7f);
    EXPECT_FLOAT_EQ(damage.scale, 45);
    damage.speed = 0.1f;
    damage.scale = 16;
    Client::Scenes::StepDamage(std::span(&damage, 1));
    EXPECT_FALSE(damage.alive);
    EXPECT_FLOAT_EQ(damage.scale, 15);
}

TEST(GameDrawTailTest, CommonHorseJointsDrawTheirSelectedFacesWithSegmentUvs)
{
    Client::Render::ModelBatch batch;
    Client::Kernel::JointState joint;
    joint.alive = true;
    joint.count = 1;
    joint.maximumCount = 3;
    joint.drawEnabled = 3;
    joint.blend = 1;
    joint.type = 0x60f;
    joint.texture = 0x60f;
    joint.light = {0.5f, 0.6f, 0.7f};
    joint.points[0][2] = {1, 2, 3};
    joint.points[0][3] = {4, 5, 6};
    joint.points[1][3] = {7, 8, 9};
    joint.points[1][2] = {10, 11, 12};
    ASSERT_TRUE(Client::Joints::DrawJoints(batch, std::span(&joint, 1), 0).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 12u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 1);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].uv.x, 0.5f);
    EXPECT_FLOAT_EQ(batch.Vertices()[2].uv.x, 0);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.5f);
    EXPECT_TRUE(joint.alive);
}

TEST(GameDrawTailTest, FirstSpritePassRetainsSpritesAndSecondPassKillsItsLowerBand)
{
    Client::Render::ModelBatch batch;
    LookupCounter models;
    Client::Particles::FrameSprites sprites;
    ASSERT_TRUE(sprites.Create(0x5ac, {0, 0, 325}, 1, {1, 1, 1}, 0, 0));
    ASSERT_TRUE(sprites.Create(0x5ac, {0, 0, 300}, 1, {1, 1, 1}, 0, 0));
    ASSERT_TRUE(sprites.Create(0x5ac, {0, 0, std::numeric_limits<float>::quiet_NaN()}, 1, {1, 1, 1}, 0, 0));
    const auto view = Client::Render::ViewRows(glm::mat4(1.0f));
    Client::Particles::DrawFrameSprites(batch, sprites, models, view, 1);
    EXPECT_TRUE(sprites.Sprites()[0].alive);
    EXPECT_TRUE(sprites.Sprites()[1].alive);
    EXPECT_TRUE(sprites.Sprites()[2].alive);
    Client::Particles::DrawFrameSprites(batch, sprites, models, view, 2);
    EXPECT_FALSE(sprites.Any());
}

TEST(GameDrawTailTest, SpriteBlendThreeUsesTheInverseColorAdditiveProfile)
{
    Client::Render::ModelBatch batch;
    LookupCounter models;
    Client::Particles::FrameSprites sprites;
    ASSERT_TRUE(sprites.Create(0x5ac, {0, 0, 0}, 1, {1, 1, 1}, 0, 3));
    Client::Particles::DrawFrameSprites(batch, sprites, models, Client::Render::ViewRows(glm::mat4(1.0f)));
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::InverseColorAdditive);
}

TEST(GameDrawTailTest, PlainFloorSwordsRenderBeforePublishingTheirProjectedShorts)
{
    Client::Render::ModelBatch batch;
    TriangleModels models;
    Client::Terrain::SphereFrustum frustum;
    Client::Kernel::FloorItemState item;
    item.alive = true;
    item.type = 0x212;
    item.scale = 1;
    item.alpha = 1;
    item.light = {1, 1, 1};
    bool projectedAfterGeometry = false;
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.project = [&batch,
                       &projectedAfterGeometry](const glm::vec3& p) -> Client::Core::Expected<glm::ivec2, std::string>
    {
        projectedAfterGeometry = batch.Vertices().size() == 3 && p.z == 30;
        return glm::ivec2(123, -2);
    };
    ASSERT_TRUE(Client::Scenes::DrawFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_TRUE(projectedAfterGeometry);
    EXPECT_TRUE(item.seen);
    EXPECT_EQ(item.screenX, 123);
    EXPECT_EQ(item.screenY, -2);
    EXPECT_FLOAT_EQ(item.position.z, 0);
}

TEST(GameDrawTailTest, ProjectionTruncatesBeforeAddingTheCenterAndScalingToVirtualPixels)
{
    Client::Kernel::GameProjection projection;
    projection.view = Client::Render::ViewRows(glm::mat4(1.0f));
    projection.horizontalScale = 0.5f;
    projection.verticalScale = 0.25f;
    projection.centerX = 512;
    projection.centerY = 384;
    projection.screenWidth = 1024;
    projection.screenHeight = 768;
    const auto p = Client::Kernel::ProjectGamePoint(projection, {1.9f, 2.9f, -2});
    ASSERT_TRUE(p.HasValue());
    EXPECT_EQ(p.Value().x, 320); // (512 - trunc(-1.9)) *640/1024
    EXPECT_EQ(p.Value().y, 236); // (384 + trunc(-5.8)) *480/768
}

TEST(GameDrawTailTest, ParticleModesFilterBeforeLookingUpUnsupportedResources)
{
    Client::Render::ModelBatch batch;
    LookupCounter models;
    Client::Particles::Particles particles;
    particles.All()[0].alive = true;
    particles.All()[0].type = -1;
    particles.All()[0].position.z = 351;
    const auto view = Client::Render::ViewRows(glm::mat4(1.0f));
    EXPECT_TRUE(Client::Effects::DrawGameParticles(batch, particles, models, view, 0, 1).HasValue());
    EXPECT_FALSE(Client::Effects::DrawGameParticles(batch, particles, models, view, 0, 2).HasValue());
    EXPECT_TRUE(particles.All()[0].alive);
}

// Fixtures computed separately with Decimal120, tan→R64 and PC53/F32 stores, without running the original.
TEST(GameDrawTailTest, ProjectionPreparationMatchesTheThreeReferenceScreensAtThirtyFiveDegrees)
{
    struct Case
    {
        int width;
        int height;
        int viewportHeight;
        std::uint32_t sx;
        std::uint32_t sy;
    };
    constexpr std::array cases{Case{640, 480, 432, 0x3abf53f3u, 0x3ad4962bu},
                               Case{1024, 768, 691, 0x3a6f3aa8u, 0x3a8522dfu},
                               Case{1920, 1080, 972, 0x3a2a11bcu, 0x3a3cf742u}};
    for (const auto& sample : cases)
    {
        Client::Render::Pass3D pass;
        pass.screen = {sample.width, sample.height};
        pass.viewport = {0, sample.height - sample.viewportHeight, sample.width, sample.viewportHeight};
        const auto result = Client::Kernel::MakeGameProjection(pass, 35);
        ASSERT_TRUE(result.HasValue());
        EXPECT_EQ(std::bit_cast<std::uint32_t>(result.Value().horizontalScale), sample.sx);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(result.Value().verticalScale), sample.sy);
    }
}

TEST(GameDrawTailTest, FaunaJointCreationRetainsOldPointsAndCountsItsTwoRandoms)
{
    std::array<Client::Kernel::JointState, 1> joints{};
    Client::Fauna::FaunaObject owner;
    owner.active = 1;
    owner.position = {10, 20, 30};
    owner.light = {0.1f, 0.2f, 0.3f};
    glm::vec3 first{1, 2, 3}, second{4, 5, 6}, angles{};
    Client::Kernel::JointRequest request{.type = 0x697,
                                         .firstPosition = first,
                                         .secondPosition = second,
                                         .angles = angles,
                                         .subtype = 0,
                                         .ownerContext = &owner,
                                         .scale = 2,
                                         .key = -1,
                                         .skill = 0,
                                         .skillSerial = 0,
                                         .target = -1};
    Client::Crt::CrtRandom random(7), expected(7);
    joints[0].points[0][0] = {99, 98, 97};
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, random).HasValue());
    EXPECT_EQ(joints[0].life, 120);
    EXPECT_EQ(joints[0].count, -1);
    EXPECT_FLOAT_EQ(joints[0].points[0][0].x, 99);
    EXPECT_EQ(random.State(), expected.State());
    joints[0].alive = false;
    request.type = 0x10f;
    request.subtype = 3;
    const float sway = static_cast<float>(expected.Next() % 500);
    const float speed = static_cast<float>(expected.Next() % 5 + 5);
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, random).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(joints[0].texture, 0x613);
    EXPECT_EQ(joints[0].life, 999999);
    EXPECT_FLOAT_EQ(joints[0].velocity.y, sway);
    EXPECT_FLOAT_EQ(joints[0].speed, speed);
    EXPECT_FLOAT_EQ(joints[0].light.y, owner.light.y);
    EXPECT_FALSE(Client::Joints::StepJoints(joints).HasValue());
    request.ownerContext = nullptr;
    const auto full = Client::Joints::CreateFaunaJoint(joints, request, random);
    ASSERT_TRUE(full.HasValue());
    EXPECT_FALSE(full.Value().has_value());
    EXPECT_EQ(random.State(), expected.State());
}

// 0x530a2a and 0x5381cb: the 0x697's creation and step do not read the subtype; the only caller (0x4473f9) asks for
// /0, but another subtype is born and follows its owner the same way, and with the owner inactive it dies without the
// tail or the life.
TEST(GameDrawTailTest, ScrollTailOfAnotherSubtypeIsTheSame)
{
    std::array<Client::Kernel::JointState, 1> joints{};
    Client::Fauna::FaunaObject owner;
    owner.active = 1;
    owner.previousPosition = {10, 20, 30};
    glm::vec3 first{1, 2, 3}, second{4, 5, 6}, angles{};
    const Client::Kernel::JointRequest request{.type = 0x697,
                                               .firstPosition = first,
                                               .secondPosition = second,
                                               .angles = angles,
                                               .subtype = 5,
                                               .ownerContext = &owner,
                                               .scale = 30,
                                               .key = -1,
                                               .skill = 0,
                                               .skillSerial = 0,
                                               .target = -1};
    Client::Crt::CrtRandom random(7);
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, random).HasValue());
    auto& joint = joints[0];
    EXPECT_EQ(joint.subtype, 5);
    EXPECT_EQ(joint.life, 120);
    EXPECT_EQ(joint.count, -1);
    EXPECT_EQ(joint.maximumCount, 28);
    EXPECT_EQ(joint.speed, 3.0f);
    EXPECT_EQ(joint.width, 30.0f);
    ASSERT_TRUE(Client::Joints::StepJoints(joints).HasValue());
    EXPECT_EQ(joint.position, owner.previousPosition);
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.life, 119);
    owner.active = 0;
    owner.previousPosition = {40, 50, 60};
    ASSERT_TRUE(Client::Joints::StepJoints(joints).HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.position, owner.previousPosition);
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.life, 119);
}

TEST(GameDrawTailTest, CrywolfJointCopiesItsOwnerTrailBeforeGrowingAndExpiring)
{
    std::array<Client::Kernel::JointState, 1> joints{};
    Client::Fauna::FaunaObject owner;
    owner.active = 1;
    owner.previousPosition = {10, 20, 30};
    auto& joint = joints[0];
    joint.alive = true;
    joint.type = 0x697;
    joint.ownerContext = &owner;
    joint.tailEnabled = true;
    joint.count = -1;
    joint.maximumCount = 28;
    joint.life = 2;
    joint.width = 4;
    ASSERT_TRUE(Client::Joints::StepJoints(joints).HasValue());
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.life, 1);
    EXPECT_FLOAT_EQ(joint.position.y, 20);
    EXPECT_FLOAT_EQ(joint.points[0][0].x, 8);
    owner.previousPosition = {40, 50, 60};
    ASSERT_TRUE(Client::Joints::StepJoints(joints).HasValue());
    EXPECT_EQ(joint.count, 1);
    EXPECT_FLOAT_EQ(joint.points[1][0].x, 8);
    EXPECT_FLOAT_EQ(joint.points[0][0].x, 38);
    EXPECT_TRUE(joint.alive); // life 0 stays alive; it dies when it goes negative.
    ASSERT_TRUE(Client::Joints::StepJoints(joints).HasValue());
    EXPECT_FALSE(joint.alive);
}

TEST(GameDrawTailTest, TarkanJointChecksItsOwnerBeforeCopyingTheTrail)
{
    std::array<Client::Kernel::JointState, 1> joints{};
    Client::Fauna::FaunaObject owner;
    owner.active = 1;
    owner.previousPosition = {12, 22, 32};
    auto& joint = joints[0];
    joint.alive = true;
    joint.type = 0x618;
    joint.subtype = 4;
    joint.ownerContext = &owner;
    joint.tailEnabled = true;
    joint.count = 0;
    joint.maximumCount = 20;
    joint.life = 10;
    joint.width = 2;
    ASSERT_TRUE(Client::Joints::StepJoints(joints).HasValue());
    EXPECT_FLOAT_EQ(joint.position.x, 12);
    EXPECT_EQ(joint.count, 1);
    EXPECT_EQ(joint.life, 9);
    owner.active = 0;
    owner.previousPosition = {90, 90, 90};
    ASSERT_TRUE(Client::Joints::StepJoints(joints).HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_FLOAT_EQ(joint.position.x, 12);
    EXPECT_EQ(joint.life, 9);
}

TEST(GameDrawTailTest, FirstDeviasSnowPassKeepsTheActiveWorldModelview)
{
    Client::Render::ModelBatch batch;
    Client::Render::Pass3D world;
    world.view[3][0] = 20;
    batch.Begin(world);
    Client::Weather::Leaves leaves;
    auto& leaf = leaves.All()[0];
    leaf.alive = true;
    leaf.scale = 8;
    const auto view = Client::Render::ViewRows(world.view);
    ASSERT_TRUE(Client::Scenes::DrawGameLeaves(batch, leaves, 2, view).HasValue());
    EXPECT_EQ(batch.State().space, Client::Render::ModelCoordinateSpace::World);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 16);
    const auto transformed = world.view * glm::vec4(batch.Vertices()[0].position, 1);
    EXPECT_FLOAT_EQ(transformed.x, 36); // 0x621400 does no LoadIdentity; the first pass applies view again.
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Client::Scenes::DrawGameLeaves(batch, leaves, 2, view).HasValue());
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 16);
}

TEST(GameDrawTailTest, DamageUnorderedScaleFallsBackToFifteenLikeTheX87Comparison)
{
    Client::Kernel::DamageNumberState damage;
    damage.alive = true;
    damage.scale = std::numeric_limits<float>::quiet_NaN();
    damage.speed = 10;
    Client::Scenes::StepDamage(std::span(&damage, 1));
    EXPECT_FLOAT_EQ(damage.scale, 15);
}

TEST(GameDrawTailTest, IcarusJointOwnerColorFailureKeepsTheEarlierConstructorPrefix)
{
    std::array<Client::Kernel::JointState, 1> joints{};
    glm::vec3 first{}, second{}, angles{};
    Client::Kernel::JointRequest request{.type = 0x610,
                                         .firstPosition = first,
                                         .secondPosition = second,
                                         .angles = angles,
                                         .subtype = 8,
                                         .ownerContext = nullptr,
                                         .scale = 77,
                                         .key = 7,
                                         .skill = 3,
                                         .skillSerial = 0,
                                         .target = -1};
    Client::Crt::CrtRandom random(1);
    const auto created = Client::Joints::CreateFaunaJoint(joints, request, random);
    ASSERT_FALSE(created.HasValue());
    EXPECT_NE(created.Error().find("0x531c7b"), std::string::npos);
    EXPECT_TRUE(joints[0].alive);
    EXPECT_EQ(joints[0].life, 7);
    EXPECT_EQ(joints[0].maximumCount, 50);
    EXPECT_EQ(joints[0].blend, 1);
    EXPECT_EQ(joints[0].drawEnabled, 3);
    EXPECT_FLOAT_EQ(joints[0].width, 20);
    EXPECT_EQ(random.State(), 1u);
}

TEST(GameDrawTailTest, BloodCastleJointRendersItsFadingAndUnfadedSubtypes)
{
    Client::Render::ModelBatch batch;
    Client::Kernel::JointState joint;
    joint.alive = true;
    joint.count = 1;
    joint.maximumCount = 30;
    joint.drawEnabled = 1;
    joint.blend = 1;
    joint.type = 0x10f;
    joint.subtype = 1;
    joint.texture = 0x613;
    joint.light = {0.2f, 0.4f, 0.8f};
    joint.life = 10;
    ASSERT_TRUE(Client::Joints::DrawJoints(batch, std::span(&joint, 1), 0).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.1f);
    batch.Begin(Client::Render::Pass3D{});
    joint.subtype = 3;
    ASSERT_TRUE(Client::Joints::DrawJoints(batch, std::span(&joint, 1), 0).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.2f);
}

TEST(GameDrawTailTest, IcarusJointEmitsTwoRotatedSpritesBeforeDrawingItsFirstFace)
{
    Client::Render::ModelBatch batch;
    Client::Kernel::JointState joint;
    joint.alive = true;
    joint.type = 0x610;
    joint.subtype = 8;
    joint.count = 1;
    joint.maximumCount = 20;
    joint.drawEnabled = 2;
    joint.blend = 4;
    joint.texture = 0x610;
    joint.light = {0.2f, 0.4f, 0.8f};
    joint.points[0] = {{{0, 0, 0}, {4, 0, 0}, {0, 4, 0}, {0, 0, 4}}};
    Client::Particles::FrameSprites sprites;
    Client::Crt::CrtRandom random(7), expected(7);
    const auto first = expected.Next() % 360;
    const auto second = expected.Next() % 360;
    const Client::Joints::JointDrawResources resources{random, sprites};
    ASSERT_TRUE(Client::Joints::DrawJoints(batch, std::span(&joint, 1), 0, resources).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::InverseColorAdditive);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(sprites.Sprites()[0].texture, 0x62b);
    EXPECT_EQ(sprites.Sprites()[1].texture, 0x5fd);
    EXPECT_FLOAT_EQ(sprites.Sprites()[0].rotation, static_cast<float>(first));
    EXPECT_FLOAT_EQ(sprites.Sprites()[1].rotation, static_cast<float>(second));
    EXPECT_FLOAT_EQ(sprites.Sprites()[0].position.x, 1);
    EXPECT_FLOAT_EQ(sprites.Sprites()[0].scale, 0.3f);
    EXPECT_FLOAT_EQ(sprites.Sprites()[1].scale, 1);
    sprites.Clear();
    batch.Begin(Client::Render::Pass3D{});
    const Client::Joints::JointDrawResources intermediate{random, sprites, false};
    ASSERT_TRUE(Client::Joints::DrawJoints(batch, std::span(&joint, 1), 0, intermediate).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_FLOAT_EQ(sprites.Sprites()[0].rotation, static_cast<float>(first));
    EXPECT_FLOAT_EQ(sprites.Sprites()[1].rotation, static_cast<float>(second));
}

TEST(GameDrawTailTest, BloodCastleJointMissingRandomFailsAfterMovementAndOwnerCheck)
{
    Client::Fauna::FaunaObject owner;
    owner.active = 1;
    Client::Kernel::JointState joint;
    joint.alive = true;
    joint.type = 0x10f;
    joint.subtype = 3;
    joint.ownerContext = &owner;
    joint.speed = 2;
    joint.life = 10;
    const auto result = Client::Joints::StepJoints(std::span(&joint, 1));
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x535f18"), std::string::npos);
    EXPECT_FLOAT_EQ(joint.position.y, -2);
    EXPECT_EQ(joint.life, 10);
}

TEST(GameDrawTailTest, BloodCastleJointStepsItsRandomLifeBeforeTheSineAndOwnerColor)
{
    Client::Fauna::FaunaObject owner;
    owner.active = 1;
    owner.timer = 50;
    owner.light = {0.25f, 0.5f, 0.75f};
    Client::Kernel::JointState joint;
    joint.alive = true;
    joint.type = 0x10f;
    joint.subtype = 3;
    joint.ownerContext = &owner;
    joint.life = 10;
    joint.velocity.y = 2;
    joint.tailEnabled = true;
    joint.count = 0;
    joint.maximumCount = 30;
    joint.width = 2;
    joint.points[0][0] = {100, 200, 300};
    Client::Crt::CrtRandom random(7);
    ASSERT_TRUE(Client::Joints::StepJoints(std::span(&joint, 1), random).HasValue());
    EXPECT_EQ(random.State(), 3289278137u); // rand61%5 !=0; rand17422%2 ==0.
    EXPECT_EQ(joint.life, 10);              // 10+1 before the sine; -1 at the end.
    // Decimal100: sin((11-2)*F32(.05))*3 = 1.304896620447595617..., then an F32 store.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.velocity.x), 0x3fa706dau);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.position.x), 0x3fa706dau);
    EXPECT_EQ(joint.light, owner.light);
    EXPECT_EQ(joint.count, 1);
    EXPECT_FLOAT_EQ(joint.points[1][0].x, 100);
}

TEST(GameDrawTailTest, BloodCastleDeadOwnerStillAddsItsLastTailWithoutRandom)
{
    Client::Fauna::FaunaObject owner;
    Client::Kernel::JointState joint;
    joint.alive = true;
    joint.type = 0x10f;
    joint.subtype = 3;
    joint.ownerContext = &owner;
    joint.life = 10;
    joint.speed = 2;
    joint.tailEnabled = true;
    joint.count = -1;
    joint.maximumCount = 30;
    joint.width = 2;
    ASSERT_TRUE(Client::Joints::StepJoints(std::span(&joint, 1)).HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, -1);
    EXPECT_EQ(joint.count, 0);
    EXPECT_FLOAT_EQ(joint.points[0][0].x, -1);
    EXPECT_FLOAT_EQ(joint.points[0][0].y, -2);
}

TEST(GameDrawTailTest, IcarusJointCopiesEvenItsDeadOwnerAndPinsTheTailToHeightThreeHundred)
{
    Client::Fauna::FaunaObject owner;
    owner.position = {12, 34, 56};
    Client::Kernel::JointState joint;
    joint.alive = true;
    joint.type = 0x610;
    joint.subtype = 8;
    joint.ownerContext = &owner;
    joint.life = 10;
    joint.tailEnabled = true;
    joint.count = -1;
    joint.maximumCount = 20;
    joint.width = 2;
    ASSERT_TRUE(Client::Joints::StepJoints(std::span(&joint, 1)).HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, 9);
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.position, glm::vec3(12, 34, 300));
    EXPECT_EQ(joint.points[0][0], glm::vec3(11, 34, 300));
    joint.alive = true;
    joint.ownerContext = nullptr;
    joint.position.x = -0.0f;
    joint.position.y = -0.0f;
    ASSERT_TRUE(Client::Joints::StepJoints(std::span(&joint, 1)).HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, 8);
    EXPECT_EQ(joint.count, 1);
    EXPECT_TRUE(std::signbit(joint.position.x)); // 0x535932 skips the sums when speed==0.
    EXPECT_TRUE(std::signbit(joint.position.y));
}

TEST(GameDrawTailTest, GroundModelEffectsRenderBeforeRestoringOpaqueStateWithoutModelLookups)
{
    Client::Render::ModelBatch batch;
    LookupCounter models;
    Client::Terrain::SphereFrustum frustum;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    heights->cells.fill(7);
    const Client::Scenes::ModelEffectDrawResources resources{*heights, 30};
    Client::Effects::ModelEffect effect;
    effect.alive = true;
    effect.type = 0xcf;
    effect.subtype = 3;
    effect.transform.position = {1000, 2000, 3000};
    ASSERT_TRUE(
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), frustum, nullptr, {}, 0, resources)
            .HasValue());
    ASSERT_EQ(batch.Vertices().size(), 294u);
    EXPECT_EQ(batch.State().texture->value, 0x6ad);
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Additive);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.z, 12); // height 7 + lift 5; not the effect's z.
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.3f);
    batch.Begin(Client::Render::Pass3D{});
    effect.type = 0x14e;
    effect.textureFrame = 7;
    effect.light = {0.25f, 0.5f, 0.75f};
    ASSERT_TRUE(
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), frustum, nullptr, {}, 0, resources)
            .HasValue());
    ASSERT_EQ(batch.Vertices().size(), 54u);
    EXPECT_EQ(batch.State().texture->value, 0x692);
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Opaque);
    EXPECT_EQ(batch.State().cullEnabled, true);
    EXPECT_EQ(batch.State().depthWrite, true);
    EXPECT_EQ(batch.State().alphaTest, false);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.25f);
    EXPECT_EQ(models.lookups, 0);
}

// 0x52d620: the siege's 0x19f and 0x61c have no case (not even in 0x52dc20) and are not drawn; aura 0x134 goes through
// the common path (0x52da92 → 0x602e90) and looks up its BMD (Skill\Aurora).
TEST(GameDrawTailTest, TheSiegeEffectsDrawOnlyTheAuraThroughItsModel)
{
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Scenes::ModelEffectDrawResources resources{*heights, 30};
    resources.siegeActive = true;
    for (const std::int16_t type : {std::int16_t{0x134}, std::int16_t{0x19f}, std::int16_t{0x61c}})
    {
        SCOPED_TRACE(type);
        Client::Render::ModelBatch batch;
        LookupCounter models;
        Client::Effects::ModelEffect effect;
        effect.alive = true;
        effect.type = type;
        effect.transform.position = {1000, 2000, 300};
        const auto drawn = Client::Scenes::DrawGameModelEffects(
            batch, models, std::span(&effect, 1), Client::Terrain::SphereFrustum{}, nullptr, {}, 0, resources);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_TRUE(effect.seen);
        EXPECT_EQ(models.lookups > 0, type == 0x134);
        EXPECT_TRUE(batch.Vertices().empty());
    }
}

// 0x518d63 clears +0x16d and not +0x16c: the slot that reuses the prefix keeps what the last draw saw, and the ground
// pass (0x52dc46) takes its rand() even though the type has no case.
TEST(GameDrawTailTest, AReusedSlotKeepsItsVisibilityUntilTheModelPass)
{
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    effects->Effects().front().seen = true;
    const auto slot = effects->Begin({.type = 0x19f});
    ASSERT_EQ(slot, 0u);

    Client::Crt::CrtRandom random(11);
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    const Client::Effects::GroundEffectResources resources{.heights = *heights, .random = random, .map = 30};
    Client::Render::ModelBatch batch;
    const auto drawn = Client::Effects::DrawGroundEffects(batch, effects->Effects(), resources);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    Client::Crt::CrtRandom expected(11);
    static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Additive);
}

// 0x52d620: arrow 0xf8 and pulse 0x105/3 are not drawn (0x52d6c3 and 0x52d78f); fragment 0xc1a and pebbles 0xc8/0xc9
// with subtype 1 go through the common path (0x602e90) with their BMD, in both passes.
TEST(GameDrawTailTest, TheCrywolfEffectsDrawThroughTheirModelsOrNotAtAll)
{
    struct Probe
    {
        std::int16_t type;
        std::int32_t subtype;
        bool drawn;
    };
    constexpr std::array probes{Probe{0xf8, 1, false}, Probe{0x105, 3, false}, Probe{0xc1a, 0, true},
                                Probe{0xc8, 1, true}, Probe{0xc9, 1, true}};
    for (const auto [type, subtype, drawn] : probes)
    {
        SCOPED_TRACE(type);
        for (const std::uint8_t mode : {std::uint8_t{0}, std::uint8_t{1}})
        {
            LookupCounter models;
            Client::Effects::ModelEffect effect;
            effect.alive = true;
            effect.type = type;
            effect.subtype = subtype;
            effect.transform.position = {1000, 2000, 300};
            Client::Render::ModelBatch batch;
            const auto result = Client::Scenes::DrawGameModelEffects(
                batch, models, std::span(&effect, 1), Client::Terrain::SphereFrustum{}, nullptr, {}, 0, mode);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_TRUE(effect.seen);
            EXPECT_TRUE(batch.Vertices().empty());
            // In the second pass the filter (0x52d68c) looks up everyone's BMD before the case.
            if (mode == 0)
            {
                EXPECT_EQ(models.lookups > 0, drawn);
            }
        }
    }
}

// 0x52d620 with the high types of CL11: 0x116, 0x117, 0x138, 0x13c and stone 0x12e/3 go through the common path
// (0x602e90) with their skills BMD; 0x625, emitters 0x812 and 0x813 and glow 0x621/1 (from 0x151 on, without a case)
// are not drawn.
TEST(GameDrawTailTest, TheHighEffectsDrawThroughTheirModelsOrNotAtAll)
{
    struct Probe
    {
        std::int16_t type;
        std::int32_t subtype;
        bool drawn;
    };
    constexpr std::array probes{Probe{0x116, 0, true},  Probe{0x117, 0, true},  Probe{0x138, 0, true},
                                Probe{0x13c, 0, true},  Probe{0x12e, 3, true},  Probe{0x625, 0, false},
                                Probe{0x812, 0, false}, Probe{0x813, 0, false}, Probe{0x621, 1, false}};
    for (const auto [type, subtype, drawn] : probes)
    {
        SCOPED_TRACE(type);
        LookupCounter models;
        Client::Effects::ModelEffect effect;
        effect.alive = true;
        effect.type = type;
        effect.subtype = subtype;
        effect.transform.position = {1000, 2000, 300};
        Client::Render::ModelBatch batch;
        const auto result = Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1),
                                                                 Client::Terrain::SphereFrustum{}, nullptr, {}, 0, 0);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(effect.seen);
        EXPECT_TRUE(batch.Vertices().empty());
        EXPECT_EQ(models.lookups > 0, drawn);
    }
}

TEST(GameDrawTailTest, NoriaJointConsumesTwoConstructorRandomsAndMovesBeforeGrowingItsTail)
{
    std::array<Client::Kernel::JointState, 1> joints{};
    joints[0].width = 10;
    joints[0].icarusRotations = std::array<float, 2>{45, 60};
    glm::vec3 position{10, 20, 30}, angles{};
    const Client::Kernel::JointRequest request{.type = 0x617,
                                               .firstPosition = position,
                                               .secondPosition = position,
                                               .angles = angles,
                                               .subtype = 0,
                                               .ownerContext = nullptr,
                                               .scale = 10,
                                               .key = -1,
                                               .skill = 0,
                                               .skillSerial = 0,
                                               .target = -1};
    Client::Crt::CrtRandom random(7);
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, random).HasValue());
    auto& joint = joints[0];
    EXPECT_FALSE(joint.icarusRotations.has_value());
    EXPECT_EQ(random.State(), 3289278137u);
    EXPECT_FLOAT_EQ(joint.width, 2);
    EXPECT_FLOAT_EQ(joint.speed, 7);
    EXPECT_EQ(joint.life, 14);
    EXPECT_EQ(joint.maximumCount, 2);
    EXPECT_FLOAT_EQ(joint.points[0][0].x, 5);
    ASSERT_TRUE(Client::Joints::StepJoints(joints).HasValue());
    EXPECT_EQ(joint.life, 13);
    EXPECT_EQ(joint.position, glm::vec3(10, 13, 30));
    EXPECT_EQ(joint.count, 1);
    EXPECT_EQ(joint.points[0][0], glm::vec3(9, 13, 30));
    EXPECT_FLOAT_EQ(joint.points[1][0].x, 5);
    Client::Render::ModelBatch batch;
    ASSERT_TRUE(Client::Joints::DrawJoints(batch, joints, 0).HasValue());
    EXPECT_EQ(batch.Vertices().size(), 12u);
    EXPECT_EQ(batch.State().texture->value, 0x617);
    EXPECT_EQ(random.State(), 3289278137u);
}

TEST(GameDrawTailTest, FloorStepFallsBeforeClampingAndSettlesItsWeaponWithoutResettingSpeed)
{
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    auto attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(7);
    Client::Kernel::FloorItemState item;
    item.alive = true;
    item.type = 0x212;
    item.position = {100, 200, 200};
    item.dropSpeed = 5;
    item.effectCounter = 1;
    const Client::Items::FloorItemStepContext context{*heights, *attributes, random, particles, 0};
    ASSERT_TRUE(Client::Items::StepFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_FLOAT_EQ(item.position.z, 205);
    EXPECT_FLOAT_EQ(item.dropSpeed, -1);
    EXPECT_FLOAT_EQ(item.angles.y, 10);
    EXPECT_EQ(item.effectCounter, 2);
    item.position.z = 100;
    ASSERT_TRUE(Client::Items::StepFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_FLOAT_EQ(item.position.z, 99); // Still in the air: ground 0 +30 +40.
    EXPECT_FLOAT_EQ(item.dropSpeed, -7);
    EXPECT_FLOAT_EQ(item.angles.y, 70);
    item.position.z = 60;
    ASSERT_TRUE(Client::Items::StepFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_FLOAT_EQ(item.position.z, 70);
    EXPECT_FLOAT_EQ(item.dropSpeed, -13); // the contact does not reset gravity.
    EXPECT_EQ(item.angles, glm::vec3(60, 0, -45));
    EXPECT_EQ(random.State(), 7u);
    item.type = 0xe12;
    item.position.z = 200;
    ASSERT_TRUE(Client::Items::StepFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_FLOAT_EQ(item.angles.z, 190);
    EXPECT_FLOAT_EQ(item.position.z, 187);
    item.type = 0x1e21;
    item.position.z = 20;
    item.dropSpeed = 0;
    ASSERT_TRUE(Client::Items::StepFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_FLOAT_EQ(item.position.z, 30);
}

TEST(GameDrawTailTest, FloorStepTakesItsTwoCallerRandomsEvenWhenTheParticlePoolIsFull)
{
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    auto attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    Client::Particles::Particles particles;
    for (auto& particle : particles.All())
        particle.alive = true;
    Client::Crt::CrtRandom random(7);
    Client::Kernel::FloorItemState item;
    item.alive = true;
    item.type = 0x212;
    item.effectCounter = 48;
    const Client::Items::FloorItemStepContext context{*heights, *attributes, random, particles, 0};
    ASSERT_TRUE(Client::Items::StepFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_EQ(item.effectCounter, 49);
    EXPECT_EQ(random.State(), 3289278137u);
    EXPECT_TRUE(item.alive);
}

TEST(GameDrawTailTest, FloorStepEmitsBothSparkSubtypesAtTheSameRotatedPoint)
{
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    auto attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(7);
    Client::Kernel::FloorItemState item;
    item.alive = true;
    item.type = 0x212;
    item.position = {100, 200, 10000};
    item.dropSpeed = 6;
    const Client::Items::FloorItemStepContext context{*heights, *attributes, random, particles, 0};
    ASSERT_TRUE(Client::Items::StepFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_EQ(item.effectCounter, 1);
    EXPECT_EQ(random.State(), 3289278137u); // R1=61→45, R2=17422→30; 0x542ab9 does not roll for /0, /1.
    for (std::size_t i = 0; i < 2; ++i)
    {
        const auto& spark = particles.All()[i];
        EXPECT_TRUE(spark.alive);
        EXPECT_EQ(spark.type, 0x5fc);
        EXPECT_EQ(spark.subtype, static_cast<std::int32_t>(i));
        EXPECT_EQ(spark.life, 18);
        EXPECT_EQ(spark.position, glm::vec3(145, 200, 10036));
        EXPECT_EQ(spark.angles, glm::vec3(45, 0, 0));
    }
}

TEST(GameDrawTailTest, FloorPublishesItsPoseEvenWhenAlphaSuppressesTheBody)
{
    Client::Render::ModelBatch batch;
    TriangleModels models;
    Client::Terrain::SphereFrustum frustum;
    Client::Kernel::FloorItemState item;
    item.alive = true;
    item.type = 0x212;
    item.scale = 1;
    item.position = {10, 20, 30};
    int poses = 0;
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.onPose =
        [&poses](const Client::Actors::Drawing::DrawnObject& object) -> Client::Core::Expected<void, std::string>
    {
        ++poses;
        EXPECT_FLOAT_EQ(object.transform.position.z, 30);
        return {};
    };
    context.project = [](const glm::vec3&) -> Client::Core::Expected<glm::ivec2, std::string> { return glm::ivec2{}; };
    ASSERT_TRUE(Client::Scenes::DrawFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_EQ(poses, 1);
    EXPECT_TRUE(batch.Vertices().empty());
}

TEST(GameDrawTailTest, ModelEffectPublishesItsPoseBeforeGeometryAndNeverForGroundDecals)
{
    Client::Render::ModelBatch batch;
    TriangleModels models;
    Client::Terrain::SphereFrustum frustum;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Scenes::ModelEffectDrawResources resources{*heights, 0};
    Client::Effects::ModelEffect effect;
    effect.alive = true;
    effect.type = 0xc2;
    effect.transform.scale = 1;
    int poses = 0;
    resources.onPose =
        [&poses](const Client::Actors::Drawing::DrawnObject&) -> Client::Core::Expected<void, std::string>
    {
        ++poses;
        return Client::Core::Unexpected(std::string("pose fixture"));
    };
    const auto result =
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), frustum, nullptr, {}, 0, resources);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), "pose fixture");
    EXPECT_EQ(poses, 1);
    EXPECT_TRUE(batch.Vertices().empty());
    resources.onPose =
        [&poses](const Client::Actors::Drawing::DrawnObject&) -> Client::Core::Expected<void, std::string>
    {
        ++poses;
        return {};
    };
    effect.alpha = 0;
    ASSERT_TRUE(
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), frustum, nullptr, {}, 0, resources)
            .HasValue());
    EXPECT_EQ(poses, 1);
    effect.type = 0xcf;
    effect.subtype = 3;
    ASSERT_TRUE(
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), frustum, nullptr, {}, 0, resources)
            .HasValue());
    EXPECT_EQ(poses, 1);
}

// 0x52d6c3: the earthquake's pulse 0x103 has no case of its own and goes via 0x52da84 → 0x602e90 with its mesh,
// flashing.bmd (0x615700). It is born with blendMesh 0: in the second pass 0x52d66a..0x52d693 filters it only with −1.
TEST(GameDrawTailTest, TheEarthquakeBurstDrawsFlashingBmdThroughTheGenericPath)
{
    TriangleModels models;
    Client::Terrain::SphereFrustum frustum;
    Client::Effects::ModelEffect burst;
    burst.alive = true;
    burst.type = Client::Effects::EarthquakeBurstEffect;
    burst.objectState.blendMesh = 0;
    burst.alpha = 1;
    burst.transform.scale = 0.5f;
    Client::Render::ModelBatch batch;
    const auto drawn =
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&burst, 1), frustum, nullptr, {}, 0, 0);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_FALSE(batch.Vertices().empty());
    Client::Render::ModelBatch secondary;
    const auto drawnAgain =
        Client::Scenes::DrawGameModelEffects(secondary, models, std::span(&burst, 1), frustum, nullptr, {}, 0, 1);
    ASSERT_TRUE(drawnAgain.HasValue()) << drawnAgain.Error();
    EXPECT_EQ(secondary.Vertices().size(), batch.Vertices().size());
    burst.objectState.blendMesh = -1;
    Client::Render::ModelBatch excluded;
    const auto skipped =
        Client::Scenes::DrawGameModelEffects(excluded, models, std::span(&burst, 1), frustum, nullptr, {}, 0, 1);
    ASSERT_TRUE(skipped.HasValue()) << skipped.Error();
    EXPECT_TRUE(excluded.Vertices().empty());
    // Without flashing.bmd the mesh count is 0 (0x60dbe0): the first pass draws nothing and does not cut.
    burst.objectState.blendMesh = 0;
    models.missingModel = static_cast<std::uint16_t>(Client::Effects::EarthquakeBurstEffect);
    Client::Render::ModelBatch missing;
    const auto withoutModel =
        Client::Scenes::DrawGameModelEffects(missing, models, std::span(&burst, 1), frustum, nullptr, {}, 0, 0);
    ASSERT_TRUE(withoutModel.HasValue()) << withoutModel.Error();
    EXPECT_TRUE(missing.Vertices().empty());
}

// 0x52d620 → 0x52d400: the horse's earthquake 0xfa (state 2) is not drawn and looks up no models, in any life. Cracks
// 0xfb..0x102 go via 0x52da84 → 0x602e90 with their mesh; with +0x74 at 0x80 (0xfc, 0xff and 0x102 of subtype 1),
// 0x6002e0 draws them through 0x4ef0c0 with 0x82 (0x6004aa), with the darkening blend.
TEST(GameDrawTailTest, TheHorseEarthquakeIsInvisibleAndItsCracksDrawTheirMesh)
{
    Client::Terrain::SphereFrustum frustum;
    for (const std::int32_t life : {20, 11, 5})
    {
        SCOPED_TRACE(life);
        LookupCounter models;
        Client::Render::ModelBatch batch;
        Client::Effects::ModelEffect quake;
        quake.alive = true;
        quake.type = Client::Effects::EarthquakeEffect;
        quake.state = 2;
        quake.life = life;
        quake.transform.scale = 0.9f;
        const auto drawn =
            Client::Scenes::DrawGameModelEffects(batch, models, std::span(&quake, 1), frustum, nullptr, {}, 0, 0);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(models.lookups, 0);
        EXPECT_TRUE(batch.Vertices().empty());
    }
    TriangleModels models;
    for (std::int16_t type = Client::Effects::FirstEarthquakeCrack; type <= Client::Effects::LastEarthquakeCrack;
         ++type)
    {
        SCOPED_TRACE(type);
        Client::Render::ModelBatch batch;
        Client::Effects::ModelEffect crack;
        crack.alive = true;
        crack.type = type;
        crack.subtype = 1;
        crack.life = 35;
        crack.transform.scale = 1.5f;
        const auto drawn =
            Client::Scenes::DrawGameModelEffects(batch, models, std::span(&crack, 1), frustum, nullptr, {}, 0, 0);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(batch.Vertices().size(), 3u);

        crack.renderFlags = Client::Effects::CrackRenderFlags;
        Client::Render::ModelBatch flagged;
        const auto dark =
            Client::Scenes::DrawGameModelEffects(flagged, models, std::span(&crack, 1), frustum, nullptr, {}, 0, 0);
        ASSERT_TRUE(dark.HasValue()) << dark.Error();
        EXPECT_EQ(flagged.Vertices().size(), 3u);
        ASSERT_FALSE(flagged.Commands().empty());
        EXPECT_EQ(flagged.Commands().back().state.blend, Client::Render::ModelBlend::Subtractive);
    }
}

// 0x6004aa, 0x601da8 and 0x601df6 with the wizards' effects: the 0xd0 (with +0x74 at 0x80 from creation) darkens; the
// 0xcc/3 and /4 go with the model color at (0.1f; 0.1f; 10) and the common pass; the 0xcb/2 and /3 with (0.5; 0.5; 1)
// and Effect\Magic_b.jpg (0x6e9) on each mesh. The 0xcc/0 and the 0xcb/0, with their light's color. With per-vertex
// light (+0xe8) and normal (0, 0, 1), each corner goes with the model color times 2 (NormalLight: 2 · 0.8 + 0.4, the
// table 0x715a98c by the +0x48 of 0x5ffce0).
TEST(GameDrawTailTest, TheWizardEffectsTakeTheirBodyPass)
{
    struct Case
    {
        std::int16_t type;
        std::int32_t subtype;
        std::uint32_t renderFlags;
        Client::Render::ModelBlend blend;
        glm::vec3 color;
        Client::Render::TextureSlot texture;
    };
    constexpr glm::vec3 Light{0.25f, 0.25f, 0.25f};
    const glm::vec3 deep{std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu), 10.0f};
    constexpr Client::Render::TextureSlot Plain{0x400};
    constexpr Client::Render::TextureSlot Magic{0x6e9};
    constexpr float NormalLight = 2.0f;
    using Client::Render::ModelBlend;
    const std::array cases{
        Case{Client::Effects::LaserEffect, 0, Client::Effects::CrackRenderFlags, ModelBlend::Subtractive, Light, Plain},
        Case{Client::Effects::OuterCircleEffect, 1, Client::Effects::CrackRenderFlags, ModelBlend::Subtractive, Light,
             Plain},
        Case{Client::Effects::OuterCircleEffect, 3, 0, ModelBlend::Opaque, deep, Plain},
        Case{Client::Effects::OuterCircleEffect, 4, 0, ModelBlend::Opaque, deep, Plain},
        Case{Client::Effects::OuterCircleEffect, 0, 0, ModelBlend::Opaque, Light, Plain},
        Case{Client::Effects::InnerCircleEffect, 2, 0, ModelBlend::Opaque, {0.5f, 0.5f, 1.0f}, Magic},
        Case{Client::Effects::InnerCircleEffect, 3, 0, ModelBlend::Opaque, {0.5f, 0.5f, 1.0f}, Magic},
        Case{Client::Effects::InnerCircleEffect, 0, 0, ModelBlend::Opaque, Light, Plain},
    };
    Client::Terrain::SphereFrustum frustum;
    TriangleModels models;
    for (const Case& tested : cases)
    {
        SCOPED_TRACE(tested.type);
        SCOPED_TRACE(tested.subtype);
        Client::Render::ModelBatch batch;
        Client::Effects::ModelEffect effect;
        effect.alive = true;
        effect.type = tested.type;
        effect.subtype = tested.subtype;
        effect.life = 10;
        effect.renderFlags = tested.renderFlags;
        effect.light = Light;
        const auto drawn =
            Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), frustum, nullptr, {}, 0, 0);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        ASSERT_EQ(batch.Vertices().size(), 3u);
        std::size_t drawing = 0;
        for (const Client::Render::ModelDrawCommand& command : batch.Commands())
        {
            if (command.operation != Client::Render::ModelStateOperation::Draw || command.vertexCount == 0)
                continue;
            ++drawing;
            EXPECT_EQ(command.state.blend, tested.blend);
            EXPECT_EQ(command.state.texture, std::optional(tested.texture));
        }
        EXPECT_EQ(drawing, 1u);
        for (const Client::Render::ModelVertex& vertex : batch.Vertices())
        {
            EXPECT_FLOAT_EQ(vertex.color.red, NormalLight * tested.color.x);
            EXPECT_FLOAT_EQ(vertex.color.green, NormalLight * tested.color.y);
            EXPECT_FLOAT_EQ(vertex.color.blue, NormalLight * tested.color.z);
        }
    }
}

// 0x60213e..0x60216b: the 0x109 (Protect2) with +0x4 at 1 (the edx of 0x601095) adds before the common pass
// (0x602170) RenderBody(0x1040, …, 1, −1): chrome 0x1000 with the pass texture (0x620), additive, without mesh 1. The
// 0x109/0, the shield of 0x4cb3b0 and the only 0x109 the original creates, goes only through the common pass: each
// mesh with its texture.
TEST(GameDrawTailTest, OnlyTheShieldOfSubtypeOneTakesTheChromePassBeforeTheCommon)
{
    using Client::Render::ModelBlend;
    using Client::Render::TextureSlot;
    struct Draw
    {
        ModelBlend blend;
        std::optional<TextureSlot> texture;
        std::size_t vertices;
    };
    Client::Terrain::SphereFrustum frustum;
    TwoMeshModels models;
    std::array<std::vector<Draw>, 2> draws;
    for (const std::int32_t subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        Client::Render::ModelBatch batch;
        Client::Effects::ModelEffect shield;
        shield.alive = true;
        shield.type = 0x109;
        shield.subtype = subtype;
        shield.life = 20;
        shield.objectState.blendMesh = 0; // 0x51921a
        shield.light = {0.25f, 0.25f, 0.25f};
        const auto drawn =
            Client::Scenes::DrawGameModelEffects(batch, models, std::span(&shield, 1), frustum, nullptr, {}, 0, 0);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        for (const Client::Render::ModelDrawCommand& command : batch.Commands())
            if (command.operation == Client::Render::ModelStateOperation::Draw && command.vertexCount != 0)
                draws[static_cast<std::size_t>(subtype)].push_back(
                    {command.state.blend, command.state.texture, command.vertexCount});
    }
    const std::vector<Draw>& common = draws[0];
    const std::vector<Draw>& chromed = draws[1];
    ASSERT_FALSE(common.empty());
    ASSERT_EQ(chromed.size(), common.size() + 1);
    // Only mesh 0: mesh 1 is hidden in this pass.
    EXPECT_EQ(chromed[0].blend, ModelBlend::Additive);
    EXPECT_EQ(chromed[0].texture, std::optional(TextureSlot{0x620}));
    EXPECT_EQ(chromed[0].vertices, 3u);
    for (std::size_t index = 0; index < common.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(chromed[index + 1].blend, common[index].blend);
        EXPECT_EQ(chromed[index + 1].texture, common[index].texture);
        EXPECT_EQ(chromed[index + 1].vertices, common[index].vertices);
    }
    for (const Draw& draw : common)
        EXPECT_NE(draw.texture, std::optional(TextureSlot{0x620}));
}

// 0x6002e0 → 0x442e10 with the 0x13d and the 0x13e, without RenderBody: mesh 0 with 2, light 1 and the color at
// (1, 1, 1). The 0x13d, with blendMesh −1 (opaque, its texture) and then with 0 (additive) and texture 0x63c. The
// 0x13e, once with blendMesh 0 and U at FSIN(T · 0.00008f) · 2.5f, and leaves +0x6c at 1; without the world clock, it
// cuts.
TEST(GameDrawTailTest, TheMeteorRockAndItsShellTakeTheirOwnPasses)
{
    Client::Terrain::SphereFrustum frustum;
    TriangleModels models;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    const Client::Scenes::ModelEffectDrawResources resources{*heights, 30};
    using Client::Render::ModelBlend;
    struct Pass
    {
        ModelBlend blend;
        Client::Render::TextureSlot texture;
    };
    const auto passes = [](const Client::Render::ModelBatch& batch)
    {
        std::vector<Pass> drawn;
        for (const Client::Render::ModelDrawCommand& command : batch.Commands())
            if (command.operation == Client::Render::ModelStateOperation::Draw && command.vertexCount != 0)
                drawn.push_back({command.state.blend, command.state.texture.value_or(Client::Render::TextureSlot{})});
        return drawn;
    };
    Client::Effects::ModelEffect rock;
    rock.alive = true;
    rock.type = 0x13d;
    rock.life = 10;
    rock.light = {0.25f, 0.25f, 0.25f};
    rock.objectState.uv = {0.5f, 0.25f};
    Client::Render::ModelBatch batch;
    const auto drawn =
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&rock, 1), frustum, nullptr, {}, 0, resources);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    const auto rockPasses = passes(batch);
    ASSERT_EQ(rockPasses.size(), 2u);
    EXPECT_EQ(rockPasses[0].blend, ModelBlend::Opaque);
    EXPECT_EQ(rockPasses[0].texture, Client::Render::TextureSlot{0x400});
    EXPECT_EQ(rockPasses[1].blend, ModelBlend::Additive);
    EXPECT_EQ(rockPasses[1].texture, Client::Render::TextureSlot{0x63c});
    EXPECT_EQ(rock.objectState.uv, (glm::vec2{0.5f, 0.25f}));

    Client::Effects::ModelEffect shell = rock;
    shell.type = 0x13e;
    Client::Render::ModelBatch shellBatch;
    const auto swayed = Client::Scenes::DrawGameModelEffects(shellBatch, models, std::span(&shell, 1), frustum, nullptr,
                                                             {}, 0, resources);
    ASSERT_TRUE(swayed.HasValue()) << swayed.Error();
    const auto shellPasses = passes(shellBatch);
    ASSERT_EQ(shellPasses.size(), 1u);
    EXPECT_EQ(shellPasses[0].blend, ModelBlend::Additive);
    EXPECT_EQ(shellPasses[0].texture, Client::Render::TextureSlot{0x400});
    EXPECT_EQ(shell.objectState.uv, (glm::vec2{1.0f, 0.25f}));

    Client::Render::ModelBatch clockless;
    const auto cut =
        Client::Scenes::DrawGameModelEffects(clockless, models, std::span(&shell, 1), frustum, nullptr, {}, 0, 0);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x443605")) << cut.Error();
}

// 0x4ecf22..0x4ed035 with RenderBody(0x82) (0x6004aa): the blendMesh mesh goes through 0x4ed014, which only looks at
// 0x80 (0x620b10, ZERO / ONE_MINUS_SRC_COLOR); the other, through 0x4ecf68, too. With blendMesh at −2 both go through
// 0x4ed014. Without 0x80 (the common pass, RenderBody(2)) the blendMesh one is additive (0x620a90) and the other
// opaque.
TEST(GameDrawTailTest, TheDarkBodyDarkensItsBlendMeshToo)
{
    TwoMeshModels models;
    Client::Terrain::SphereFrustum frustum;
    using Client::Render::ModelBlend;
    struct Case
    {
        std::uint32_t renderFlags;
        std::int16_t blendMesh;
        std::array<ModelBlend, 2> blends;
    };
    for (const Case& tested :
         {Case{Client::Effects::CrackRenderFlags, 0, {ModelBlend::Subtractive, ModelBlend::Subtractive}},
          Case{Client::Effects::CrackRenderFlags, -2, {ModelBlend::Subtractive, ModelBlend::Subtractive}},
          Case{0, 0, {ModelBlend::Additive, ModelBlend::Opaque}}})
    {
        SCOPED_TRACE(tested.renderFlags);
        SCOPED_TRACE(tested.blendMesh);
        Client::Render::ModelBatch batch;
        Client::Effects::ModelEffect laser;
        laser.alive = true;
        laser.type = Client::Effects::LaserEffect;
        laser.life = 10;
        laser.renderFlags = tested.renderFlags;
        laser.objectState.blendMesh = tested.blendMesh;
        laser.light = {0.25f, 0.25f, 0.25f};
        const auto drawn =
            Client::Scenes::DrawGameModelEffects(batch, models, std::span(&laser, 1), frustum, nullptr, {}, 0, 0);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        ASSERT_EQ(batch.Vertices().size(), 6u);
        std::vector<ModelBlend> blends;
        for (const Client::Render::ModelDrawCommand& command : batch.Commands())
            if (command.operation == Client::Render::ModelStateOperation::Draw && command.vertexCount != 0)
                blends.push_back(command.state.blend);
        ASSERT_EQ(blends.size(), 2u);
        EXPECT_EQ(blends[0], tested.blends[0]);
        EXPECT_EQ(blends[1], tested.blends[1]);
    }
}

// 0x600328: with +0x58 at −2 (the selection's butterfly 0xae, 0x51913a) 0x6002e0 exits after posing: the pose is
// published and the mesh is not drawn.
TEST(GameDrawTailTest, AHiddenModelEffectPublishesItsPoseWithoutGeometry)
{
    TriangleModels models;
    Client::Terrain::SphereFrustum frustum;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Kernel::PoseBuffer carry;
    Client::Scenes::ModelEffectDrawResources resources{*heights, 0};
    int poses = 0;
    resources.onPose = [&](const Client::Actors::Drawing::DrawnObject& object)
    {
        ++poses;
        return Client::Actors::Drawing::PublishPose(carry, models, object, true);
    };
    resources.posedMeshes = [&]() -> std::span<const Client::Models::SkinnedMesh> { return carry.meshes; };
    for (const bool hidden : {true, false})
    {
        Client::Render::ModelBatch batch;
        Client::Effects::ModelEffect effect;
        effect.alive = true;
        effect.type = Client::Effects::ButterflyEffect;
        effect.transform.scale = 1;
        if (hidden)
            effect.objectState.hiddenMesh = Client::Kernel::HiddenObject;
        const auto drawn = Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), frustum, nullptr,
                                                                {}, 0, resources);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(batch.Vertices().empty(), hidden);
        Client::Render::ModelBatch plain;
        ASSERT_TRUE(
            Client::Scenes::DrawGameModelEffects(plain, models, std::span(&effect, 1), frustum, nullptr, {}, 0, 0)
                .HasValue());
        EXPECT_EQ(plain.Vertices().empty(), hidden);
    }
    EXPECT_EQ(poses, 2);
}

TEST(GameDrawTailTest, ModelEffectNonzeroModeFiltersBlendMeshBeforeDispatchingItsSpecialCase)
{
    Client::Render::ModelBatch batch;
    LookupCounter missing;
    Client::Terrain::SphereFrustum frustum;
    Client::Effects::ModelEffect effect;
    effect.alive = true;
    effect.type = 0x14e;
    ASSERT_TRUE(Client::Scenes::DrawGameModelEffects(batch, missing, std::span(&effect, 1), frustum, nullptr, {}, 0, 1)
                    .HasValue());
    EXPECT_TRUE(effect.seen);
    EXPECT_EQ(missing.lookups, 0);
    EXPECT_TRUE(batch.Commands().empty());
    effect.objectState.blendMesh = -3;
    ASSERT_TRUE(Client::Scenes::DrawGameModelEffects(batch, missing, std::span(&effect, 1), frustum, nullptr, {}, 0, 1)
                    .HasValue());
    EXPECT_EQ(missing.lookups, 0);
    effect.objectState.blendMesh = 2;
    const auto withoutMeshes =
        Client::Scenes::DrawGameModelEffects(batch, missing, std::span(&effect, 1), frustum, nullptr, {}, 0, 1);
    ASSERT_TRUE(withoutMeshes.HasValue()) << withoutMeshes.Error();
    TriangleModels oneMesh;
    ASSERT_TRUE(Client::Scenes::DrawGameModelEffects(batch, oneMesh, std::span(&effect, 1), frustum, nullptr, {}, 0, 1)
                    .HasValue());
    EXPECT_TRUE(batch.Commands().empty());
    effect.type = -1;
    const auto invalid =
        Client::Scenes::DrawGameModelEffects(batch, missing, std::span(&effect, 1), frustum, nullptr, {}, 0, 1);
    ASSERT_FALSE(invalid.HasValue());
    EXPECT_NE(invalid.Error().find("0x52d68c"), std::string::npos);
}

TEST(GameDrawTailTest, AnUnloadedModelDoesNotAbortTheRestOfTheSecondPass)
{
    Client::Render::ModelBatch batch;
    TriangleModels models;
    models.missingModel = std::uint16_t{0x14e};
    Client::Terrain::SphereFrustum frustum;
    std::array<Client::Effects::ModelEffect, 2> effects{};
    effects[0].alive = true;
    effects[0].type = 0x14e;
    effects[0].objectState.blendMesh = 1;
    effects[1].alive = true;
    effects[1].type = 0xdb;
    effects[1].objectState.blendMesh = 1;
    effects[1].transform.scale = 1;

    const auto drawn = Client::Scenes::DrawGameModelEffects(batch, models, effects, frustum, nullptr, {}, 0, 1);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_TRUE(effects[1].seen);
    EXPECT_EQ(batch.Vertices().size(), 3u);
}

TEST(GameDrawTailTest, MirroredModelEffectDrawsTwiceAndRetainsItsSecondHeight)
{
    Client::Render::ModelBatch batch;
    TriangleModels models;
    Client::Terrain::SphereFrustum frustum;
    Client::Effects::ModelEffect effect;
    effect.alive = true;
    effect.type = 0x135;
    effect.transform.scale = 1;
    effect.transform.position.z = 100;
    effect.start.z = 20; // +184.
    ASSERT_TRUE(
        Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1), frustum, nullptr, {}, 0).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.z, 100);
    EXPECT_FLOAT_EQ(batch.Vertices()[3].position.z, 340); // (20+400) -(100-20).
    EXPECT_FLOAT_EQ(effect.transform.position.z, 340);
    EXPECT_TRUE(effect.seen);
}

TEST(GameDrawTailTest, FloorBodyUsesThePrecedingTransparentPoseAndRetainsItsDummyBone)
{
    Client::Render::ModelBatch batch;
    RetainedDummyModels models;
    Client::Terrain::SphereFrustum frustum;
    Client::Kernel::PoseBuffer carry;
    carry.meshes.resize(1);
    carry.meshes[0].positions.assign(3, glm::vec3(42));
    std::array<Client::Kernel::FloorItemState, 2> items{};
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        items[i].alive = true;
        items[i].type = static_cast<std::int16_t>(0x212 + i);
        items[i].scale = 1;
    }
    items[1].alpha = 1;
    items[1].position = {1000, 1000, 300};
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.onPose = [&](const Client::Actors::Drawing::DrawnObject& object)
    {
        auto published = Client::Actors::Drawing::PublishFloorPose(carry, models, object);
        if (object.model == 0x212)
        {
            EXPECT_EQ(carry.meshes[0].positions[0], glm::vec3(42)); // alpha0 posa bones, no skin.
        }
        return published;
    };
    context.posedMeshes = [&]() -> std::span<const Client::Models::SkinnedMesh> { return carry.meshes; };
    context.project = [](const glm::vec3&) -> Client::Core::Expected<glm::ivec2, std::string> { return glm::ivec2{}; };
    ASSERT_TRUE(Client::Scenes::DrawFloorItems(items, context).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_EQ(batch.Vertices()[0].position, glm::vec3(1007, 1008, 309));
}

TEST(GameDrawTailTest, ModelEffectBodyUsesTheDummyBonePublishedByThePrecedingModel)
{
    Client::Render::ModelBatch batch;
    RetainedDummyModels models;
    Client::Terrain::SphereFrustum frustum;
    Client::Kernel::PoseBuffer carry;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    std::array<Client::Effects::ModelEffect, 2> effects{};
    for (std::size_t i = 0; i < effects.size(); ++i)
    {
        effects[i].alive = true;
        effects[i].type = static_cast<std::int16_t>(0xc2 + i);
        effects[i].transform.scale = 1;
    }
    effects[1].transform.position = {1000, 1000, 300};
    Client::Scenes::ModelEffectDrawResources resources{*heights, 0};
    resources.onPose = [&](const Client::Actors::Drawing::DrawnObject& object)
    { return Client::Actors::Drawing::PublishPose(carry, models, object, true); };
    resources.posedMeshes = [&]() -> std::span<const Client::Models::SkinnedMesh> { return carry.meshes; };
    ASSERT_TRUE(
        Client::Scenes::DrawGameModelEffects(batch, models, effects, frustum, nullptr, {}, 0, resources).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    // 0x4eb860 with the flag at 1 does not rewrite the dummy (0x4eb973): the previous one's matrix, already in the
    // world, stays (its origin is 0), and 0x4ebc80 with Translate at 0 does not add the second's origin.
    EXPECT_EQ(batch.Vertices()[3].position, glm::vec3(7, 8, 9));
}

TEST(GameDrawTailTest, LegacyModelEffectsConsumeTheSamePublishedDummyBone)
{
    Client::Render::ModelBatch batch;
    RetainedDummyModels models;
    Client::Terrain::SphereFrustum frustum;
    Client::Kernel::PoseBuffer carry;
    std::array<Client::Effects::ModelEffect, 2> effects{};
    for (std::size_t i = 0; i < effects.size(); ++i)
    {
        effects[i].alive = true;
        effects[i].type = static_cast<std::int16_t>(0xc2 + i);
        effects[i].transform.scale = 1;
    }
    effects[1].transform.position = {1000, 1000, 300};
    const Client::Actors::Drawing::TailPoseObserver onPose = [&](const Client::Actors::Drawing::DrawnObject& object)
    { return Client::Actors::Drawing::PublishPose(carry, models, object, true); };
    const Client::Actors::Drawing::TailMeshProvider posedMeshes = [&]() -> std::span<const Client::Models::SkinnedMesh>
    { return carry.meshes; };
    ASSERT_TRUE(Client::Effects::DrawModelEffects(batch, models, effects, frustum, nullptr, {}, 0, onPose, posedMeshes)
                    .HasValue());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_EQ(batch.Vertices()[3].position,
              glm::vec3(7, 8, 9)); // The previous one's matrix, without the origin (0x4ebc80).
}

TEST(GameDrawTailTest, PublishedFloorBodyReportsMissingAndIncompleteScratchBeforeProjection)
{
    Client::Render::ModelBatch batch;
    TriangleModels models;
    Client::Terrain::SphereFrustum frustum;
    Client::Kernel::FloorItemState item;
    item.alive = true;
    item.type = 0x212;
    item.alpha = 1;
    item.screenX = 1234;
    int poses = 0;
    bool projected = false;
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.onPose = [&](const Client::Actors::Drawing::DrawnObject&) -> Client::Core::Expected<void, std::string>
    {
        ++poses;
        return {};
    };
    context.project = [&](const glm::vec3&) -> Client::Core::Expected<glm::ivec2, std::string>
    {
        projected = true;
        return glm::ivec2{};
    };
    auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x4ebc80"), std::string::npos);
    EXPECT_EQ(poses, 1);
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_FALSE(projected);
    EXPECT_EQ(item.screenX, 1234);
    context.posedMeshes = []() -> std::span<const Client::Models::SkinnedMesh> { return {}; };
    result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("no cubre"), std::string::npos);
    EXPECT_EQ(poses, 2);
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_FALSE(projected);
}
