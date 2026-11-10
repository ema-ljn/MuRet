#include "Scenes/KalimaObjectServices.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/Sprites.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
class KalimaModel final : public Client::Scenes::IModelLibrary
{
public:
    explicit KalimaModel(bool twoMeshes = false)
    {
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 100}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "test.jpg";
        m_model.meshes = {mesh};
        if (twoMeshes)
            m_model.meshes.push_back(mesh);
        Client::Render::Models::SkeletonBone bone;
        bone.parent = -1;
        Client::Render::Models::BoneKey key;
        key.rotation = Client::Render::Models::AngleQuaternion({0, 0, 0});
        bone.actions = {{key}};
        m_skeleton.bones = {bone};
        m_skeleton.actions = {{.repeats = true, .speed = 0, .keyCount = 1, .lockPositions = false}};
    }
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return &m_model;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return &m_skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return m_textures;
    }
    std::optional<Client::Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override
    {
        if (selector == Client::Scenes::ChromeTextureSlot.value)
            return Client::Scenes::ChromeTextureSlot;
        if (water && selector >= 65 && selector < 97)
            return Client::Render::TextureSlot{static_cast<std::uint16_t>(selector)};
        return std::nullopt;
    }
    bool water = true;

private:
    Client::Data::Models::Model m_model;
    Client::Render::Models::Skeleton m_skeleton;
    std::array<Client::Render::TextureSlot, 2> m_textures{Client::Render::TextureSlot{100},
                                                          Client::Render::TextureSlot{101}};
};

Client::World::WorldObject Object(std::int16_t type)
{
    Client::World::WorldObject object;
    object.type = type;
    object.transform.position = glm::vec3(100, 100, 0);
    object.light = glm::vec3(1);
    object.vertexLight = false;
    return object;
}

Client::Core::Expected<void, std::string> Draw(Client::Render::ModelBatch& batch, const KalimaModel& models,
                                               std::span<Client::World::WorldObject> objects,
                                               const Client::Scenes::GameObjectPass& pass)
{
    batch.Begin(Client::Render::Pass3D{});
    return Client::Scenes::DrawGameWorldObjects(batch, models, objects, {.everything = true}, nullptr, {}, pass);
}
} // namespace

TEST(KalimaObjectServicesTest, MissingCycleRetainsTheBodyBeforeRandomAndLeavesTheSuffixUnvisited)
{
    const KalimaModel models;
    Client::Render::Models::CrtRandom random(7);
    std::array objects{Object(0), Object(2)};
    Client::Render::ModelBatch batch;
    const auto result = Draw(batch, models, objects, {.map = 24, .random = &random});
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x439593"), std::string::npos);
    EXPECT_EQ(batch.Vertices().size(), 3u);
    EXPECT_TRUE(objects[0].seen);
    EXPECT_FALSE(objects[1].seen);
    EXPECT_EQ(random.State(), 7u);
}

TEST(KalimaObjectServicesTest, TheRealWaterFrameSelectsItsPhysicalTextureAndAddsTheAlphaBody)
{
    const KalimaModel models;
    Client::Render::Models::CrtRandom random(7);
    std::array objects{Object(0)};
    Client::Render::ModelBatch batch;
    const auto result = Draw(batch, models, objects, {.map = 36, .random = &random, .waterTextureFrame = 31});
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(batch.Vertices().size(), 6u);
    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[1].state.texture, Client::Render::TextureSlot{96});
    EXPECT_EQ(batch.Commands()[1].state.blend, Client::Render::ModelBlend::Alpha);
    EXPECT_FLOAT_EQ(batch.Vertices()[3].color.alpha, 0.3f);
    Client::Render::Models::CrtRandom expected(7);
    static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
}

TEST(KalimaObjectServicesTest, AProducerWithoutItsRealTextureStopsAfterTheFirstBody)
{
    KalimaModel models;
    models.water = false;
    std::array objects{Object(0)};
    Client::Render::ModelBatch batch;
    const auto result = Draw(batch, models, objects, {.map = 24, .originalFrame = false, .waterTextureFrame = 0});
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x4395ba"), std::string::npos);
    EXPECT_EQ(batch.Vertices().size(), 3u);
}

TEST(KalimaObjectServicesTest, AnExcludedTypeNeedsNoWaterAndIntermediateFramesConsumeNoRandom)
{
    const KalimaModel models;
    Client::Render::Models::CrtRandom random(7);
    std::array objects{Object(2)};
    Client::Render::ModelBatch batch;
    ASSERT_TRUE(Draw(batch, models, objects, {.map = 29, .originalFrame = false, .random = &random}).HasValue());
    EXPECT_EQ(batch.Vertices().size(), 3u);
    EXPECT_EQ(random.State(), 7u);
    const auto other = Draw(batch, models, objects, {.map = 30, .originalFrame = false});
    EXPECT_FALSE(other.HasValue());
}

TEST(KalimaObjectServicesTest, TheStepHidesExactlyThirtySevenThroughFortyAfterTheCommonAlphaGate)
{
    constexpr std::array<std::uint8_t, 7> Maps{24, 25, 26, 27, 28, 29, 36};
    for (const std::uint8_t map : Maps)
    {
        for (std::int16_t type = 36; type <= 41; ++type)
        {
            auto object = Object(type);
            Client::Render::Models::CrtRandom random(7);
            const auto result = Client::World::StepWorldObjectChecked(object, map, nullptr, random, {});
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(object.state.hiddenMesh, type >= 37 && type <= 40 ? -2 : -1);
            EXPECT_EQ(random.State(), 7u);
        }
    }
    auto faded = Object(37);
    faded.alpha = 0;
    faded.targetAlpha = 0;
    Client::Render::Models::CrtRandom random(7);
    ASSERT_TRUE(Client::World::StepWorldObjectChecked(faded, 24, nullptr, random, {}).HasValue());
    EXPECT_EQ(faded.state.hiddenMesh, -1);
}

TEST(KalimaObjectServicesTest, TheFlareUsesBoneTwentyAndDoesNotInventAnObjectOrigin)
{
    auto object = Object(12);
    Client::Scenes::FaunaDrawCarry carry;
    Client::World::FrameSprites sprites;
    Client::Render::Models::CrtRandom random(7);
    const auto result = Client::Scenes::StepKalimaObjectFrame(
        object, {.map = 24, .random = &random, .sprites = &sprites, .retainedPose = &carry});
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_TRUE(sprites.Sprites()[0].alive);
    EXPECT_EQ(sprites.Sprites()[0].position, glm::vec3(0));
    EXPECT_EQ(sprites.Sprites()[0].texture, 0x5ac);
    EXPECT_FLOAT_EQ(sprites.Sprites()[0].scale, 0.9f);
    EXPECT_EQ(sprites.Sprites()[0].light, glm::vec3(0.6f, 0.6f, 1));
    EXPECT_EQ(random.State(), 7u);
}

TEST(KalimaObjectServicesTest, TheLastMarkerKeepsItsGuardBeforeTheScaleStoreOrTheParticleBoundary)
{
    bool skipped = false;
    bool emitted = false;
    for (std::uint32_t seed = 0; seed < 12; ++seed)
    {
        auto object = Object(40);
        object.transform.scale = 7;
        Client::Render::Models::CrtRandom random(seed);
        auto expected = random;
        const bool creates = expected.Next() % 4 == 0;
        const auto result = Client::Scenes::StepKalimaObjectFrame(object, {.map = 24, .random = &random});
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(result.HasValue(), !creates);
        EXPECT_FLOAT_EQ(object.transform.scale, creates ? 7 : 0.5f);
        if (creates)
        {
            EXPECT_NE(result.Error().find("0x438f81"), std::string::npos);
            emitted = true;
        }
        else
            skipped = true;
    }
    EXPECT_TRUE(skipped);
    EXPECT_TRUE(emitted);
}

TEST(KalimaObjectServicesTest, TheThreeNativeShadowTypesEmitTheFlattenedSecondBody)
{
    const KalimaModel models;
    for (const std::int16_t type : {std::int16_t{15}, std::int16_t{29}, std::int16_t{32}})
    {
        std::array objects{Object(type)};
        objects[0].transform.position.z = 250;
        Client::Render::ModelBatch batch;
        const auto drawn = Draw(batch, models, objects, {.map = 24, .originalFrame = false});
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        ASSERT_EQ(batch.Vertices().size(), 6u);
        EXPECT_FLOAT_EQ(batch.Vertices()[3].position.z, 255);
        // X=100+1+(2000+1)*100/(100-4000), con un único store binario32 final.
        EXPECT_EQ(std::bit_cast<std::uint32_t>(batch.Vertices()[4].position.x), 0x4246c4ecu);
        EXPECT_FLOAT_EQ(batch.Vertices()[3].color.red, 0.1f);
        EXPECT_FLOAT_EQ(batch.Vertices()[3].color.alpha, 1);
    }
}

TEST(KalimaObjectServicesTest, TheThirtyFourthBodyAddsOnlyItsSecondChromeMeshAfterTheWater)
{
    const KalimaModel models(true);
    std::array objects{Object(34)};
    Client::Render::ModelBatch batch;
    const auto drawn = Draw(batch, models, objects, {.map = 24, .originalFrame = false, .waterTextureFrame = 0});
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(batch.Vertices().size(), 15u);
    ASSERT_FALSE(batch.Commands().empty());
    EXPECT_EQ(batch.Commands().back().state.texture, Client::Scenes::ChromeTextureSlot);
    EXPECT_EQ(batch.Commands().back().state.blend, Client::Render::ModelBlend::Additive);
    EXPECT_EQ(batch.Commands().back().vertexCount, 3u);
}
