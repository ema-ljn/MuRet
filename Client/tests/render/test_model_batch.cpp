#include "Render/ModelBatch.h"

#include "Core/PixelSize.h"
#include "Render/ModelFog.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/TextureEnvMode.h"
#include "Render/TextureSlot.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int4.hpp>

#include <array>
#include <cstddef>
#include <optional>

// The triangles of a 3D world pass and their spans, in arrival order, like the 2D SpriteBatch (CL5.2).
namespace
{
using Client::Core::PixelSize;
using Client::Render::Camera3D;
using Client::Render::MakePass3D;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawCommand;
using Client::Render::ModelDrawState;
using Client::Render::ModelFog;
using Client::Render::ModelStateOperation;
using Client::Render::ModelVertex;
using Client::Render::Pass3D;
using Client::Render::TextureEnvMode;
using Client::Render::TextureSlot;
using Client::Render::ViewportRect;

constexpr TextureSlot SomeSlot{0x6f8};
constexpr TextureSlot OtherSlot{0x6f9};

Pass3D SomePass(ViewportRect rect = {0, 25, 640, 430})
{
    Camera3D camera;
    camera.farPlane = 2000.0f;
    return MakePass3D(PixelSize{1024, 768}, rect, camera);
}

// x identifies the triangle.
std::array<ModelVertex, 3> TriangleAt(float x)
{
    return {ModelVertex{{x, 0, 0}, {0, 0}, {}}, ModelVertex{{x, 1, 0}, {0, 1}, {}}, ModelVertex{{x, 0, 1}, {1, 0}, {}}};
}

TEST(ModelBatchTest, TrianglesWithTheSameStateShareACommand)
{
    ModelBatch batch;
    batch.Begin(SomePass());

    batch.SetState(ModelDrawState{SomeSlot, ModelBlend::Opaque, true});
    batch.Triangle(TriangleAt(1));
    batch.Triangle(TriangleAt(2));
    batch.SetState(ModelDrawState{SomeSlot, ModelBlend::Opaque, true});
    batch.Triangle(TriangleAt(3));

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].firstVertex, 0u);
    EXPECT_EQ(batch.Commands()[0].vertexCount, 9u);
    EXPECT_EQ(batch.Vertices().size(), 9u);
}

// Another texture, another blend or depth off make another span, and again another one even if the state is the
// earlier one: what comes after is drawn after.
TEST(ModelBatchTest, NeverReorders)
{
    ModelBatch batch;
    batch.Begin(SomePass());
    const ModelDrawState first{SomeSlot, ModelBlend::Opaque, true};
    const ModelDrawState states[] = {first,
                                     {OtherSlot, ModelBlend::Opaque, true},
                                     {SomeSlot, ModelBlend::Additive, true},
                                     {SomeSlot, ModelBlend::Opaque, false},
                                     {std::nullopt, ModelBlend::Alpha, true},
                                     first};
    float x = 0;
    for (const ModelDrawState& state : states)
    {
        batch.SetState(state);
        batch.Triangle(TriangleAt(x++));
    }

    ASSERT_EQ(batch.Commands().size(), std::size(states));
    for (std::size_t k = 0; k < std::size(states); ++k)
    {
        const ModelDrawCommand& command = batch.Commands()[k];
        EXPECT_EQ(command.state, states[k]) << k;
        EXPECT_EQ(command.firstVertex, 3 * k) << k;
        EXPECT_EQ(command.vertexCount, 3u) << k;
        EXPECT_FLOAT_EQ(batch.Vertices()[3 * k].position.x, static_cast<float>(k)) << k;
    }
}

TEST(ModelBatchTest, AStateWithoutTrianglesAddsNothing)
{
    ModelBatch batch;
    batch.Begin(SomePass());

    batch.SetState(ModelDrawState{SomeSlot, ModelBlend::Opaque, true});
    batch.SetState(ModelDrawState{OtherSlot, ModelBlend::Alpha, true});
    batch.Triangle(TriangleAt(0));

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.texture, OtherSlot);
}

TEST(ModelBatchTest, BeginEmptiesTheBatch)
{
    ModelBatch batch;
    batch.Begin(SomePass());
    batch.SetState(ModelDrawState{SomeSlot, ModelBlend::Opaque, true});
    batch.Triangle(TriangleAt(0));

    batch.Begin(SomePass(ViewportRect{0, 0, 640, 48}));
    batch.Triangle(TriangleAt(1));

    EXPECT_EQ(batch.Pass().viewport, glm::ivec4(0, 692, 1024, 76));
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Vertices().size(), 3u);
    // The state of a freshly begun pass: opaque, untextured and with depth.
    EXPECT_EQ(batch.Commands()[0].state, ModelDrawState{});
}

// The fog of 0x42b0a0 is not part of the material: each span carries the one in effect when drawn, turning it on or
// off opens another span with the same state, and a new state, even ModelDrawState{}, does not remove it.
TEST(ModelBatchTest, EachCommandCarriesTheFogOfItsTriangles)
{
    const ModelFog fog{.color = glm::vec3(0.0f), .start = 2000.0f, .end = 2700.0f};
    ModelBatch batch;
    batch.Begin(SomePass());
    batch.SetState(ModelDrawState{SomeSlot, ModelBlend::Opaque, true});
    batch.Triangle(TriangleAt(0));
    const auto serial = batch.StateSerial();
    batch.SetFog(fog);
    EXPECT_EQ(batch.StateSerial(), serial);
    batch.Triangle(TriangleAt(1));
    batch.SetState(ModelDrawState{});
    batch.Triangle(TriangleAt(2));
    batch.SetFog(std::nullopt);
    batch.Triangle(TriangleAt(3));

    ASSERT_EQ(batch.Commands().size(), 6u);
    EXPECT_EQ(batch.Commands()[0].fog, std::nullopt);
    EXPECT_EQ(batch.Commands()[1].fog, fog);
    EXPECT_EQ(batch.Commands()[1].state, batch.Commands()[0].state);
    EXPECT_EQ(batch.Commands()[2].fog, fog);
    EXPECT_EQ(batch.Commands()[3].fog, fog);
    EXPECT_EQ(batch.Commands()[4].fog, std::nullopt);
    EXPECT_EQ(batch.Commands()[4].vertexCount, 0u);
    EXPECT_EQ(batch.Commands()[5].fog, std::nullopt);
    EXPECT_EQ(batch.Commands()[5].firstVertex, 9u);
}

// 0x620db0 turns fog off (Begin with the material) and 0x6213e0 or a flush keep it (Begin without it). Operations and
// spans without triangles carry it too.
TEST(ModelBatchTest, OnlyAPassThatInitializesTheMaterialTurnsTheFogOff)
{
    const ModelFog fog{.color = glm::vec3(0.0f), .start = 2000.0f, .end = 2700.0f};
    ModelBatch batch;
    batch.Begin(SomePass());
    batch.SetFog(fog);
    batch.Begin(SomePass(), false);
    EXPECT_EQ(batch.Fog(), fog);
    batch.ApplyOperation(ModelDrawState{}, ModelStateOperation::Blend);
    batch.ApplyState(ModelDrawState{SomeSlot, ModelBlend::Alpha, true});
    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].fog, fog);
    EXPECT_EQ(batch.Commands()[1].fog, fog);

    batch.Begin(SomePass());
    EXPECT_EQ(batch.Fog(), std::nullopt);
}

// glTexEnvi is not part of the material either: the mode goes in each span, changing it opens another, and Begin does
// not touch it, with or without material.
TEST(ModelBatchTest, EachCommandCarriesTheTextureEnvOfItsTriangles)
{
    ModelBatch batch;
    batch.Begin(SomePass());
    batch.SetState(ModelDrawState{SomeSlot, ModelBlend::BlendedShadow, true});
    batch.Triangle(TriangleAt(0));
    batch.SetTextureEnv(TextureEnvMode::Add);
    batch.Triangle(TriangleAt(1));
    batch.Triangle(TriangleAt(2));
    batch.SetTextureEnv(TextureEnvMode::Modulate);
    batch.Triangle(TriangleAt(3));

    ASSERT_EQ(batch.Commands().size(), 5u);
    EXPECT_EQ(batch.Commands()[0].textureEnv, TextureEnvMode::Modulate);
    EXPECT_EQ(batch.Commands()[1].textureEnv, TextureEnvMode::Add);
    EXPECT_EQ(batch.Commands()[1].vertexCount, 0u);
    EXPECT_EQ(batch.Commands()[2].vertexCount, 6u);
    EXPECT_EQ(batch.Commands()[2].textureEnv, TextureEnvMode::Add);
    EXPECT_EQ(batch.Commands()[3].textureEnv, TextureEnvMode::Modulate);
    EXPECT_EQ(batch.Commands()[3].vertexCount, 0u);
    EXPECT_EQ(batch.Commands()[4].textureEnv, TextureEnvMode::Modulate);

    batch.SetTextureEnv(TextureEnvMode::Add);
    batch.Begin(SomePass());
    EXPECT_EQ(batch.TextureEnv(), TextureEnvMode::Add);
}

TEST(ModelBatchTest, TextureEnvChangesPublishWithoutGeometryOrMaterialSerialChanges)
{
    ModelBatch batch;
    batch.Begin(SomePass(), false);
    const auto serial = batch.StateSerial();
    batch.SetTextureEnv(TextureEnvMode::Modulate);
    EXPECT_TRUE(batch.Commands().empty());
    batch.SetTextureEnv(TextureEnvMode::Add);
    batch.SetTextureEnv(TextureEnvMode::Add);
    batch.SetTextureEnv(TextureEnvMode::Modulate);
    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].textureEnv, TextureEnvMode::Add);
    EXPECT_EQ(batch.Commands()[1].textureEnv, TextureEnvMode::Modulate);
    for (const auto& command : batch.Commands())
    {
        EXPECT_NE(command.operation, ModelStateOperation::Draw);
        EXPECT_EQ(command.vertexCount, 0u);
    }
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_EQ(batch.StateSerial(), serial);
}

TEST(ModelBatchTest, FogChangesPublishWithoutGeometryOrMaterialSerialChanges)
{
    const ModelFog fog{.color = {0.25f, 0.5f, 0.75f}, .start = 10, .end = 30};
    ModelBatch batch;
    batch.Begin(SomePass(), false);
    const auto serial = batch.StateSerial();
    batch.SetFog(std::nullopt);
    EXPECT_TRUE(batch.Commands().empty());
    batch.SetFog(fog);
    batch.SetFog(fog);
    batch.SetFog(std::nullopt);
    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].fog, fog);
    EXPECT_EQ(batch.Commands()[1].fog, std::nullopt);
    for (const auto& command : batch.Commands())
    {
        EXPECT_NE(command.operation, ModelStateOperation::Draw);
        EXPECT_EQ(command.vertexCount, 0u);
    }
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_EQ(batch.StateSerial(), serial);
}

} // namespace
