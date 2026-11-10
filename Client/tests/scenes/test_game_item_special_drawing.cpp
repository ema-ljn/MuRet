#include "Scenes/GameItemSpecialDrawing.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameModels.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"
#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Skinning.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "World/Sprites.h"
#include <gtest/gtest.h>
#include <glm/ext/vector_float3.hpp>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>
namespace
{
class SpecialModels final : public Client::Scenes::IModelLibrary
{
public:
    SpecialModels()
    {
        for (std::int16_t index = 0; index < 7; ++index)
        {
            Client::Data::Models::Mesh mesh;
            mesh.texture = index;
            mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
            mesh.normals = {{0, {0, 0, 1}, 0}};
            mesh.texCoords = {{0, 0}};
            mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
            mesh.textureName = "plain.jpg";
            model.meshes.push_back(mesh);
            meshes.push_back({{{static_cast<float>(index), 0, 0}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, 1}}});
        }
    }
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return available ? &model : nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return std::span(textures).first(textureCount);
    }
    bool available = true;
    std::size_t textureCount = 7;
    Client::Data::Models::Model model;
    std::vector<Client::Render::Models::SkinnedMesh> meshes;
    std::array<Client::Render::TextureSlot, 7> textures{
        Client::Render::TextureSlot{0x400}, Client::Render::TextureSlot{0x401}, Client::Render::TextureSlot{0x402},
        Client::Render::TextureSlot{0x403}, Client::Render::TextureSlot{0x404}, Client::Render::TextureSlot{0x405},
        Client::Render::TextureSlot{0x406}};
};
} // namespace
TEST(GameItemSpecialDrawingTest, DefaultUsesGeometryAndRejectsMissingModel)
{
    SpecialModels models;
    Client::Render::ModelBatch batch;
    Client::Scenes::DrawnObject object;
    object.model = 0x212;
    auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(batch.Vertices().size(), 21u);
    models.available = false;
    result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes);
    EXPECT_FALSE(result.HasValue());
}
TEST(GameItemSpecialDrawingTest, SwordAndCrossbowUseOrderedChromePasses)
{
    for (const auto type : {0x225, 0xc1c, 0xa24, 0x61f})
    {
        SCOPED_TRACE(type);
        SpecialModels models;
        Client::Render::ModelBatch batch;
        Client::Scenes::DrawnObject object;
        object.model = static_cast<std::uint16_t>(type);
        Client::Scenes::ItemSpecialDrawContext context;
        context.worldClock = 1000.0f;
        const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(batch.Vertices().size(), type == 0x61f ? 6u : 9u);
        EXPECT_EQ(batch.Commands()[1].state.texture, Client::Scenes::ChromeTextureSlot);
        EXPECT_EQ(batch.Commands()[1].state.blend, Client::Render::ModelBlend::Additive);
        EXPECT_FLOAT_EQ(batch.Vertices()[3].uv.y, type == 0x225 ? 0.1f : -0.1f);
        if (type != 0x61f)
        {
            EXPECT_FLOAT_EQ(batch.Vertices()[6].position.x, 1);
        }
    }
}
TEST(GameItemSpecialDrawingTest, StaffStoresAndRandomErrorKeepTheirPrefix)
{
    SpecialModels models;
    Client::Render::ModelBatch batch;
    Client::Scenes::DrawnObject object;
    object.model = 0x61c;
    Client::Scenes::ItemSpecialDrawContext context;
    context.worldClock = 1000.0f;
    context.publishedObject = &object;
    auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(object.state.hiddenMesh, 2);
    EXPECT_EQ(object.state.blendMesh, 2);
    EXPECT_FLOAT_EQ(object.state.light, 1);
    EXPECT_EQ(batch.Vertices().size(), 21u);
    EXPECT_FLOAT_EQ(batch.Vertices()[18].uv.x, 0.5f);
    batch = Client::Render::ModelBatch{};
    object = {};
    object.model = 0xe22;
    result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes);
    EXPECT_FALSE(result.HasValue());
    EXPECT_EQ(batch.Vertices().size(), 12u);
}
TEST(GameItemSpecialDrawingTest, PublicFamiliesHaveTheirIndependentPassCounts)
{
    struct Case
    {
        std::uint16_t type;
        std::size_t triangles;
    };
    const std::array cases{Case{0x1a18, 14}, Case{0xa25, 9},   Case{0x231, 5},   Case{0x81c, 8},   Case{0x619, 8},
                           Case{0x1c16, 14}, Case{0x1c17, 8},  Case{0x61a, 8},   Case{0x61b, 9},   Case{0x61c, 7},
                           Case{0x61d, 8},   Case{0x226, 10},  Case{0x227, 9},   Case{0xa27, 2},   Case{0xc1e, 2},
                           Case{0x620, 2},   Case{0x228, 2},   Case{0x229, 10},  Case{0xa26, 7},   Case{0x1e2e, 2},
                           Case{0x1e2f, 4},  Case{0xc1d, 10},  Case{0x61e, 9},   Case{0xe21, 5},   Case{0xe22, 5},
                           Case{0x1c1d, 2},  Case{0x1221, 14}, Case{0x1621, 14}, Case{0x1421, 14}, Case{0x1821, 14},
                           Case{0x1226, 14}, Case{0x1426, 14}, Case{0x1626, 14}, Case{0x1826, 14}, Case{0x1229, 14},
                           Case{0x1429, 14}, Case{0x1629, 14}, Case{0x1829, 14}};
    for (const auto test : cases)
    {
        SCOPED_TRACE(test.type);
        SpecialModels models;
        Client::Render::ModelBatch batch;
        Client::Scenes::DrawnObject object;
        object.model = test.type;
        Client::Render::Models::CrtRandom random;
        glm::vec2 pulse{0.5f, 0.01f};
        Client::Scenes::ItemSpecialDrawContext context;
        context.random = &random;
        context.pulse = &pulse;
        const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(batch.Vertices().size(), 3 * test.triangles);
    }
}

TEST(GameItemSpecialDrawingTest, FlagsHiddenSelectionAndSpecialArmorTakeTheirNativeBranches)
{
    struct Case
    {
        std::uint16_t type;
        std::uint32_t flags;
        std::int16_t hidden;
        std::uint32_t objectFlags;
        std::size_t vertices;
    };
    const std::array cases{Case{0xa15, 0x102, -1, 0, 21}, Case{0x1f7, 6, -1, 0, 27},     Case{0x1f7, 2, -1, 0, 21},
                           Case{0x1e19, 2, 0, 0, 9},      Case{0x1e19, 2, 1, 0, 9},      Case{0x1e19, 2, -1, 0, 0},
                           Case{0x1c19, 2, 0, 0, 3},      Case{0x1c19, 2, 1, 0, 3},      Case{0x1c19, 2, -1, 0, 0},
                           Case{0x101b, 2, -1, 0x102, 9}, Case{0x141b, 2, -1, 0x102, 9}, Case{0x161b, 2, -1, 0x102, 9},
                           Case{0x181b, 2, -1, 0x102, 9}, Case{0x101b, 2, -1, 0, 21}};
    for (const auto test : cases)
    {
        SCOPED_TRACE(test.type);
        SpecialModels models;
        Client::Render::ModelBatch batch;
        Client::Scenes::DrawnObject object;
        object.model = test.type;
        object.state.hiddenMesh = test.hidden;
        Client::Scenes::ItemSpecialDrawContext context;
        context.flags = test.flags;
        context.objectFlags = test.objectFlags;
        const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(batch.Vertices().size(), test.vertices);
    }
}
TEST(GameItemSpecialDrawingTest, HelmetUsesPrivateMatrixAndPublishesLightEvenWhenSpritePoolIsFull)
{
    SpecialModels models;
    Client::Render::ModelBatch batch;
    Client::Scenes::DrawnObject object;
    object.model = 0x121b;
    object.color = {0.2f, 0.3f, 0.4f};
    glm::vec3 light{};
    Client::World::FrameSprites sprites;
    for (std::size_t index = 0; index < Client::World::SpriteCount; ++index)
        ASSERT_TRUE(sprites.Create(1, {}, 1, {}, 0, 0));
    Client::Scenes::ItemSpecialDrawContext context;
    context.objectFlags = 0x102;
    context.objectLight = &light;
    context.sprites = &sprites;
    context.publishedObject = &object;
    int bones = 0;
    context.privateBoneOrigin = [&]() -> Client::Core::Expected<glm::vec3, std::string>
    {
        ++bones;
        return glm::vec3{1, 2, 3};
    };
    const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(bones, 1);
    EXPECT_EQ(batch.Vertices().size(), 9u);
    EXPECT_FLOAT_EQ(light.r, 0.9f * 0.3f);
    EXPECT_FLOAT_EQ(light.g, 0.9f * 0.5f);
    EXPECT_FLOAT_EQ(light.b, 0.9f * 0.8f);
    EXPECT_EQ(object.color, (glm::vec3{0.2f, 0.3f, 0.4f}));
}
TEST(GameItemSpecialDrawingTest, WingConsumesExactlyTwoSharedCrtDrawsInPushOrder)
{
    SpecialModels models;
    Client::Render::ModelBatch batch;
    Client::Scenes::DrawnObject object;
    object.model = 0xe22;
    Client::Render::Models::CrtRandom random(7);
    auto expected = random;
    const float v = static_cast<float>(expected.Next() % 10) * 0.1f;
    const float u = static_cast<float>(expected.Next() % 10) * 0.1f;
    Client::Scenes::ItemSpecialDrawContext context;
    context.random = &random;
    const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(batch.Vertices().size(), 15u);
    EXPECT_FLOAT_EQ(batch.Vertices()[12].uv.x, u);
    EXPECT_FLOAT_EQ(batch.Vertices()[12].uv.y, v);
}
TEST(GameItemSpecialDrawingTest, OrbPulseUpdatesAfterItsPassAndMissingPulsePreservesPrefix)
{
    SpecialModels models;
    Client::Render::ModelBatch batch;
    Client::Scenes::DrawnObject object;
    object.model = 0x1e2f;
    Client::Scenes::ItemSpecialDrawContext context;
    context.publishedObject = &object;
    auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
    EXPECT_FALSE(result.HasValue());
    EXPECT_EQ(batch.Vertices().size(), 3u);
    EXPECT_EQ(object.color, (glm::vec3{1, 0.5f, 0}));
    glm::vec2 pulse{1.2f, 0.01f};
    context.pulse = &pulse;
    result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FLOAT_EQ(pulse.x, 0.99f);
    EXPECT_FLOAT_EQ(pulse.y, -0.01f);
    // 60a4ff..60a50a: el último 0x44 recibe alpha.3 y deja sus FSTP sobre model RGB.
    EXPECT_EQ(object.color, glm::vec3(0.3f));
    EXPECT_FLOAT_EQ(batch.Vertices().back().color.red, 0.3f);
    EXPECT_FLOAT_EQ(object.alpha, 1);
}
namespace
{
class GoldenGameItemSpecialDrawing : public Client::Tests::GoldenDataTest
{
};
} // namespace
TEST_F(GoldenGameItemSpecialDrawing, ActualItemResourcesProvideGeometryForSpecialPasses)
{
    Client::Tests::FakeTextureStore textures;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Client::Scenes::GameModels models;
    const auto loaded = models.Load(m_dataRoot, textures, log);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    for (const auto type : {0x225,  0xc1c,  0xa24, 0x61f, 0xa25, 0x231, 0x81c,  0x619,  0x1c16, 0x1c17, 0x61a,
                            0x61b,  0x61c,  0x61d, 0x226, 0x227, 0xa27, 0xc1e,  0x620,  0x228,  0x229,  0xa26,
                            0x1e2e, 0x1e2f, 0xc1d, 0x61e, 0xe21, 0xe22, 0x1221, 0x1426, 0x1629})
    {
        SCOPED_TRACE(type);
        Client::Scenes::DrawnObject object;
        object.model = static_cast<std::uint16_t>(type);
        Client::Scenes::FaunaDrawCarry carry;
        const auto posed = Client::Scenes::PublishFaunaFloorPose(carry, models, object);
        ASSERT_TRUE(posed.HasValue()) << posed.Error();
        Client::Render::ModelBatch batch;
        Client::Render::Models::CrtRandom random;
        glm::vec2 pulse{0.5f, 0.01f};
        Client::Scenes::ItemSpecialDrawContext context;
        context.random = &random;
        context.pulse = &pulse;
        context.worldClock = 1234.5f;
        const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, carry.meshes, context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_FALSE(batch.Vertices().empty());
    }
}

TEST(GameItemSpecialDrawingTest, PulsingModelColorRoundsOnlyAtTheAsmStore)
{
    struct Case
    {
        std::uint16_t model;
        std::uint32_t bits;
    };
    // Evaluación independiente PC53 de sin(0.5 * F32(0.001)), constantes del PE;
    // el RGB no tiene un spill intermedio antes de multiplicarlo por el rojo inicial.
    for (const auto test : {Case{0x619, 0x3df5f6fe}, Case{0x231, 0x3e4ce704}, Case{0xe21, 0x3e75d789}})
    {
        SCOPED_TRACE(test.model);
        SpecialModels models;
        Client::Render::ModelBatch batch;
        Client::Scenes::DrawnObject object;
        object.model = test.model;
        object.color = {0.4f, 0.6f, 0.8f};
        Client::Render::Models::CrtRandom random;
        Client::Scenes::ItemSpecialDrawContext context;
        context.worldClock = 0.5f;
        context.random = &random;
        context.publishedObject = &object;
        const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(std::bit_cast<std::uint32_t>(object.color.r), test.bits);
        EXPECT_EQ(object.color.r, object.color.g);
        EXPECT_EQ(object.color.g, object.color.b);
    }
}

TEST(GameItemSpecialDrawingTest, UnorderedOrbPulseUsesTheNativeLowerClamp)
{
    SpecialModels models;
    Client::Render::ModelBatch batch;
    Client::Scenes::DrawnObject object;
    object.model = 0x1e2f;
    glm::vec2 pulse{std::numeric_limits<float>::quiet_NaN(), -1};
    Client::Scenes::ItemSpecialDrawContext context;
    context.pulse = &pulse;
    const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FLOAT_EQ(pulse.x, 0.02f);
    EXPECT_FLOAT_EQ(pulse.y, 0.01f);
}

TEST(GameItemSpecialDrawingTest, HelmetRejectsGlobalPoseWhenThePrivateMatrixIsMissing)
{
    SpecialModels models;
    Client::Render::ModelBatch batch;
    Client::Scenes::DrawnObject object;
    object.model = 0x121b;
    glm::vec3 light{};
    Client::World::FrameSprites sprites;
    Client::Scenes::ItemSpecialDrawContext context;
    context.objectFlags = 0x102;
    context.objectLight = &light;
    context.sprites = &sprites;
    context.bonePoint = [](std::size_t) -> Client::Core::Expected<glm::vec3, std::string> { return glm::vec3{}; };
    const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
    EXPECT_FALSE(result.HasValue());
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_FALSE(sprites.Any());
}

TEST(GameItemSpecialDrawingTest, EveryMeshPassUsesTheExplicitNativeWorldClock)
{
    SpecialModels models;
    Client::Render::ModelBatch batch;
    Client::Scenes::DrawnObject object;
    object.model = 0xa25;
    Client::Scenes::ItemSpecialDrawContext context;
    context.worldClock = 1000.5f;
    const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_GE(batch.Vertices().size(), 6u);
    // 4ecc30 trunca el reloj para t=(T%10000)*F32(.0001), con normal(0,0,1).
    EXPECT_FLOAT_EQ(batch.Vertices()[3].uv.x, 0.6f);
    EXPECT_FLOAT_EQ(batch.Vertices()[3].uv.y, 0.2f);
}

TEST(GameItemSpecialDrawingTest, MissingSpritesKeepThePrivateOriginAndFirstLightStore)
{
    SpecialModels models;
    Client::Render::ModelBatch batch;
    Client::Scenes::DrawnObject object;
    object.model = 0x121b;
    glm::vec3 light{};
    int origins = 0;
    Client::Scenes::ItemSpecialDrawContext context;
    context.objectFlags = 0x102;
    context.objectLight = &light;
    context.privateBoneOrigin = [&]() -> Client::Core::Expected<glm::vec3, std::string>
    {
        ++origins;
        return glm::vec3{};
    };
    const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
    EXPECT_FALSE(result.HasValue());
    EXPECT_EQ(origins, 1);
    EXPECT_EQ(light, (glm::vec3{0.4f, 0.6f, 0.8f}));
    EXPECT_TRUE(batch.Vertices().empty());
}

TEST(GameItemSpecialDrawingTest, LoadedZeroMeshEffectTakesTheDefaultWithoutColorStores)
{
    SpecialModels models;
    models.model.meshes.clear();
    models.meshes.clear();
    Client::Render::ModelBatch batch;
    Client::Scenes::DrawnObject object;
    object.model = 0x1f7;
    object.color = {0.2f, 0.3f, 0.4f};
    Client::Scenes::ItemSpecialDrawContext context;
    context.flags = 6;
    context.publishedObject = &object;
    const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
    EXPECT_TRUE(result.HasValue());
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_EQ(object.color, (glm::vec3{0.2f, 0.3f, 0.4f}));
}

TEST(GameItemSpecialDrawingTest, LongRunningClockUsesTheNativeLowWordInSpecialModuloPasses)
{
    struct Clock
    {
        float time;
        std::int32_t lowWord;
    };
    const std::array clocks{Clock{0x1p32f, 0}, Clock{0x1p31f, std::numeric_limits<std::int32_t>::min()},
                            Clock{0x1p32f + 1024, 1024}};
    for (const auto clock : clocks)
        for (const auto type : {0x228, 0xc1e, 0xa27})
        {
            SCOPED_TRACE(clock.time);
            SCOPED_TRACE(type);
            SpecialModels models;
            Client::Render::ModelBatch batch;
            Client::Scenes::DrawnObject object;
            object.model = static_cast<std::uint16_t>(type);
            Client::Scenes::ItemSpecialDrawContext context;
            context.worldClock = clock.time;
            const auto result =
                Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            ASSERT_EQ(batch.Vertices().size(), 6u);
            const auto remainder = clock.lowWord % (type == 0xc1e ? 2000 : 4000);
            const float expected = type == 0x228 ? static_cast<float>(static_cast<double>(remainder) * 0.0004f - 0.7f)
                                                 : static_cast<float>(static_cast<double>(remainder) * 0.0002f - 0.3f);
            EXPECT_FLOAT_EQ(batch.Vertices()[3].uv.x, expected);
        }
}

TEST(GameItemSpecialDrawingTest, LongRunningBowChromeUsesTheNativeSignedWordPeriod)
{
    SpecialModels models;
    Client::Render::ModelBatch batch;
    Client::Scenes::DrawnObject object;
    object.model = 0xa25;
    Client::Scenes::ItemSpecialDrawContext context;
    context.worldClock = 0x1p32f;
    const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_GE(batch.Vertices().size(), 6u);
    EXPECT_FLOAT_EQ(batch.Vertices()[3].uv.x, 0.5f);
    EXPECT_FLOAT_EQ(batch.Vertices()[3].uv.y, 0);
}
TEST(GameItemSpecialDrawingTest, ChromeFourKeepsTheFractionalNativeClockForItsSine)
{
    SpecialModels models;
    Client::Render::ModelBatch batch;
    Client::Scenes::DrawnObject object;
    object.model = 0xc1d;
    Client::Scenes::ItemSpecialDrawContext context;
    context.worldClock = 1234.5f;
    const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(batch.Vertices().size(), 30u);
    // F32(1 + 3*F32(sin(1234.5*F32(.002)))), con normal(0,0,1).
    EXPECT_EQ(std::bit_cast<std::uint32_t>(batch.Vertices()[27].uv.x), 0x40379e7cu);
}

TEST(GameItemSpecialDrawingTest, ShieldRetainsAdditiveRgbAcrossLayersWithoutStoringTheirParameters)
{
    SpecialModels models;
    models.model.meshes.resize(2);
    models.meshes.resize(2);
    Client::Render::ModelBatch batch;
    Client::Scenes::DrawnObject object;
    object.model = 0xc1d;
    object.alpha = 0.5f;
    object.state.light = 0.75f;
    object.state.uv = {0.125f, 0.25f};
    Client::Scenes::ItemSpecialDrawContext context;
    context.worldClock = 100.0f;
    context.publishedObject = &object;
    const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(batch.Vertices().size(), 15u);
    EXPECT_FLOAT_EQ(batch.Vertices()[6].color.red, 0.5f);
    EXPECT_FLOAT_EQ(batch.Vertices()[9].color.red, 0.5f * 0.47942555f);
    EXPECT_FLOAT_EQ(batch.Vertices()[12].color.red, 0.25f);
    EXPECT_EQ(object.color, glm::vec3(0.25f));
    EXPECT_FLOAT_EQ(object.alpha, 0.5f);
    EXPECT_EQ(object.state.blendMesh, -1);
    EXPECT_FLOAT_EQ(object.state.light, 0.75f);
    EXPECT_EQ(object.state.uv, (glm::vec2{0.125f, 0.25f}));
}

TEST(GameItemSpecialDrawingTest, ShieldPublishesAttenuatedRgbBeforeALaterLayerFails)
{
    SpecialModels models;
    models.model.meshes.resize(2);
    models.meshes.resize(2);
    models.meshes[0].positions.clear();
    Client::Render::ModelBatch batch;
    Client::Scenes::DrawnObject object;
    object.model = 0xc1d;
    object.alpha = 0.5f;
    object.state.hiddenMesh = 0;
    Client::Scenes::ItemSpecialDrawContext context;
    context.worldClock = 100.0f;
    context.publishedObject = &object;
    const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("vértices/normales"), std::string::npos);
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_FLOAT_EQ(batch.Vertices()[3].color.red, 0.5f);
    EXPECT_EQ(object.color, glm::vec3(0.5f));
    EXPECT_EQ(object.state.blendMesh, -1);
}

TEST(GameItemSpecialDrawingTest, MissingSecondMeshResourceKeepsFirstGeometryAndRgbStore)
{
    for (const bool missingTexture : {true, false})
    {
        SCOPED_TRACE(missingTexture);
        SpecialModels models;
        models.model.meshes.resize(2);
        if (missingTexture)
            models.textureCount = 1;
        else
            models.meshes.resize(1);
        Client::Render::ModelBatch batch;
        Client::Scenes::DrawnObject object;
        object.model = 0x212;
        object.alpha = 0.5f;
        Client::Scenes::ItemSpecialDrawContext context;
        context.flags = 0x44;
        context.publishedObject = &object;
        const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find(missingTexture ? "textura" : "vértices/normales"), std::string::npos);
        EXPECT_EQ(batch.Vertices().size(), 3u);
        if (batch.Vertices().size() == 3)
        {
            EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 0);
            EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.5f);
        }
        EXPECT_EQ(object.color, glm::vec3(0.5f));
    }
}

TEST(GameItemSpecialDrawingTest, StaffStoresPrecedeIncompleteScratchAndKeepFirstMesh)
{
    SpecialModels models;
    models.meshes.resize(1);
    Client::Render::ModelBatch batch;
    Client::Scenes::DrawnObject object;
    object.model = 0x61c;
    object.state.light = 0.25f;
    Client::Scenes::ItemSpecialDrawContext context;
    context.publishedObject = &object;
    const auto result = Client::Scenes::DrawSpecialItemMeshes(batch, models, object, {}, models.meshes, context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("vértices/normales"), std::string::npos);
    EXPECT_FLOAT_EQ(object.state.light, 1);
    EXPECT_EQ(object.state.hiddenMesh, 2);
    EXPECT_EQ(object.state.blendMesh, 2);
    ASSERT_EQ(batch.Vertices().size(), 3u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 0);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 1);
}
