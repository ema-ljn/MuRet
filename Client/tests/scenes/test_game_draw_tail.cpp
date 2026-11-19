#include "Scenes/WorldObjectsDrawing.h"
#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Scenes/GameFloorItems.h"
#include "Render/TextureSlot.h"
#include "Render/Pass3D.h"
#include "World/IFishStepServices.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameProjection.h"
#include "Scenes/GameParticles.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/FaunaObject.h"
#include "Scenes/FaunaDrawing.h"
#include "Render/Models/Skinning.h"
#include "Render/Models/Cloth.h"
#include "World/Particles.h"
#include "Render/Billboard.h"
#include "World/Leaves.h"
#include "World/Sprites.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Skeleton.h"
#include "World/ModelEffects.h"
#include "World/WizardModelEffects.h"
#include "World/GroundEffectDrawing.h"
#include "World/SphereFrustum.h"
#include "World/WorldObjects.h"

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
class LookupCounter final : public Client::Scenes::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        ++lookups;
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
    mutable int lookups = 0;
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
    const Client::Data::Models::Model* Find(std::uint16_t id) const override
    {
        return missingModel == id ? nullptr : &model;
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
    std::optional<std::uint16_t> missingModel;
};
// Dos triángulos, cada uno con su textura (0 y 1), con un hueso.
class TwoMeshModels final : public Client::Scenes::IModelLibrary
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
    std::array<Client::Render::TextureSlot, 2> textures{Client::Render::TextureSlot{0x400},
                                                        Client::Render::TextureSlot{0x401}};
};
class RetainedDummyModels final : public Client::Scenes::IModelLibrary
{
public:
    RetainedDummyModels()
    {
        producer.skeleton.bones[0].actions[0][0].position = {7, 8, 9};
        consumer.skeleton.bones[0].dummy = true;
    }
    const Client::Data::Models::Model* Find(std::uint16_t type) const override
    {
        return Selected(type).Find(type);
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t type) const override
    {
        return Selected(type).SkeletonOf(type);
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t type) const override
    {
        return Selected(type).MeshTextures(type);
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

// 52da84: los efectos de suelo publican seen pero no consultan modelos en el pase de cuerpos.
TEST(GameDrawTailTest, GroundOnlyEffectsPublishVisibilityWithoutLookingForModels)
{
    Client::Render::ModelBatch batch;
    LookupCounter models;
    std::array<Client::World::ModelEffect, 3> effects{};
    for (std::size_t i = 0; i < effects.size(); ++i)
    {
        effects[i].alive = true;
        effects[i].type = static_cast<std::int16_t>(0x61d + i);
    }
    Client::Scenes::DrawModelEffects(batch, models, effects, Client::World::SphereFrustum{}, nullptr, {}, 0.0f);
    EXPECT_EQ(models.lookups, 0);
    for (const auto& effect : effects)
        EXPECT_TRUE(effect.seen);
    EXPECT_TRUE(batch.Vertices().empty());
}

TEST(GameDrawTailTest, LorenciaLeavesIgnoreScaleAndPublishTheEmptyPassBlend)
{
    Client::Render::ModelBatch batch;
    Client::World::Leaves leaves;
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
    Client::World::Leaves leaves;
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
    // El621400 de5306a0 no apila identidad: conserva la modelview del caller.
    EXPECT_EQ(batch.State().space, Client::Render::ModelCoordinateSpace::World);
}

TEST(GameDrawTailTest, RibbonsChangeBlendBeforeCheckingTheirPointCount)
{
    Client::Render::ModelBatch batch;
    Client::Scenes::RibbonState ribbon;
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
    Client::Scenes::RibbonState ribbon;
    ribbon.alive = true;
    EXPECT_FALSE(Client::Scenes::DrawRibbons(batch, std::span(&ribbon, 1)).HasValue());
    EXPECT_TRUE(batch.Commands().empty());
}

TEST(GameDrawTailTest, DamageKeepsItsUnconditionalStateAndReportsItsLiveFrontier)
{
    Client::Render::ModelBatch batch;
    Client::Scenes::DamageNumberState damage;
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
    Client::Scenes::DamageNumberState damage;
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
    Client::Scenes::DamageNumberState damage;
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
    Client::Scenes::JointState joint;
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
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 0).HasValue());
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
    Client::World::FrameSprites sprites;
    ASSERT_TRUE(sprites.Create(0x5ac, {0, 0, 325}, 1, {1, 1, 1}, 0, 0));
    ASSERT_TRUE(sprites.Create(0x5ac, {0, 0, 300}, 1, {1, 1, 1}, 0, 0));
    ASSERT_TRUE(sprites.Create(0x5ac, {0, 0, std::numeric_limits<float>::quiet_NaN()}, 1, {1, 1, 1}, 0, 0));
    const auto view = Client::Render::ViewRows(glm::mat4(1.0f));
    Client::Scenes::DrawFrameSprites(batch, sprites, models, view, 1);
    EXPECT_TRUE(sprites.Sprites()[0].alive);
    EXPECT_TRUE(sprites.Sprites()[1].alive);
    EXPECT_TRUE(sprites.Sprites()[2].alive);
    Client::Scenes::DrawFrameSprites(batch, sprites, models, view, 2);
    EXPECT_FALSE(sprites.Any());
}

TEST(GameDrawTailTest, SpriteBlendThreeUsesTheInverseColorAdditiveProfile)
{
    Client::Render::ModelBatch batch;
    LookupCounter models;
    Client::World::FrameSprites sprites;
    ASSERT_TRUE(sprites.Create(0x5ac, {0, 0, 0}, 1, {1, 1, 1}, 0, 3));
    Client::Scenes::DrawFrameSprites(batch, sprites, models, Client::Render::ViewRows(glm::mat4(1.0f)));
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::InverseColorAdditive);
}

TEST(GameDrawTailTest, PlainFloorSwordsRenderBeforePublishingTheirProjectedShorts)
{
    Client::Render::ModelBatch batch;
    TriangleModels models;
    Client::World::SphereFrustum frustum;
    Client::Scenes::FloorItemState item;
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
    Client::Scenes::GameProjection projection;
    projection.view = Client::Render::ViewRows(glm::mat4(1.0f));
    projection.horizontalScale = 0.5f;
    projection.verticalScale = 0.25f;
    projection.centerX = 512;
    projection.centerY = 384;
    projection.screenWidth = 1024;
    projection.screenHeight = 768;
    const auto p = Client::Scenes::ProjectGamePoint(projection, {1.9f, 2.9f, -2});
    ASSERT_TRUE(p.HasValue());
    EXPECT_EQ(p.Value().x, 320); // (512 - trunc(-1.9)) *640/1024
    EXPECT_EQ(p.Value().y, 236); // (384 + trunc(-5.8)) *480/768
}

TEST(GameDrawTailTest, ParticleModesFilterBeforeLookingUpUnsupportedResources)
{
    Client::Render::ModelBatch batch;
    LookupCounter models;
    Client::World::Particles particles;
    particles.All()[0].alive = true;
    particles.All()[0].type = -1;
    particles.All()[0].position.z = 351;
    const auto view = Client::Render::ViewRows(glm::mat4(1.0f));
    EXPECT_TRUE(Client::Scenes::DrawGameParticles(batch, particles, models, view, 0, 1).HasValue());
    EXPECT_FALSE(Client::Scenes::DrawGameParticles(batch, particles, models, view, 0, 2).HasValue());
    EXPECT_TRUE(particles.All()[0].alive);
}

// Fixtures calculados aparte con Decimal120, tan→R64 y stores PC53/F32, sin ejecutar el original.
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
        const auto result = Client::Scenes::MakeGameProjection(pass, 35);
        ASSERT_TRUE(result.HasValue());
        EXPECT_EQ(std::bit_cast<std::uint32_t>(result.Value().horizontalScale), sample.sx);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(result.Value().verticalScale), sample.sy);
    }
}

TEST(GameDrawTailTest, FaunaJointCreationRetainsOldPointsAndCountsItsTwoRandoms)
{
    std::array<Client::Scenes::JointState, 1> joints{};
    Client::Scenes::FaunaObject owner;
    owner.active = 1;
    owner.position = {10, 20, 30};
    owner.light = {0.1f, 0.2f, 0.3f};
    glm::vec3 first{1, 2, 3}, second{4, 5, 6}, angles{};
    Client::World::FishEffectRequest request{.type = 0x697,
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
    Client::Render::Models::CrtRandom random(7), expected(7);
    joints[0].points[0][0] = {99, 98, 97};
    ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, random).HasValue());
    EXPECT_EQ(joints[0].life, 120);
    EXPECT_EQ(joints[0].count, -1);
    EXPECT_FLOAT_EQ(joints[0].points[0][0].x, 99);
    EXPECT_EQ(random.State(), expected.State());
    joints[0].alive = false;
    request.type = 0x10f;
    request.subtype = 3;
    const float sway = static_cast<float>(expected.Next() % 500);
    const float speed = static_cast<float>(expected.Next() % 5 + 5);
    ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, random).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(joints[0].texture, 0x613);
    EXPECT_EQ(joints[0].life, 999999);
    EXPECT_FLOAT_EQ(joints[0].velocity.y, sway);
    EXPECT_FLOAT_EQ(joints[0].speed, speed);
    EXPECT_FLOAT_EQ(joints[0].light.y, owner.light.y);
    EXPECT_FALSE(Client::Scenes::StepJoints(joints).HasValue());
    request.ownerContext = nullptr;
    const auto full = Client::Scenes::CreateFaunaJoint(joints, request, random);
    ASSERT_TRUE(full.HasValue());
    EXPECT_FALSE(full.Value().has_value());
    EXPECT_EQ(random.State(), expected.State());
}

// 0x530a2a y 0x5381cb: el alta y el paso del 0x697 no leen el subtipo; el único llamador (0x4473f9) pide el /0, pero
// otro subtipo nace y sigue a su dueño igual, y con el dueño inactivo muere sin la cola ni la vida.
TEST(GameDrawTailTest, ScrollTailOfAnotherSubtypeIsTheSame)
{
    std::array<Client::Scenes::JointState, 1> joints{};
    Client::Scenes::FaunaObject owner;
    owner.active = 1;
    owner.previousPosition = {10, 20, 30};
    glm::vec3 first{1, 2, 3}, second{4, 5, 6}, angles{};
    const Client::World::FishEffectRequest request{.type = 0x697,
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
    Client::Render::Models::CrtRandom random(7);
    ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, random).HasValue());
    auto& joint = joints[0];
    EXPECT_EQ(joint.subtype, 5);
    EXPECT_EQ(joint.life, 120);
    EXPECT_EQ(joint.count, -1);
    EXPECT_EQ(joint.maximumCount, 28);
    EXPECT_EQ(joint.speed, 3.0f);
    EXPECT_EQ(joint.width, 30.0f);
    ASSERT_TRUE(Client::Scenes::StepJoints(joints).HasValue());
    EXPECT_EQ(joint.position, owner.previousPosition);
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.life, 119);
    owner.active = 0;
    owner.previousPosition = {40, 50, 60};
    ASSERT_TRUE(Client::Scenes::StepJoints(joints).HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.position, owner.previousPosition);
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.life, 119);
}

TEST(GameDrawTailTest, CrywolfJointCopiesItsOwnerTrailBeforeGrowingAndExpiring)
{
    std::array<Client::Scenes::JointState, 1> joints{};
    Client::Scenes::FaunaObject owner;
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
    ASSERT_TRUE(Client::Scenes::StepJoints(joints).HasValue());
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.life, 1);
    EXPECT_FLOAT_EQ(joint.position.y, 20);
    EXPECT_FLOAT_EQ(joint.points[0][0].x, 8);
    owner.previousPosition = {40, 50, 60};
    ASSERT_TRUE(Client::Scenes::StepJoints(joints).HasValue());
    EXPECT_EQ(joint.count, 1);
    EXPECT_FLOAT_EQ(joint.points[1][0].x, 8);
    EXPECT_FLOAT_EQ(joint.points[0][0].x, 38);
    EXPECT_TRUE(joint.alive); // life0 sigue vivo; muere al quedar negativo.
    ASSERT_TRUE(Client::Scenes::StepJoints(joints).HasValue());
    EXPECT_FALSE(joint.alive);
}

TEST(GameDrawTailTest, TarkanJointChecksItsOwnerBeforeCopyingTheTrail)
{
    std::array<Client::Scenes::JointState, 1> joints{};
    Client::Scenes::FaunaObject owner;
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
    ASSERT_TRUE(Client::Scenes::StepJoints(joints).HasValue());
    EXPECT_FLOAT_EQ(joint.position.x, 12);
    EXPECT_EQ(joint.count, 1);
    EXPECT_EQ(joint.life, 9);
    owner.active = 0;
    owner.previousPosition = {90, 90, 90};
    ASSERT_TRUE(Client::Scenes::StepJoints(joints).HasValue());
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
    Client::World::Leaves leaves;
    auto& leaf = leaves.All()[0];
    leaf.alive = true;
    leaf.scale = 8;
    const auto view = Client::Render::ViewRows(world.view);
    ASSERT_TRUE(Client::Scenes::DrawGameLeaves(batch, leaves, 2, view).HasValue());
    EXPECT_EQ(batch.State().space, Client::Render::ModelCoordinateSpace::World);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 16);
    const auto transformed = world.view * glm::vec4(batch.Vertices()[0].position, 1);
    EXPECT_FLOAT_EQ(transformed.x, 36); // 621400 no hace LoadIdentity; el primer pase vuelve a aplicar view.
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Client::Scenes::DrawGameLeaves(batch, leaves, 2, view).HasValue());
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 16);
}

TEST(GameDrawTailTest, DamageUnorderedScaleFallsBackToFifteenLikeTheX87Comparison)
{
    Client::Scenes::DamageNumberState damage;
    damage.alive = true;
    damage.scale = std::numeric_limits<float>::quiet_NaN();
    damage.speed = 10;
    Client::Scenes::StepDamage(std::span(&damage, 1));
    EXPECT_FLOAT_EQ(damage.scale, 15);
}

TEST(GameDrawTailTest, IcarusJointOwnerColorFailureKeepsTheEarlierConstructorPrefix)
{
    std::array<Client::Scenes::JointState, 1> joints{};
    glm::vec3 first{}, second{}, angles{};
    Client::World::FishEffectRequest request{.type = 0x610,
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
    Client::Render::Models::CrtRandom random(1);
    const auto created = Client::Scenes::CreateFaunaJoint(joints, request, random);
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
    Client::Scenes::JointState joint;
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
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 0).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.1f);
    batch.Begin(Client::Render::Pass3D{});
    joint.subtype = 3;
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 0).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.2f);
}

TEST(GameDrawTailTest, IcarusJointEmitsTwoRotatedSpritesBeforeDrawingItsFirstFace)
{
    Client::Render::ModelBatch batch;
    Client::Scenes::JointState joint;
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
    Client::World::FrameSprites sprites;
    Client::Render::Models::CrtRandom random(7), expected(7);
    const auto first = expected.Next() % 360;
    const auto second = expected.Next() % 360;
    const Client::Scenes::JointDrawResources resources{random, sprites};
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 0, resources).HasValue());
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
    const Client::Scenes::JointDrawResources intermediate{random, sprites, false};
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 0, intermediate).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_FLOAT_EQ(sprites.Sprites()[0].rotation, static_cast<float>(first));
    EXPECT_FLOAT_EQ(sprites.Sprites()[1].rotation, static_cast<float>(second));
}

TEST(GameDrawTailTest, BloodCastleJointMissingRandomFailsAfterMovementAndOwnerCheck)
{
    Client::Scenes::FaunaObject owner;
    owner.active = 1;
    Client::Scenes::JointState joint;
    joint.alive = true;
    joint.type = 0x10f;
    joint.subtype = 3;
    joint.ownerContext = &owner;
    joint.speed = 2;
    joint.life = 10;
    const auto result = Client::Scenes::StepJoints(std::span(&joint, 1));
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x535f18"), std::string::npos);
    EXPECT_FLOAT_EQ(joint.position.y, -2);
    EXPECT_EQ(joint.life, 10);
}

TEST(GameDrawTailTest, BloodCastleJointStepsItsRandomLifeBeforeTheSineAndOwnerColor)
{
    Client::Scenes::FaunaObject owner;
    owner.active = 1;
    owner.timer = 50;
    owner.light = {0.25f, 0.5f, 0.75f};
    Client::Scenes::JointState joint;
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
    Client::Render::Models::CrtRandom random(7);
    ASSERT_TRUE(Client::Scenes::StepJoints(std::span(&joint, 1), random).HasValue());
    EXPECT_EQ(random.State(), 3289278137u); // rand61%5 !=0; rand17422%2 ==0.
    EXPECT_EQ(joint.life, 10);              // 10+1 antes de sin; -1 al final.
    // Decimal100: sin((11-2)*F32(.05))*3 =1.304896620447595617..., storeF32.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.velocity.x), 0x3fa706dau);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.position.x), 0x3fa706dau);
    EXPECT_EQ(joint.light, owner.light);
    EXPECT_EQ(joint.count, 1);
    EXPECT_FLOAT_EQ(joint.points[1][0].x, 100);
}

TEST(GameDrawTailTest, BloodCastleDeadOwnerStillAddsItsLastTailWithoutRandom)
{
    Client::Scenes::FaunaObject owner;
    Client::Scenes::JointState joint;
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
    ASSERT_TRUE(Client::Scenes::StepJoints(std::span(&joint, 1)).HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, -1);
    EXPECT_EQ(joint.count, 0);
    EXPECT_FLOAT_EQ(joint.points[0][0].x, -1);
    EXPECT_FLOAT_EQ(joint.points[0][0].y, -2);
}

TEST(GameDrawTailTest, IcarusJointCopiesEvenItsDeadOwnerAndPinsTheTailToHeightThreeHundred)
{
    Client::Scenes::FaunaObject owner;
    owner.position = {12, 34, 56};
    Client::Scenes::JointState joint;
    joint.alive = true;
    joint.type = 0x610;
    joint.subtype = 8;
    joint.ownerContext = &owner;
    joint.life = 10;
    joint.tailEnabled = true;
    joint.count = -1;
    joint.maximumCount = 20;
    joint.width = 2;
    ASSERT_TRUE(Client::Scenes::StepJoints(std::span(&joint, 1)).HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, 9);
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.position, glm::vec3(12, 34, 300));
    EXPECT_EQ(joint.points[0][0], glm::vec3(11, 34, 300));
    joint.alive = true;
    joint.ownerContext = nullptr;
    joint.position.x = -0.0f;
    joint.position.y = -0.0f;
    ASSERT_TRUE(Client::Scenes::StepJoints(std::span(&joint, 1)).HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, 8);
    EXPECT_EQ(joint.count, 1);
    EXPECT_TRUE(std::signbit(joint.position.x)); // 535932 omite las sumas cuando speed==0.
    EXPECT_TRUE(std::signbit(joint.position.y));
}

TEST(GameDrawTailTest, GroundModelEffectsRenderBeforeRestoringOpaqueStateWithoutModelLookups)
{
    Client::Render::ModelBatch batch;
    LookupCounter models;
    Client::World::SphereFrustum frustum;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    heights->cells.fill(7);
    const Client::Scenes::ModelEffectDrawResources resources{*heights, 30};
    Client::World::ModelEffect effect;
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
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.z, 12); // altura7 +lift5; no z del efecto.
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

// 0x52d620: el 0x19f y el 0x61c del asedio no tienen caso (ni en 0x52dc20) y no se dibujan; el aura 0x134 va por el
// camino común (0x52da92 → 0x602e90) y busca su BMD (Skill\Aurora).
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
        Client::World::ModelEffect effect;
        effect.alive = true;
        effect.type = type;
        effect.transform.position = {1000, 2000, 300};
        const auto drawn = Client::Scenes::DrawGameModelEffects(
            batch, models, std::span(&effect, 1), Client::World::SphereFrustum{}, nullptr, {}, 0, resources);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_TRUE(effect.seen);
        EXPECT_EQ(models.lookups > 0, type == 0x134);
        EXPECT_TRUE(batch.Vertices().empty());
    }
}

// 0x518d63 limpia +0x16d y no +0x16c: el lugar que vuelve a usar el prefijo conserva lo que vio el último dibujo, y el
// pase del suelo (0x52dc46) saca su rand() aunque el tipo no tenga caso.
TEST(GameDrawTailTest, AReusedSlotKeepsItsVisibilityUntilTheModelPass)
{
    auto effects = std::make_unique<Client::World::ModelEffects>();
    effects->Effects().front().seen = true;
    const auto slot = effects->Begin({.type = 0x19f});
    ASSERT_EQ(slot, 0u);

    Client::Render::Models::CrtRandom random(11);
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    const Client::World::GroundEffectResources resources{.heights = *heights, .random = random, .map = 30};
    Client::Render::ModelBatch batch;
    const auto drawn = Client::Scenes::DrawGroundEffects(batch, effects->Effects(), resources);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    Client::Render::Models::CrtRandom expected(11);
    static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Additive);
}

// 0x52d620: la flecha 0xf8 y el pulso 0x105/3 no se dibujan (0x52d6c3 y 0x52d78f); el fragmento 0xc1a y las piedritas
// 0xc8/0xc9 con el subtipo 1 van por el camino común (0x602e90) con su BMD, en los dos pases.
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
            Client::World::ModelEffect effect;
            effect.alive = true;
            effect.type = type;
            effect.subtype = subtype;
            effect.transform.position = {1000, 2000, 300};
            Client::Render::ModelBatch batch;
            const auto result = Client::Scenes::DrawGameModelEffects(
                batch, models, std::span(&effect, 1), Client::World::SphereFrustum{}, nullptr, {}, 0, mode);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_TRUE(effect.seen);
            EXPECT_TRUE(batch.Vertices().empty());
            // En el segundo pase el filtro (0x52d68c) busca el BMD de todos antes del caso.
            if (mode == 0)
            {
                EXPECT_EQ(models.lookups > 0, drawn);
            }
        }
    }
}

// 0x52d620 con los tipos altos de la CL11: el 0x116, el 0x117, el 0x138, el 0x13c y la piedra 0x12e/3 van por el
// camino común (0x602e90) con su BMD de las skills; el 0x625, los emisores 0x812 y 0x813 y el brillo 0x621/1 (de
// 0x151 en adelante, sin caso) no se dibujan.
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
        Client::World::ModelEffect effect;
        effect.alive = true;
        effect.type = type;
        effect.subtype = subtype;
        effect.transform.position = {1000, 2000, 300};
        Client::Render::ModelBatch batch;
        const auto result = Client::Scenes::DrawGameModelEffects(batch, models, std::span(&effect, 1),
                                                                 Client::World::SphereFrustum{}, nullptr, {}, 0, 0);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(effect.seen);
        EXPECT_TRUE(batch.Vertices().empty());
        EXPECT_EQ(models.lookups > 0, drawn);
    }
}

TEST(GameDrawTailTest, NoriaJointConsumesTwoConstructorRandomsAndMovesBeforeGrowingItsTail)
{
    std::array<Client::Scenes::JointState, 1> joints{};
    joints[0].width = 10;
    joints[0].icarusRotations = std::array<float, 2>{45, 60};
    glm::vec3 position{10, 20, 30}, angles{};
    const Client::World::FishEffectRequest request{.type = 0x617,
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
    Client::Render::Models::CrtRandom random(7);
    ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, random).HasValue());
    auto& joint = joints[0];
    EXPECT_FALSE(joint.icarusRotations.has_value());
    EXPECT_EQ(random.State(), 3289278137u);
    EXPECT_FLOAT_EQ(joint.width, 2);
    EXPECT_FLOAT_EQ(joint.speed, 7);
    EXPECT_EQ(joint.life, 14);
    EXPECT_EQ(joint.maximumCount, 2);
    EXPECT_FLOAT_EQ(joint.points[0][0].x, 5);
    ASSERT_TRUE(Client::Scenes::StepJoints(joints).HasValue());
    EXPECT_EQ(joint.life, 13);
    EXPECT_EQ(joint.position, glm::vec3(10, 13, 30));
    EXPECT_EQ(joint.count, 1);
    EXPECT_EQ(joint.points[0][0], glm::vec3(9, 13, 30));
    EXPECT_FLOAT_EQ(joint.points[1][0].x, 5);
    Client::Render::ModelBatch batch;
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, joints, 0).HasValue());
    EXPECT_EQ(batch.Vertices().size(), 12u);
    EXPECT_EQ(batch.State().texture->value, 0x617);
    EXPECT_EQ(random.State(), 3289278137u);
}

TEST(GameDrawTailTest, FloorStepFallsBeforeClampingAndSettlesItsWeaponWithoutResettingSpeed)
{
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    auto attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(7);
    Client::Scenes::FloorItemState item;
    item.alive = true;
    item.type = 0x212;
    item.position = {100, 200, 200};
    item.dropSpeed = 5;
    item.effectCounter = 1;
    const Client::Scenes::FloorItemStepContext context{*heights, *attributes, random, particles, 0};
    ASSERT_TRUE(Client::Scenes::StepFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_FLOAT_EQ(item.position.z, 205);
    EXPECT_FLOAT_EQ(item.dropSpeed, -1);
    EXPECT_FLOAT_EQ(item.angles.y, 10);
    EXPECT_EQ(item.effectCounter, 2);
    item.position.z = 100;
    ASSERT_TRUE(Client::Scenes::StepFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_FLOAT_EQ(item.position.z, 99); // Sigue en el aire: suelo0 +30 +40.
    EXPECT_FLOAT_EQ(item.dropSpeed, -7);
    EXPECT_FLOAT_EQ(item.angles.y, 70);
    item.position.z = 60;
    ASSERT_TRUE(Client::Scenes::StepFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_FLOAT_EQ(item.position.z, 70);
    EXPECT_FLOAT_EQ(item.dropSpeed, -13); // el contacto no resetea la gravedad.
    EXPECT_EQ(item.angles, glm::vec3(60, 0, -45));
    EXPECT_EQ(random.State(), 7u);
    item.type = 0xe12;
    item.position.z = 200;
    ASSERT_TRUE(Client::Scenes::StepFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_FLOAT_EQ(item.angles.z, 190);
    EXPECT_FLOAT_EQ(item.position.z, 187);
    item.type = 0x1e21;
    item.position.z = 20;
    item.dropSpeed = 0;
    ASSERT_TRUE(Client::Scenes::StepFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_FLOAT_EQ(item.position.z, 30);
}

TEST(GameDrawTailTest, FloorStepTakesItsTwoCallerRandomsEvenWhenTheParticlePoolIsFull)
{
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    auto attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    Client::World::Particles particles;
    for (auto& particle : particles.All())
        particle.alive = true;
    Client::Render::Models::CrtRandom random(7);
    Client::Scenes::FloorItemState item;
    item.alive = true;
    item.type = 0x212;
    item.effectCounter = 48;
    const Client::Scenes::FloorItemStepContext context{*heights, *attributes, random, particles, 0};
    ASSERT_TRUE(Client::Scenes::StepFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_EQ(item.effectCounter, 49);
    EXPECT_EQ(random.State(), 3289278137u);
    EXPECT_TRUE(item.alive);
}

TEST(GameDrawTailTest, FloorStepEmitsBothSparkSubtypesAtTheSameRotatedPoint)
{
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    auto attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(7);
    Client::Scenes::FloorItemState item;
    item.alive = true;
    item.type = 0x212;
    item.position = {100, 200, 10000};
    item.dropSpeed = 6;
    const Client::Scenes::FloorItemStepContext context{*heights, *attributes, random, particles, 0};
    ASSERT_TRUE(Client::Scenes::StepFloorItems(std::span(&item, 1), context).HasValue());
    EXPECT_EQ(item.effectCounter, 1);
    EXPECT_EQ(random.State(), 3289278137u); // R1=61→45,R2=17422→30;542ab9 no sortea para/0,/1.
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
    Client::World::SphereFrustum frustum;
    Client::Scenes::FloorItemState item;
    item.alive = true;
    item.type = 0x212;
    item.scale = 1;
    item.position = {10, 20, 30};
    int poses = 0;
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.onPose = [&poses](const Client::Scenes::DrawnObject& object) -> Client::Core::Expected<void, std::string>
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
    Client::World::SphereFrustum frustum;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Scenes::ModelEffectDrawResources resources{*heights, 0};
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = 0xc2;
    effect.transform.scale = 1;
    int poses = 0;
    resources.onPose = [&poses](const Client::Scenes::DrawnObject&) -> Client::Core::Expected<void, std::string>
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
    resources.onPose = [&poses](const Client::Scenes::DrawnObject&) -> Client::Core::Expected<void, std::string>
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

// 0x52d6c3: el pulso 0x103 del terremoto no tiene caso propio y va por 0x52da84 → 0x602e90 con su malla,
// flashing.bmd (0x615700). Nace con blendMesh 0: en el segundo pase lo filtra 0x52d66a..0x52d693 sólo con −1.
TEST(GameDrawTailTest, TheEarthquakeBurstDrawsFlashingBmdThroughTheGenericPath)
{
    TriangleModels models;
    Client::World::SphereFrustum frustum;
    Client::World::ModelEffect burst;
    burst.alive = true;
    burst.type = Client::World::EarthquakeBurstEffect;
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
    // Sin flashing.bmd la cuenta de mallas es 0 (0x60dbe0): el primer pase no dibuja nada y no corta.
    burst.objectState.blendMesh = 0;
    models.missingModel = static_cast<std::uint16_t>(Client::World::EarthquakeBurstEffect);
    Client::Render::ModelBatch missing;
    const auto withoutModel =
        Client::Scenes::DrawGameModelEffects(missing, models, std::span(&burst, 1), frustum, nullptr, {}, 0, 0);
    ASSERT_TRUE(withoutModel.HasValue()) << withoutModel.Error();
    EXPECT_TRUE(missing.Vertices().empty());
}

// 0x52d620 → 0x52d400: el terremoto 0xfa del caballo (estado 2) no se dibuja ni busca modelos, en ninguna vida. Las
// grietas 0xfb..0x102 van por 0x52da84 → 0x602e90 con su malla; con +0x74 en 0x80 (0xfc, 0xff y 0x102 de subtipo 1),
// 0x6002e0 las dibuja por 0x4ef0c0 con 0x82 (0x6004aa), con la mezcla que oscurece.
TEST(GameDrawTailTest, TheHorseEarthquakeIsInvisibleAndItsCracksDrawTheirMesh)
{
    Client::World::SphereFrustum frustum;
    for (const std::int32_t life : {20, 11, 5})
    {
        SCOPED_TRACE(life);
        LookupCounter models;
        Client::Render::ModelBatch batch;
        Client::World::ModelEffect quake;
        quake.alive = true;
        quake.type = Client::World::EarthquakeEffect;
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
    for (std::int16_t type = Client::World::FirstEarthquakeCrack; type <= Client::World::LastEarthquakeCrack; ++type)
    {
        SCOPED_TRACE(type);
        Client::Render::ModelBatch batch;
        Client::World::ModelEffect crack;
        crack.alive = true;
        crack.type = type;
        crack.subtype = 1;
        crack.life = 35;
        crack.transform.scale = 1.5f;
        const auto drawn =
            Client::Scenes::DrawGameModelEffects(batch, models, std::span(&crack, 1), frustum, nullptr, {}, 0, 0);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(batch.Vertices().size(), 3u);

        crack.renderFlags = Client::World::CrackRenderFlags;
        Client::Render::ModelBatch flagged;
        const auto dark =
            Client::Scenes::DrawGameModelEffects(flagged, models, std::span(&crack, 1), frustum, nullptr, {}, 0, 0);
        ASSERT_TRUE(dark.HasValue()) << dark.Error();
        EXPECT_EQ(flagged.Vertices().size(), 3u);
        ASSERT_FALSE(flagged.Commands().empty());
        EXPECT_EQ(flagged.Commands().back().state.blend, Client::Render::ModelBlend::Subtractive);
    }
}

// 0x6004aa, 0x601da8 y 0x601df6 con los efectos de los magos: el 0xd0 (con +0x74 en 0x80 desde el alta) oscurece; el
// 0xcc/3 y /4 van con el color del modelo en (0,1f; 0,1f; 10) y el pase común; el 0xcb/2 y /3 con (0,5; 0,5; 1) y
// Effect\Magic_b.jpg (0x6e9) en cada malla. El 0xcc/0 y el 0xcb/0, con el color de su luz. Con la luz por vértice
// (+0xe8) y la normal (0, 0, 1), cada esquina va con el color del modelo por 2 (NormalLight: 2 · 0,8 + 0,4, la tabla
// 0x715a98c por el +0x48 de 0x5ffce0).
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
        Case{Client::World::LaserEffect, 0, Client::World::CrackRenderFlags, ModelBlend::Subtractive, Light, Plain},
        Case{Client::World::OuterCircleEffect, 1, Client::World::CrackRenderFlags, ModelBlend::Subtractive, Light,
             Plain},
        Case{Client::World::OuterCircleEffect, 3, 0, ModelBlend::Opaque, deep, Plain},
        Case{Client::World::OuterCircleEffect, 4, 0, ModelBlend::Opaque, deep, Plain},
        Case{Client::World::OuterCircleEffect, 0, 0, ModelBlend::Opaque, Light, Plain},
        Case{Client::World::InnerCircleEffect, 2, 0, ModelBlend::Opaque, {0.5f, 0.5f, 1.0f}, Magic},
        Case{Client::World::InnerCircleEffect, 3, 0, ModelBlend::Opaque, {0.5f, 0.5f, 1.0f}, Magic},
        Case{Client::World::InnerCircleEffect, 0, 0, ModelBlend::Opaque, Light, Plain},
    };
    Client::World::SphereFrustum frustum;
    TriangleModels models;
    for (const Case& tested : cases)
    {
        SCOPED_TRACE(tested.type);
        SCOPED_TRACE(tested.subtype);
        Client::Render::ModelBatch batch;
        Client::World::ModelEffect effect;
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

// 0x6002e0 → 0x442e10 con el 0x13d y el 0x13e, sin RenderBody: la malla 0 con 2, la luz 1 y el color en (1, 1, 1). El
// 0x13d, con el blendMesh −1 (opaca, su textura) y después con el 0 (aditiva) y la textura 0x63c. El 0x13e, una vez con
// el blendMesh 0 y la U en FSIN(T · 0,00008f) · 2,5f, y deja +0x6c en 1; sin el reloj del mundo, corta.
TEST(GameDrawTailTest, TheMeteorRockAndItsShellTakeTheirOwnPasses)
{
    Client::World::SphereFrustum frustum;
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
    Client::World::ModelEffect rock;
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

    Client::World::ModelEffect shell = rock;
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

// 0x4ecf22..0x4ed035 con RenderBody(0x82) (0x6004aa): la malla del blendMesh va por 0x4ed014, que sólo mira la 0x80
// (0x620b10, ZERO / ONE_MINUS_SRC_COLOR); la otra, por 0x4ecf68, también. Con el blendMesh en −2 van las dos por
// 0x4ed014. Sin la 0x80 (el pase común, RenderBody(2)) la del blendMesh es aditiva (0x620a90) y la otra opaca.
TEST(GameDrawTailTest, TheDarkBodyDarkensItsBlendMeshToo)
{
    TwoMeshModels models;
    Client::World::SphereFrustum frustum;
    using Client::Render::ModelBlend;
    struct Case
    {
        std::uint32_t renderFlags;
        std::int16_t blendMesh;
        std::array<ModelBlend, 2> blends;
    };
    for (const Case& tested :
         {Case{Client::World::CrackRenderFlags, 0, {ModelBlend::Subtractive, ModelBlend::Subtractive}},
          Case{Client::World::CrackRenderFlags, -2, {ModelBlend::Subtractive, ModelBlend::Subtractive}},
          Case{0, 0, {ModelBlend::Additive, ModelBlend::Opaque}}})
    {
        SCOPED_TRACE(tested.renderFlags);
        SCOPED_TRACE(tested.blendMesh);
        Client::Render::ModelBatch batch;
        Client::World::ModelEffect laser;
        laser.alive = true;
        laser.type = Client::World::LaserEffect;
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

// 0x600328: con +0x58 en −2 (la mariposa 0xae de la selección, 0x51913a) 0x6002e0 sale después de posar: la
// pose se publica y la malla no se dibuja.
TEST(GameDrawTailTest, AHiddenModelEffectPublishesItsPoseWithoutGeometry)
{
    TriangleModels models;
    Client::World::SphereFrustum frustum;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Scenes::FaunaDrawCarry carry;
    Client::Scenes::ModelEffectDrawResources resources{*heights, 0};
    int poses = 0;
    resources.onPose = [&](const Client::Scenes::DrawnObject& object)
    {
        ++poses;
        return Client::Scenes::PublishFaunaPose(carry, models, object, true);
    };
    resources.posedMeshes = [&]() -> std::span<const Client::Render::Models::SkinnedMesh> { return carry.meshes; };
    for (const bool hidden : {true, false})
    {
        Client::Render::ModelBatch batch;
        Client::World::ModelEffect effect;
        effect.alive = true;
        effect.type = Client::World::ButterflyEffect;
        effect.transform.scale = 1;
        if (hidden)
            effect.objectState.hiddenMesh = Client::World::HiddenObject;
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
    Client::World::SphereFrustum frustum;
    Client::World::ModelEffect effect;
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
    Client::World::SphereFrustum frustum;
    std::array<Client::World::ModelEffect, 2> effects{};
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
    Client::World::SphereFrustum frustum;
    Client::World::ModelEffect effect;
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
    Client::World::SphereFrustum frustum;
    Client::Scenes::FaunaDrawCarry carry;
    carry.meshes.resize(1);
    carry.meshes[0].positions.assign(3, glm::vec3(42));
    std::array<Client::Scenes::FloorItemState, 2> items{};
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        items[i].alive = true;
        items[i].type = static_cast<std::int16_t>(0x212 + i);
        items[i].scale = 1;
    }
    items[1].alpha = 1;
    items[1].position = {1000, 1000, 300};
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.onPose = [&](const Client::Scenes::DrawnObject& object)
    {
        auto published = Client::Scenes::PublishFaunaFloorPose(carry, models, object);
        if (object.model == 0x212)
        {
            EXPECT_EQ(carry.meshes[0].positions[0], glm::vec3(42)); // alpha0 posa bones, no skin.
        }
        return published;
    };
    context.posedMeshes = [&]() -> std::span<const Client::Render::Models::SkinnedMesh> { return carry.meshes; };
    context.project = [](const glm::vec3&) -> Client::Core::Expected<glm::ivec2, std::string> { return glm::ivec2{}; };
    ASSERT_TRUE(Client::Scenes::DrawFloorItems(items, context).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_EQ(batch.Vertices()[0].position, glm::vec3(1007, 1008, 309));
}

TEST(GameDrawTailTest, ModelEffectBodyUsesTheDummyBonePublishedByThePrecedingModel)
{
    Client::Render::ModelBatch batch;
    RetainedDummyModels models;
    Client::World::SphereFrustum frustum;
    Client::Scenes::FaunaDrawCarry carry;
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    std::array<Client::World::ModelEffect, 2> effects{};
    for (std::size_t i = 0; i < effects.size(); ++i)
    {
        effects[i].alive = true;
        effects[i].type = static_cast<std::int16_t>(0xc2 + i);
        effects[i].transform.scale = 1;
    }
    effects[1].transform.position = {1000, 1000, 300};
    Client::Scenes::ModelEffectDrawResources resources{*heights, 0};
    resources.onPose = [&](const Client::Scenes::DrawnObject& object)
    { return Client::Scenes::PublishFaunaPose(carry, models, object, true); };
    resources.posedMeshes = [&]() -> std::span<const Client::Render::Models::SkinnedMesh> { return carry.meshes; };
    ASSERT_TRUE(
        Client::Scenes::DrawGameModelEffects(batch, models, effects, frustum, nullptr, {}, 0, resources).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_EQ(batch.Vertices()[3].position, glm::vec3(1007, 1008, 309));
}

TEST(GameDrawTailTest, LegacyModelEffectsConsumeTheSamePublishedDummyBone)
{
    Client::Render::ModelBatch batch;
    RetainedDummyModels models;
    Client::World::SphereFrustum frustum;
    Client::Scenes::FaunaDrawCarry carry;
    std::array<Client::World::ModelEffect, 2> effects{};
    for (std::size_t i = 0; i < effects.size(); ++i)
    {
        effects[i].alive = true;
        effects[i].type = static_cast<std::int16_t>(0xc2 + i);
        effects[i].transform.scale = 1;
    }
    effects[1].transform.position = {1000, 1000, 300};
    const Client::Scenes::TailPoseObserver onPose = [&](const Client::Scenes::DrawnObject& object)
    { return Client::Scenes::PublishFaunaPose(carry, models, object, true); };
    const Client::Scenes::TailMeshProvider posedMeshes = [&]() -> std::span<const Client::Render::Models::SkinnedMesh>
    { return carry.meshes; };
    ASSERT_TRUE(Client::Scenes::DrawModelEffects(batch, models, effects, frustum, nullptr, {}, 0, onPose, posedMeshes)
                    .HasValue());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_EQ(batch.Vertices()[3].position, glm::vec3(1007, 1008, 309));
}

TEST(GameDrawTailTest, PublishedFloorBodyReportsMissingAndIncompleteScratchBeforeProjection)
{
    Client::Render::ModelBatch batch;
    TriangleModels models;
    Client::World::SphereFrustum frustum;
    Client::Scenes::FloorItemState item;
    item.alive = true;
    item.type = 0x212;
    item.alpha = 1;
    item.screenX = 1234;
    int poses = 0;
    bool projected = false;
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.onPose = [&](const Client::Scenes::DrawnObject&) -> Client::Core::Expected<void, std::string>
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
    context.posedMeshes = []() -> std::span<const Client::Render::Models::SkinnedMesh> { return {}; };
    result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("no cubre"), std::string::npos);
    EXPECT_EQ(poses, 2);
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_FALSE(projected);
}
