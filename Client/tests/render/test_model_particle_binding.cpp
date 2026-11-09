#include "Render/ModelBatch.h"

#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>

namespace
{
// Quitar el espacio del estado fusionaría tres comandos y aplicaría view dos veces al central.
TEST(ModelParticleBindingTest, EyeAndWorldRemainSeparateCommandsWithTheSameMaterial)
{
    using namespace Client::Render;
    ModelBatch batch;
    batch.Begin(Pass3D{});
    const std::array<ModelVertex, 3> triangle{};
    ModelDrawState state{TextureSlot{100}, ModelBlend::Alpha, true};
    batch.SetState(state);
    batch.Triangle(triangle);
    state.space = ModelCoordinateSpace::Eye;
    batch.SetState(state);
    batch.Triangle(triangle);
    state.space = ModelCoordinateSpace::World;
    batch.SetState(state);
    batch.Triangle(triangle);
    ASSERT_EQ(batch.Commands().size(), 3u);
    EXPECT_EQ(batch.Commands()[0].state.space, ModelCoordinateSpace::World);
    EXPECT_EQ(batch.Commands()[1].state.space, ModelCoordinateSpace::Eye);
    EXPECT_EQ(batch.Commands()[2].state.space, ModelCoordinateSpace::World);
}

// Nombre físico0 es un binding explícito; ausencia y slot son representaciones diferentes.
TEST(ModelParticleBindingTest, RawNamesIncludeZeroAndRejectAnAmbiguousSlot)
{
    Client::Render::ModelDrawState state;
    EXPECT_TRUE(state.HasValidTextureBinding());
    state.rawTextureName = std::uint32_t{0};
    EXPECT_TRUE(state.HasValidTextureBinding());
    ASSERT_TRUE(state.rawTextureName.has_value());
    EXPECT_EQ(*state.rawTextureName, 0u);
    state.texture = Client::Render::TextureSlot{0};
    EXPECT_FALSE(state.HasValidTextureBinding());
    state.rawTextureName.reset();
    EXPECT_TRUE(state.HasValidTextureBinding());
}

TEST(ModelParticleBindingTest, StateSerialObservesEqualSetStateAndResourceInvalidationWithoutInventingAName)
{
    using namespace Client::Render;
    ModelBatch batch;
    batch.Begin(Pass3D{});
    const auto begun = batch.StateSerial();
    batch.SetState(ModelDrawState{});
    EXPECT_GT(batch.StateSerial(), begun);
    const auto first = batch.StateSerial();
    batch.SetState(ModelDrawState{});
    EXPECT_GT(batch.StateSerial(), first);
    batch.SetState({TextureSlot{100}, ModelBlend::Alpha, false});
    ASSERT_TRUE(batch.InvalidateTextureKnowledge().HasValue());
    EXPECT_FALSE(batch.State().texture.has_value());
    EXPECT_FALSE(batch.State().rawTextureName.has_value());
    EXPECT_EQ(batch.State().blend, ModelBlend::Alpha);
    EXPECT_FALSE(batch.State().depthTest);
}
} // namespace
