#include "Core/Expected.h"
#include "Core/Log.h"
#include "Core/ILogSink.h"
#include "Scenes/GameModels.h"
#include "Scenes/FaunaDrawing.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"
#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/Skinning.h"
#include "Render/Models/Cloth.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "World/SphereFrustum.h"
#include "World/Sprites.h"
#include "World/ObjectModelStepState.h"
#include "World/IFishStepServices.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_int2.hpp>
#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <map>
#include <chrono>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

namespace
{
class FloorModels final : public Client::Scenes::IModelLibrary
{
public:
    FloorModels()
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
    std::array<Client::Render::TextureSlot, 3> textures{
        Client::Render::TextureSlot{0x400}, Client::Render::TextureSlot{0x401}, Client::Render::TextureSlot{0x402}};
};
Client::Scenes::FloorItemState Item(std::int16_t type)
{
    Client::Scenes::FloorItemState item;
    item.alive = true;
    item.type = type;
    item.scale = 1;
    item.alpha = 1;
    item.light = {1, 1, 1};
    return item;
}
Client::Core::Expected<glm::ivec2, std::string> Project(const glm::vec3&)
{
    return glm::ivec2(65537, -65538);
}
} // namespace

TEST(GameFloorItemDrawingTest, CommonWeaponAndConsumableFamiliesRenderAndWrapProjectedShorts)
{
    FloorModels models;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    const std::array<std::int16_t, 8> types{0x412, 0x612, 0x812, 0xa12, 0xc12, 0xe12, 0x1c12, 0x1e12};
    for (const auto type : types)
    {
        auto item = Item(type);
        Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
        context.project = Project;
        const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(item.seen);
        EXPECT_EQ(item.screenX, 1);
        EXPECT_EQ(item.screenY, -2);
    }
    EXPECT_EQ(batch.Vertices().size(), 24u);
}

TEST(GameFloorItemDrawingTest, TransparentArmorPosesPlayerWithFamilyRootHeight)
{
    FloorModels models;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    const std::array<std::int16_t, 5> types{0x1012, 0x1212, 0x1412, 0x1612, 0x1812};
    const std::array<float, 5> heights{-160, -100, -50, -70, 0};
    for (std::size_t index = 0; index < types.size(); ++index)
    {
        auto item = Item(types[index]);
        item.alpha = 0;
        int calls = 0;
        Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
        context.project = Project;
        context.onPose = [&](const Client::Scenes::DrawnObject& object) -> Client::Core::Expected<void, std::string>
        {
            ++calls;
            EXPECT_EQ(object.model, 0x20a);
            EXPECT_FLOAT_EQ(object.transform.lockedRootHeight, heights[index]);
            return {};
        };
        const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(calls, 1);
    }
    EXPECT_TRUE(batch.Vertices().empty());
}

TEST(GameFloorItemDrawingTest, ItemLevelSevenAddsChromeGeometry)
{
    FloorModels models;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    auto item = Item(0x212);
    item.value = 7 << 3;
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.project = Project;
    const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.8f);
    EXPECT_EQ(batch.Commands().back().state.texture, Client::Scenes::ChromeTextureSlot);
}

TEST(GameFloorItemDrawingTest, SwordTwentyUsesTwoMeshZeroPassesBeforeMeshOne)
{
    FloorModels models;
    models.model.meshes.push_back(models.model.meshes[0]);
    models.model.meshes[1].texture = 1;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    auto item = Item(0x225);
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.project = Project;
    const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(batch.Vertices().size(), 9u);
    ASSERT_EQ(batch.Commands().size(), 3u);
    EXPECT_EQ(batch.Commands()[1].state.blend, Client::Render::ModelBlend::Additive);
}

TEST(GameFloorItemDrawingTest, KalimaSkinUsesWaterButProjectionUsesRestoredPosition)
{
    FloorModels models;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    auto item = Item(0x612);
    item.position = {100, 200, 300};
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.map = 24;
    context.waterHeight = [](float x, float y) -> Client::Core::Expected<float, std::string>
    {
        EXPECT_FLOAT_EQ(x, 100);
        EXPECT_FLOAT_EQ(y, 200);
        return 600;
    };
    context.project = [](const glm::vec3& point) -> Client::Core::Expected<glm::ivec2, std::string>
    {
        EXPECT_EQ(point, glm::vec3(100, 200, 330));
        return glm::ivec2{};
    };
    const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.z, 780);
    EXPECT_EQ(item.position, glm::vec3(100, 200, 300));
}

TEST(GameFloorItemDrawingTest, CoinsReuseOnePoseAndTheExplicitTableBeforeTheCenter)
{
    FloorModels models;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    auto item = Item(0x1e21);
    item.value = 64;
    std::array<std::int32_t, 100> table{};
    table.fill(7);
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.randomTable = table;
    context.project = Project;
    const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(batch.Vertices().size(), 12u);
    EXPECT_NE(batch.Vertices()[0].position, glm::vec3{});
    EXPECT_EQ(batch.Vertices()[9].position, glm::vec3{});
    EXPECT_EQ(item.position, glm::vec3{});
}

TEST(GameFloorItemDrawingTest, MissingCoinTableKeepsThePosePrefixWithoutInventingOffsets)
{
    FloorModels models;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    auto item = Item(0x1e21);
    int poses = 0;
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.onPose = [&](const Client::Scenes::DrawnObject&) -> Client::Core::Expected<void, std::string>
    {
        ++poses;
        return {};
    };
    context.project = Project;
    const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x608bf5"), std::string::npos);
    EXPECT_EQ(poses, 1);
    EXPECT_TRUE(item.seen);
    EXPECT_TRUE(batch.Vertices().empty());
}

namespace
{
class GoldenGameFloorItemDrawing : public Client::Tests::GoldenDataTest
{
};
} // namespace

TEST_F(GoldenGameFloorItemDrawing, RealWeaponsArmorAndMoneyUseTheSharedPoseAndTheirLoadedTextures)
{
    Client::Tests::FakeTextureStore textures;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Client::Scenes::GameModels models;
    const auto loaded = models.Load(m_dataRoot, textures, log);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    Client::Scenes::FaunaDrawCarry carry;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    const std::array<std::int16_t, 5> types{0x225, 0x612, 0x1012, 0x1812, 0x1e21};
    std::array<std::int32_t, 100> table{};
    for (const auto map : {0, 2, 10, 24, 36})
        for (const auto type : types)
        {
            auto item = Item(type);
            Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
            context.map = map;
            context.worldClock = 1250;
            context.randomTable = table;
            context.waterHeight = [](float, float) -> Client::Core::Expected<float, std::string> { return 20; };
            context.project = Project;
            context.onPose = [&](const Client::Scenes::DrawnObject& object)
            { return Client::Scenes::PublishFaunaFloorPose(carry, models, object); };
            context.onSkin = [&](const Client::Scenes::DrawnObject& object)
            { return Client::Scenes::TransformFaunaPose(carry, models, object); };
            context.posedMeshes = [&]() -> std::span<const Client::Render::Models::SkinnedMesh>
            { return carry.meshes; };
            const auto before = batch.Vertices().size();
            const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
            ASSERT_TRUE(result.HasValue()) << result.Error() << " tipo " << type << " mapa " << map;
            EXPECT_GT(batch.Vertices().size(), before) << "tipo " << type << " mapa " << map;
        }
}

TEST(GameFloorItemDrawingTest, HelpersOverrideThePackedItemLevelBeforeChoosingTheirPasses)
{
    FloorModels models;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    const std::array<std::int16_t, 4> types{0x1a13, 0x1a19, 0x1e1f, 0x1e1e};
    const std::array<std::size_t, 4> vertices{3, 9, 6, 6};
    for (std::size_t index = 0; index < types.size(); ++index)
    {
        auto item = Item(types[index]);
        item.value = index == 0 ? 13 << 3 : 0;
        const auto before = batch.Vertices().size();
        Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
        context.project = Project;
        const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(batch.Vertices().size() - before, vertices[index]);
    }
}

TEST(GameFloorItemDrawingTest, DisabledGlowDetailSuppressesTheExcellentOptionPass)
{
    FloorModels models;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    auto item = Item(0x212);
    item.excellent = 1;
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.glowDetail = 0;
    context.project = Project;
    const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(batch.Vertices().size(), 3u);
}

TEST(GameFloorItemDrawingTest, AncientLowPulseDrawConsumesOneNativeRandomAndPublishesTheModelSelection)
{
    FloorModels models;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    auto item = Item(0x212);
    item.ancient = 1;
    Client::Render::Models::CrtRandom random;
    auto expected = random;
    const auto selection = expected.Next() % 100;
    std::map<std::uint16_t, std::int32_t> selections;
    Client::Scenes::ItemSpecialDrawContext special;
    special.random = &random;
    Client::Scenes::ItemDrawContext items;
    items.special = &special;
    items.selections = &selections;
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.frameClock = std::chrono::milliseconds(4700);
    context.worldClock = 4700;
    context.itemContext = &items;
    context.project = Project;
    const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_TRUE(selections.contains(0x212));
    EXPECT_EQ(selections.at(0x212), selection);
}

TEST(GameFloorItemDrawingTest, SpecialItemFamiliesChooseTheirNativeEarlyPasses)
{
    FloorModels models;
    models.model.meshes.resize(3, models.model.meshes[0]);
    const std::array<std::int16_t, 6> types{0x1e2d, 0x2245, 0x2248, 0x2240, 0x1c23, 0x1c21};
    const std::array<std::uint8_t, 6> levels{3, 0, 2, 13, 0, 0};
    const std::array<std::size_t, 6> counts{15, 18, 18, 18, 18, 18};
    for (std::size_t index = 0; index < types.size(); ++index)
    {
        Client::Render::ModelBatch batch;
        Client::World::SphereFrustum frustum;
        auto item = Item(types[index]);
        item.value = levels[index] << 3;
        Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
        context.project = Project;
        const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(batch.Vertices().size(), counts[index]) << types[index];
    }
}

TEST(GameFloorItemDrawingTest, GlowItemsPublishTheirSpritesFromTheRetainedBones)
{
    FloorModels models;
    models.skeleton.bones.resize(11, models.skeleton.bones[0]);
    const std::array<std::int16_t, 5> types{0x2246, 0x2247, 0x1e24, 0x1e25, 0x1e27};
    const std::array<std::size_t, 5> counts{1, 1, 2, 2, 1};
    for (std::size_t index = 0; index < types.size(); ++index)
    {
        Client::Render::ModelBatch batch;
        Client::World::SphereFrustum frustum;
        Client::World::FrameSprites sprites;
        Client::Scenes::FaunaDrawCarry carry;
        auto item = Item(types[index]);
        Client::Scenes::ItemSpecialDrawContext special;
        special.sprites = &sprites;
        Client::Scenes::ItemDrawContext items;
        items.special = &special;
        items.poseBones = [&]() -> std::span<const Client::Render::Models::BoneTransform> { return carry.bones; };
        Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
        context.itemContext = &items;
        context.project = Project;
        context.onPose = [&](const Client::Scenes::DrawnObject& object)
        { return Client::Scenes::PublishFaunaFloorPose(carry, models, object); };
        context.onSkin = [&](const Client::Scenes::DrawnObject& object)
        { return Client::Scenes::TransformFaunaPose(carry, models, object); };
        context.posedMeshes = [&]() -> std::span<const Client::Render::Models::SkinnedMesh> { return carry.meshes; };
        const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(static_cast<std::size_t>(
                      std::ranges::count_if(sprites.Sprites(), [](const auto& sprite) { return sprite.alive; })),
                  counts[index]);
    }
}

TEST(GameFloorItemDrawingTest, ItemClockTruncationUsesTheLowWordOfTheNativeInt64)
{
    FloorModels models;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    Client::World::FrameSprites sprites;
    auto item = Item(0x2247);
    Client::Scenes::ItemSpecialDrawContext special;
    special.sprites = &sprites;
    Client::Scenes::ItemDrawContext items;
    items.special = &special;
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.itemContext = &items;
    context.worldClock = 0x1p32f;
    context.project = Project;
    const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FLOAT_EQ(item.objectState.uv.y, 0);
}

TEST(GameFloorItemDrawingTest, HelperBeamsUseTheRetainedBonesAndKeepTheDurableOwner)
{
    FloorModels models;
    models.skeleton.bones.resize(31, models.skeleton.bones[0]);
    for (std::size_t bone = 0; bone < models.skeleton.bones.size(); ++bone)
        models.skeleton.bones[bone].actions[0][0].position.x = static_cast<float>(bone);
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    Client::World::FrameSprites sprites;
    auto item = Item(0x1a18);
    item.scale = 2;
    item.position = {100, 200, 300};
    std::size_t requests = 0;
    Client::Scenes::ItemSpecialDrawContext special;
    special.sprites = &sprites;
    Client::Scenes::ItemDrawContext items;
    items.special = &special;
    items.createJoint =
        [&](const Client::World::FishEffectRequest& request) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(request.ownerContext, &item);
        EXPECT_EQ(request.type, requests % 2 == 0 ? 0x614 : 0x613);
        EXPECT_EQ(request.subtype, requests % 2 == 0 ? 14 : 4);
        if (requests == 0)
        {
            EXPECT_EQ(request.firstPosition, glm::vec3(160, 200, 300));
            EXPECT_EQ(request.secondPosition, glm::vec3(144, 200, 300));
        }
        ++requests;
        return {};
    };
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.itemContext = &items;
    context.project = Project;
    auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(requests, 20u);
    EXPECT_EQ(std::ranges::count_if(sprites.Sprites(), [](const auto& sprite) { return sprite.alive; }), 10);
    items.originalFrame = false;
    sprites.Clear();
    result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(requests, 20u);
    EXPECT_EQ(std::ranges::count_if(sprites.Sprites(), [](const auto& sprite) { return sprite.alive; }), 10);
}

TEST(GameFloorItemDrawingTest, FailedWaterSkinKeepsItsTemporaryPositionAndTheNextSlotUntouched)
{
    FloorModels models;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    std::array<Client::Scenes::FloorItemState, 2> items{Item(0x212), Item(0x612)};
    items[0].position.z = 300;
    items[1].screenX = 17;
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.map = 24;
    context.project = Project;
    context.waterHeight = [](float, float) -> Client::Core::Expected<float, std::string> { return 600; };
    context.onSkin = [](const Client::Scenes::DrawnObject&) -> Client::Core::Expected<void, std::string>
    { return Client::Core::Unexpected(std::string("skin interrumpido")); };
    const auto result = Client::Scenes::DrawFloorItems(items, context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), "skin interrumpido");
    EXPECT_FLOAT_EQ(items[0].position.z, 780);
    EXPECT_TRUE(items[0].seen);
    EXPECT_FALSE(items[1].seen);
    EXPECT_EQ(items[1].screenX, 17);
}

TEST(GameFloorItemDrawingTest, WaterCannotReuseTheUnshiftedMeshWithoutTheSkinBinding)
{
    FloorModels models;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    Client::Render::Models::SkinnedMesh mesh;
    mesh.positions.assign(3, glm::vec3{});
    mesh.normals.assign(1, glm::vec3(0, 0, 1));
    auto item = Item(0x212);
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.map = 24;
    context.project = Project;
    context.waterHeight = [](float, float) -> Client::Core::Expected<float, std::string> { return 600; };
    context.onPose = [](const Client::Scenes::DrawnObject&) -> Client::Core::Expected<void, std::string> { return {}; };
    context.posedMeshes = [&]() -> std::span<const Client::Render::Models::SkinnedMesh> { return {&mesh, 1}; };
    const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x60d186"), std::string::npos);
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_FLOAT_EQ(item.position.z, 780);
}

TEST(GameFloorItemDrawingTest, MissingAncientRandomPreservesAllThePrecedingLevelPasses)
{
    FloorModels models;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    auto item = Item(0x212);
    item.value = 13 << 3;
    item.ancient = 1;
    Client::Scenes::ItemDrawContext items;
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.itemContext = &items;
    context.worldClock = 4700;
    context.frameClock = std::chrono::milliseconds(4700);
    context.project = Project;
    const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x60cc29"), std::string::npos);
    EXPECT_EQ(batch.Vertices().size(), 12u);
}

TEST(GameFloorItemDrawingTest, ShinePeriodsWrapTheNativeClockWithoutResettingItsSinePhase)
{
    FloorModels models;
    Client::World::SphereFrustum frustum;
    const auto sine = Client::Render::Models::Detail::SineProductPC53(static_cast<double>(0x1p32f) * 0.002f, 1);
    ASSERT_TRUE(sine.has_value());
    const float phase = static_cast<float>(*sine);
    const std::array<int, 3> levels{7, 11, 13};
    const std::array<glm::vec2, 3> expected{glm::vec2(0.5f, 0), glm::vec2(0, -1.2f),
                                            glm::vec2(static_cast<float>(static_cast<double>(phase) * 3 + 1), -0.5f)};
    for (std::size_t index = 0; index < levels.size(); ++index)
    {
        Client::Render::ModelBatch batch;
        auto item = Item(0x212);
        item.value = levels[index] << 3;
        Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
        context.frameClock = std::chrono::milliseconds(4294967296LL);
        context.worldClock = 0x1p32f;
        context.project = Project;
        const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_GE(batch.Vertices().size(), 6u);
        EXPECT_FLOAT_EQ(batch.Vertices()[3].uv.x, expected[index].x);
        EXPECT_FLOAT_EQ(batch.Vertices()[3].uv.y, expected[index].y);
    }
}

TEST(GameFloorItemDrawingTest, TheBasePassReadsTheSharedUnlitSelectionBeforeDrawing)
{
    FloorModels models;
    models.model.meshes[0].normals[0].normal = {0, 0, -1};
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    Client::World::ObjectModelStepState state;
    state.unlitMeshes[0x212] = 0;
    auto item = Item(0x212);
    item.vertexLight = true;
    item.objectState.uv = {0.25f, 0.5f};
    Client::Scenes::ItemDrawContext items;
    items.modelState = &state;
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.itemContext = &items;
    context.project = Project;
    const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 1);
    EXPECT_EQ(batch.Vertices()[0].uv, glm::vec2(0.25f, 0.5f));
}

TEST(GameFloorItemDrawingTest, RedGlowSelectsUnlitMeshBeforeRenderingItsScroll)
{
    FloorModels models;
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    Client::World::FrameSprites sprites;
    Client::World::ObjectModelStepState modelState;
    modelState.unlitMeshes[0x2247] = -1;
    auto item = Item(0x2247);
    item.vertexLight = true;
    Client::Scenes::ItemSpecialDrawContext special;
    special.sprites = &sprites;
    Client::Scenes::ItemDrawContext items;
    items.special = &special;
    items.modelState = &modelState;
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.itemContext = &items;
    context.worldClock = 1000;
    context.project = Project;
    const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].uv.y, -0.25f);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 1);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.green, 1);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.blue, 1);
    EXPECT_EQ(modelState.unlitMeshes.at(0x2247), 0);
    EXPECT_FLOAT_EQ(item.objectState.uv.y, -0.25f);
}

TEST(GameFloorItemDrawingTest, RedGlowPublishesUnlitSelectionBeforeMissingBonesOrPool)
{
    for (const bool missingBones : {true, false})
    {
        SCOPED_TRACE(missingBones);
        FloorModels models;
        Client::Render::ModelBatch batch;
        Client::World::SphereFrustum frustum;
        Client::World::ObjectModelStepState modelState;
        modelState.unlitMeshes[0x2247] = -1;
        auto item = Item(0x2247);
        Client::Scenes::FaunaDrawCarry carry;
        Client::Scenes::ItemDrawContext items;
        items.modelState = &modelState;
        items.poseBones = [&]() -> std::span<const Client::Render::Models::BoneTransform>
        { return missingBones ? std::span<const Client::Render::Models::BoneTransform>{} : carry.bones; };
        Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
        context.itemContext = &items;
        context.worldClock = 1000;
        context.project = Project;
        context.onPose = [&](const Client::Scenes::DrawnObject& object)
        { return Client::Scenes::PublishFaunaFloorPose(carry, models, object); };
        context.onSkin = [&](const Client::Scenes::DrawnObject& object)
        { return Client::Scenes::TransformFaunaPose(carry, models, object); };
        context.posedMeshes = [&]() -> std::span<const Client::Render::Models::SkinnedMesh> { return carry.meshes; };
        const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find(missingBones ? "4ec110" : "5491e0"), std::string::npos);
        EXPECT_EQ(modelState.unlitMeshes.at(0x2247), 0);
        EXPECT_FLOAT_EQ(item.objectState.uv.y, -0.25f);
        EXPECT_TRUE(batch.Vertices().empty());
    }
}

TEST(GameFloorItemDrawingTest, IncompleteStaffScratchKeepsStoresAndFirstMeshThroughTheFloorAdapter)
{
    FloorModels models;
    models.model.meshes.resize(3, models.model.meshes[0]);
    for (std::size_t index = 0; index < models.model.meshes.size(); ++index)
        models.model.meshes[index].texture = static_cast<std::int16_t>(index);
    Client::Render::ModelBatch batch;
    Client::World::SphereFrustum frustum;
    Client::Scenes::FaunaDrawCarry carry;
    auto item = Item(0x61c);
    item.objectState.light = 0.25f;
    Client::Scenes::FloorItemDrawContext context{.batch = batch, .models = models, .frustum = frustum};
    context.project = Project;
    context.onPose = [&](const Client::Scenes::DrawnObject& object)
    { return Client::Scenes::PublishFaunaFloorPose(carry, models, object); };
    context.onSkin = [&](const Client::Scenes::DrawnObject& object)
    {
        const auto result = Client::Scenes::TransformFaunaPose(carry, models, object);
        carry.meshes.resize(1);
        return result;
    };
    context.posedMeshes = [&]() -> std::span<const Client::Render::Models::SkinnedMesh> { return carry.meshes; };
    const auto result = Client::Scenes::DrawFloorItems(std::span(&item, 1), context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("vértices/normales"), std::string::npos);
    EXPECT_FLOAT_EQ(item.objectState.light, 1);
    EXPECT_EQ(item.objectState.hiddenMesh, 2);
    EXPECT_EQ(item.objectState.blendMesh, 2);
    EXPECT_TRUE(item.seen);
    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 0);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 1);
}
