#include "Scenes/VertexEffects.h"

#include "Data/Models/Model.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Skinning.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "World/ModelEffects.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace
{
class VertexModels final : public Client::Scenes::IModelLibrary
{
public:
    VertexModels()
    {
        Client::Data::Models::Mesh mesh;
        mesh.triangles = {{3, {0, 0, 0, 0}}, {3, {0, 0, 0, 0}}};
        model.meshes.push_back(mesh);
    }
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return &model;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    Client::Data::Models::Model model;
};

TEST(VertexEffectsTest, RepeatedCornersConsumeThreeDrawsEachEvenWithAFullEffectPool)
{
    VertexModels models;
    const Client::Scenes::DrawnObject object;
    const std::array meshes{Client::Render::Models::SkinnedMesh{{glm::vec3{11, 22, 33}}, {}}};
    auto effects = std::make_unique<Client::World::ModelEffects>();
    for (auto& effect : effects->Effects())
        effect.alive = true;
    Client::Render::Models::CrtRandom random(1), expected(1);
    expected.Discard(18);
    const auto result = Client::Scenes::EmitVertexEffects(models, object, meshes, 0, 0x131, 0, *effects, random);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(random.State(), expected.State());
}

TEST(VertexEffectsTest, GateChildrenUseThePublishedScratchAndRunTheirConstructorBeforeTheNextCorner)
{
    VertexModels models;
    models.model.meshes[0].triangles.resize(1);
    models.model.meshes[0].triangles[0].polygon = 1;
    const Client::Scenes::DrawnObject object;
    const std::array meshes{Client::Render::Models::SkinnedMesh{{glm::vec3{11, 22, 33}}, {}}};
    auto effects = std::make_unique<Client::World::ModelEffects>();
    Client::Render::Models::CrtRandom random(2), expected(2);
    // CRT seed2:45%3=0; segundo sorteo elige12f/130; ocho del constructor.
    expected.Discard(10);
    const auto result = Client::Scenes::EmitVertexEffects(models, object, meshes, 0, 0x12f, 0, *effects, random);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(effects->Effects()[0].alive);
    EXPECT_EQ(effects->Effects()[0].transform.position, (glm::vec3{11, 22, 83}));
    EXPECT_EQ(effects->Effects()[0].light, glm::vec3(1));
    EXPECT_FALSE(effects->Effects()[1].alive);
    EXPECT_EQ(random.State(), expected.State());
}

TEST(VertexEffectsTest, AnInvalidMeshAndSignedEmptyPolygonsDoNotConsumeRandom)
{
    VertexModels models;
    for (auto& triangle : models.model.meshes[0].triangles)
        triangle.polygon = 0xff;
    const std::array meshes{Client::Render::Models::SkinnedMesh{}};
    auto effects = std::make_unique<Client::World::ModelEffects>();
    Client::Render::Models::CrtRandom random(1);
    const Client::Scenes::DrawnObject object;
    ASSERT_TRUE(Client::Scenes::EmitVertexEffects(models, object, meshes, -1, 0x131, 0, *effects, random).HasValue());
    ASSERT_TRUE(Client::Scenes::EmitVertexEffects(models, object, meshes, 0, 0x131, 0, *effects, random).HasValue());
    EXPECT_EQ(random.State(), 1u);
}

TEST(VertexEffectsTest, TheTwoProjectileFamiliesPreserveTheirDifferentChildSubtypes)
{
    VertexModels models;
    models.model.meshes[0].triangles.resize(1);
    models.model.meshes[0].triangles[0].polygon = 1;
    const Client::Scenes::DrawnObject object;
    const std::array meshes{Client::Render::Models::SkinnedMesh{{glm::vec3{11, 22, 33}}, {}}};
    for (const std::int32_t family : {0x12d, 0x12e})
    {
        auto effects = std::make_unique<Client::World::ModelEffects>();
        Client::Render::Models::CrtRandom random(2), expected(2);
        const auto result = Client::Scenes::EmitVertexEffects(models, object, meshes, 0, family, 0, *effects, random);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(effects->Effects()[0].alive);
        EXPECT_EQ(effects->Effects()[0].subtype, family == 0x12d ? 1 : 0);
        EXPECT_EQ(effects->Effects()[0].transform.position, (glm::vec3{11, 22, 83}));
        expected.Discard(10);
        EXPECT_EQ(random.State(), expected.State());
    }
}
} // namespace
