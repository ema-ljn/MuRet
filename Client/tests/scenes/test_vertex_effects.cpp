#include "Scenes/VertexEffects.h"

#include "Data/Models/Model.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Skinning.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "World/ModelEffects.h"
#include "World/SkinShellEffects.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
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
// 0x4eed94..0x4eee66: el 0x13c saca un rand() % 45 por esquina; con 0 y menos de 20 contados, el /0 crea el 0x13c/0
// (ocho rand() de su alta) y el /1 el 0x12d/2 y el 0x12e/2 (ocho cada uno), con los ángulos en 0 y la luz 1; otro
// subtipo solo cuenta. Desde los 20, solo el rand() de cada esquina.
TEST(VertexEffectsTest, TheGolemStonesComeOneInFortyFiveUpToTwenty)
{
    VertexModels models;
    models.model.meshes[0].triangles.assign(1500, {1, {0, 0, 0, 0}});
    const Client::Scenes::DrawnObject object;
    const std::array meshes{Client::Render::Models::SkinnedMesh{{glm::vec3{11, 22, 33}}, {}}};
    for (const std::int32_t subtype : {0, 1, 2})
    {
        SCOPED_TRACE(subtype);
        auto effects = std::make_unique<Client::World::ModelEffects>();
        Client::Render::Models::CrtRandom random(3), expected(3);
        int counted = 0;
        std::size_t made = 0;
        for (std::size_t corner = 0; corner < models.model.meshes[0].triangles.size(); ++corner)
        {
            if (expected.Next() % 45 != 0 || counted >= 20)
                continue;
            const int children = subtype == 0 ? 1 : (subtype == 1 ? 2 : 0);
            expected.Discard(static_cast<std::uint32_t>(8 * children));
            made += static_cast<std::size_t>(children);
            ++counted;
        }
        ASSERT_EQ(counted, 20);
        const auto result =
            Client::Scenes::EmitVertexEffects(models, object, meshes, 0, 0x13c, subtype, *effects, random);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(random.State(), expected.State());
        const auto live = static_cast<std::size_t>(
            std::ranges::count_if(effects->Effects(), [](const auto& effect) { return effect.alive; }));
        EXPECT_EQ(live, made);
        if (subtype == 0)
        {
            EXPECT_EQ(effects->Effects()[0].type, 0x13c);
            EXPECT_EQ(effects->Effects()[0].subtype, 0);
        }
        if (subtype == 1)
        {
            EXPECT_EQ(effects->Effects()[0].type, 0x12d);
            EXPECT_EQ(effects->Effects()[1].type, 0x12e);
            EXPECT_EQ(effects->Effects()[1].subtype, 2);
        }
        if (made > 0)
        {
            EXPECT_EQ(effects->Effects()[0].transform.position, (glm::vec3{11, 22, 83}));
            EXPECT_EQ(effects->Effects()[0].light, glm::vec3(1));
        }
    }
}

// 0x4eee6b..0x4eeec1: el 0x137 saca un rand() % 8 por esquina; con 0, la cáscara 0x137 del subtipo pedido (el del joint
// 0x632, también el 1) en la esquina, con los ángulos en 0 y la luz 1, y los nueve rand() de su alta. Con la tabla
// llena, sólo el de cada esquina.
TEST(VertexEffectsTest, TheSkinShellsComeOneInEightWithTheJointSubtype)
{
    VertexModels models;
    models.model.meshes[0].triangles.assign(40, {3, {0, 1, 2, 0}});
    const Client::Scenes::DrawnObject object;
    const std::array meshes{
        Client::Render::Models::SkinnedMesh{{glm::vec3{11, 22, 33}, glm::vec3{44, 55, 66}, glm::vec3{77, 88, 99}}, {}}};
    for (const std::int32_t subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        auto effects = std::make_unique<Client::World::ModelEffects>();
        auto twin = std::make_unique<Client::World::ModelEffects>();
        Client::Render::Models::CrtRandom random(4), expected(4);
        for (std::size_t corner = 0; corner < 3 * models.model.meshes[0].triangles.size(); ++corner)
        {
            if (expected.Next() % 8 != 0)
                continue;
            ASSERT_TRUE(Client::World::CreateSkinShell({.type = Client::World::SkinShellEffect,
                                                        .position = meshes[0].positions[corner % 3],
                                                        .angles = glm::vec3(0.0f),
                                                        .light = glm::vec3(1.0f),
                                                        .subtype = subtype},
                                                       *twin, expected)
                            .has_value());
        }
        const auto result = Client::Scenes::EmitVertexEffects(models, object, meshes, 0, Client::World::SkinShellEffect,
                                                              subtype, *effects, random);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(random.State(), expected.State());
        std::size_t made = 0;
        for (std::size_t slot = 0; slot < effects->Effects().size(); ++slot)
        {
            const auto& effect = effects->Effects()[slot];
            const auto& replica = twin->Effects()[slot];
            ASSERT_EQ(effect.alive, replica.alive);
            if (!effect.alive)
                continue;
            ++made;
            EXPECT_EQ(effect.type, Client::World::SkinShellEffect);
            EXPECT_EQ(effect.subtype, subtype);
            EXPECT_EQ(effect.transform.position, replica.transform.position);
            EXPECT_EQ(effect.transform.angles, replica.transform.angles);
            EXPECT_EQ(effect.velocity, replica.velocity);
            EXPECT_EQ(effect.light, replica.light);
        }
        EXPECT_GT(made, 0U);

        for (auto& effect : effects->Effects())
            effect.alive = true;
        Client::Render::Models::CrtRandom full(4), corners(4);
        corners.Discard(static_cast<std::uint32_t>(3 * models.model.meshes[0].triangles.size()));
        ASSERT_TRUE(Client::Scenes::EmitVertexEffects(models, object, meshes, 0, Client::World::SkinShellEffect,
                                                      subtype, *effects, full)
                        .HasValue());
        EXPECT_EQ(full.State(), corners.State());
    }
}
} // namespace
