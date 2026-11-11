#include "Scenes/FaunaDrawing.h"

#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/TextureSlot.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaObject.h"
#include "Scenes/FaunaState.h"
#include "World/WorldObjects.h"
#include "World/Sprites.h"
#include "World/TerrainCulling.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include "support/SyntheticModels.h"

#include <gtest/gtest.h>

#include <memory>
#include <cstdint>
#include <cstddef>
#include <span>

namespace
{
class Models : public Client::Scenes::IModelLibrary
{
public:
    Client::Data::Models::Model model = Client::Tests::SampleModel();
    Client::Render::Models::Skeleton skeleton;
    Models()
    {
        auto loaded = Client::Render::Models::MakeSkeleton(model);
        if (loaded.HasValue())
            skeleton = loaded.Value();
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
};
class FaunaDrawingTest : public ::testing::Test
{
protected:
    Client::Scenes::FaunaState m_fauna;
    Models m_models;
    Client::World::WorldTerrain m_terrain;
    Client::World::TerrainLighting m_light;
    Client::World::TerrainPolygon m_polygon{.everything = true};
    Client::Render::Models::ClothWorld m_cloth;
    Client::World::FrameSprites m_sprites;
    Client::Scenes::FaunaDrawCarry m_carry;
    Client::Render::ModelBatch m_batch;
    Client::Scenes::FaunaDrawContext Context()
    {
        return {m_fauna, m_models, m_terrain, m_light, m_polygon, m_cloth, m_sprites, m_carry, 0, 0, {}};
    }
    void SetUp() override
    {
        m_terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        m_terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    }
    void Live(Client::Scenes::FaunaObject& object, std::int16_t type)
    {
        object.active = 1;
        object.type = type;
        object.bodyScale = 1;
        object.alpha = 1;
        object.hiddenMesh = object.blendMesh = -1;
        object.light = glm::vec3(.5f);
        object.position = {1000, 1000, 300};
    }
};
TEST_F(FaunaDrawingTest, DeadSlotsStillReceiveTheTwoRoundedYawStores)
{
    m_fauna.Birds()[0].angles.z = .000001f;
    m_fauna.Birds()[1].type = 0x10f;
    m_fauna.Birds()[1].angles.z = .000001f;
    auto context = Context();
    ASSERT_TRUE(DrawGameBirds(m_batch, context).HasValue());
    EXPECT_NE(m_fauna.Birds()[0].angles.z, .000001f);
    EXPECT_FLOAT_EQ(m_fauna.Birds()[1].angles.z, .000001f);
    m_fauna.Fish()[0].type = 0x10f;
    m_fauna.Fish()[0].angles.z = .000001f;
    ASSERT_TRUE(DrawGameFish(m_batch, context).HasValue());
    EXPECT_NE(m_fauna.Fish()[0].angles.z, .000001f);
    EXPECT_EQ(m_cloth.random.State(), 1U);
}
TEST_F(FaunaDrawingTest, AVisibleBirdDrawsItsOwnMeshesAndAFifthAlphaShadow)
{
    Live(m_fauna.Birds()[0], 0xad);
    auto context = Context();
    const auto result = DrawGameBirds(m_batch, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_fauna.Birds()[0].seen, 1);
    bool shadow = false;
    for (const auto& command : m_batch.Commands())
        if (command.state.blend == Client::Render::ModelBlend::BlendedShadow)
        {
            shadow = true;
            for (std::size_t i = command.firstVertex; i < command.firstVertex + command.vertexCount; ++i)
                EXPECT_FLOAT_EQ(m_batch.Vertices()[i].color.alpha, .2f);
        }
    EXPECT_TRUE(shadow);
    EXPECT_EQ(m_cloth.random.State(), 1U);
}
TEST_F(FaunaDrawingTest, FishTypesB9AndBaDoNotEmitShadowsOrRandomDraws)
{
    Live(m_fauna.Fish()[0], 0xb9);
    auto context = Context();
    const auto result = DrawGameFish(m_batch, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_fauna.Fish()[0].seen, 1);
    for (const auto& command : m_batch.Commands())
        EXPECT_NE(command.state.blend, Client::Render::ModelBlend::BlendedShadow);
    EXPECT_EQ(m_cloth.random.State(), 1U);
}
TEST_F(FaunaDrawingTest, ButterflySpendsOneSharedDrawAndCreatesOneSprite)
{
    Live(m_fauna.Birds()[0], 0xae);
    auto context = Context();
    Client::Render::Models::CrtRandom expected;
    static_cast<void>(expected.Next());
    const auto result = DrawGameBirds(m_batch, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_cloth.random.State(), expected.State());
    EXPECT_TRUE(m_sprites.Sprites()[0].alive);
    EXPECT_EQ(m_sprites.Sprites()[0].texture, 0x5ac);
    EXPECT_FALSE(m_sprites.Sprites()[1].alive);
}

TEST_F(FaunaDrawingTest, ABodyUsesTheRetainedDummyBoneInsteadOfARecreatedIdentity)
{
    Live(m_fauna.Birds()[0], 0xad);
    m_carry.bones.resize(3, Client::Render::Models::BoneTransform(0));
    m_carry.bones[2][3] = {7, 8, 9};
    auto context = Context();
    const auto result = DrawGameBirds(m_batch, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_FALSE(m_batch.Vertices().empty());
    EXPECT_FLOAT_EQ(m_batch.Vertices()[0].position.x, 1007);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[0].position.y, 1008);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[0].position.z, 309);
}

TEST_F(FaunaDrawingTest, AnIntermediateFrameDrawsWithoutRepeatingNativeMutationsOrRandom)
{
    Live(m_fauna.Birds()[0], 0xae);
    m_fauna.Birds()[0].angles.z = .000001f;
    auto context = Context();
    context.originalFrame = false;
    const auto result = DrawGameBirds(m_batch, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FLOAT_EQ(m_fauna.Birds()[0].angles.z, .000001f);
    EXPECT_EQ(m_fauna.Birds()[0].seen, 0);
    EXPECT_EQ(m_cloth.random.State(), 1U);
    EXPECT_FALSE(m_sprites.Any());
    EXPECT_FALSE(m_batch.Vertices().empty());
}

} // namespace

namespace
{
TEST_F(FaunaDrawingTest, AShadowPublishesTheRetainedOriginUsedByTheNextTransparentBcGlow)
{
    Live(m_fauna.Birds()[0], 0xbc);
    Live(m_fauna.Birds()[1], 0xbc);
    m_fauna.Birds()[1].alpha = 0;
    m_fauna.Birds()[1].bodyScale = 7;
    m_fauna.Birds()[1].position = {2000, 2100, 900};
    auto context = Context();
    context.map = 11;
    const auto drawn = DrawGameBirds(m_batch, context);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_TRUE(m_sprites.Sprites()[2].alive);
    // El primer sprite ya tuvo spillF32 al sumar300; no restarlo para reconstruir su componente local.
    const auto expected = Client::Render::Models::OriginalTransformPosition(
        Client::Render::Models::OriginalMatrixOf(m_carry.bones[1]), {-5, 0, 0}, 1, {1000, 1000, 0});
    EXPECT_EQ(m_sprites.Sprites()[2].position, expected);
    ASSERT_TRUE(m_carry.models.contains(0xbc));
    EXPECT_EQ(m_carry.models.at(0xbc).position, glm::vec3(2000, 2100, 0));
    EXPECT_FLOAT_EQ(m_carry.models.at(0xbc).scale, 1);
}
TEST_F(FaunaDrawingTest, AShadowPublishesItsOriginEvenWhenTheModelHasNoMeshes)
{
    Live(m_fauna.Birds()[0], 0xad);
    m_fauna.Birds()[0].alpha = 0;
    m_models.model.meshes.clear();
    m_carry.models[0xad].position = {11, 22, 99};
    m_carry.models[0xad].scale = 4;
    auto context = Context();
    ASSERT_TRUE(DrawGameBirds(m_batch, context).HasValue());
    EXPECT_EQ(m_carry.models.at(0xad).position, glm::vec3(1000, 1000, 0));
    EXPECT_FLOAT_EQ(m_carry.models.at(0xad).scale, 4);
}
TEST_F(FaunaDrawingTest, AProjectedStormBirdKeepsBothPureShinePassesWithoutCreatingEffects)
{
    Live(m_fauna.Birds()[0], 0x171);
    m_fauna.Birds()[0].counter = 1;
    auto context = Context();
    context.map = 10;
    ASSERT_TRUE(DrawGameBirds(m_batch, context).HasValue());
    const auto commands = m_batch.Commands().size();
    const auto vertices = m_batch.Vertices().size();
    Client::Render::ModelBatch projected;
    context.originalFrame = false;
    const auto random = m_cloth.random.State();
    ASSERT_TRUE(DrawGameBirds(projected, context).HasValue());
    EXPECT_EQ(projected.Commands().size(), commands);
    EXPECT_EQ(projected.Vertices().size(), vertices);
    EXPECT_EQ(m_cloth.random.State(), random);
    EXPECT_FALSE(m_sprites.Any());
}
} // namespace

namespace
{
TEST_F(FaunaDrawingTest, DurableFixedHeightFeedsBirdAndFishShadowsAndTheirModelOrigin)
{
    m_terrain.attributes->cells.fill(0x40);
    Live(m_fauna.Birds()[0], 0xad);
    Live(m_fauna.Fish()[0], 0xb2);
    auto context = Context();
    context.fixedHeight = 1201;
    ASSERT_TRUE(DrawGameBirds(m_batch, context).HasValue());
    ASSERT_TRUE(DrawGameFish(m_batch, context).HasValue());
    EXPECT_FLOAT_EQ(m_carry.models.at(0xad).position.z, 1201);
    EXPECT_FLOAT_EQ(m_carry.models.at(0xb2).position.z, 1201);
    for (const auto& command : m_batch.Commands())
    {
        if (command.state.blend == Client::Render::ModelBlend::BlendedShadow)
        {
            for (std::size_t i = command.firstVertex; i < command.firstVertex + command.vertexCount; ++i)
            {
                EXPECT_FLOAT_EQ(m_batch.Vertices()[i].position.z, 1206);
            }
        }
    }
}
TEST_F(FaunaDrawingTest, TransparentFloorItemsPublishBonesWithoutReplacingTheMeshScratch)
{
    Client::Scenes::DrawnObject item;
    item.model = 0xad;
    item.alpha = 0;
    item.transform.position = {1000, 2000, 3000};
    item.transform.scale = 3;
    m_carry.meshes.resize(1);
    m_carry.meshes[0].positions = {{7, 8, 9}};
    ASSERT_TRUE(Client::Scenes::PublishFaunaFloorPose(m_carry, m_models, item).HasValue());
    ASSERT_FALSE(m_carry.bones.empty());
    EXPECT_EQ(m_carry.meshes[0].positions.front(), glm::vec3(7, 8, 9));
    EXPECT_EQ(m_carry.models.at(0xad).position, item.transform.position);
}
} // namespace

namespace
{
TEST_F(FaunaDrawingTest, AProjectedFrameKeepsItsPosesInTheBorrowedFrameScratch)
{
    Live(m_fauna.Birds()[0], 0xad);
    auto context = Context();
    context.originalFrame = false;
    context.projectedCarry = true;
    ASSERT_TRUE(DrawGameBirds(m_batch, context).HasValue());
    EXPECT_TRUE(m_carry.models.contains(0xad));
    EXPECT_FALSE(m_carry.meshes.empty());
    EXPECT_EQ(m_fauna.Birds()[0].seen, 0);
    EXPECT_EQ(m_cloth.random.State(), 1U);
}

} // namespace

namespace
{
TEST_F(FaunaDrawingTest, CharacterBodiesInitializeDummyMatricesFromTheSharedScratchBeforePosing)
{
    m_carry.bones.resize(3, Client::Render::Models::BoneTransform(0));
    m_carry.bones[2][3] = {7, 8, 9};
    Client::Scenes::DrawnCharacter character;
    character.object.position = {1000, 1000, 300};
    character.castsShadow = false;
    character.poseBones = [&](std::uint16_t)
    { return std::span<const Client::Render::Models::BoneTransform>(m_carry.bones); };
    character.onPose = [&](std::uint16_t model, std::span<const Client::Render::Models::BoneTransform> bones,
                           const Client::Render::Models::ObjectTransform& transform, bool translated)
    { EXPECT_TRUE(PublishFaunaPoseRaw(m_carry, m_models, model, bones, transform, translated).HasValue()); };
    DrawCharacter(m_batch, m_models, character, {});
    ASSERT_FALSE(m_batch.Vertices().empty());
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(1007, 1008, 309));
    EXPECT_EQ(m_carry.bones[2][3], glm::vec3(7, 8, 9));
}
} // namespace
