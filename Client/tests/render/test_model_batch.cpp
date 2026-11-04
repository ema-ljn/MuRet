#include "Render/ModelBatch.h"

#include "App/WindowState.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_int4.hpp>

#include <array>
#include <cstddef>
#include <optional>

// Client::Render::ModelBatch: los triángulos de un pase 3D en el mundo y sus tramos, en el orden en
// que llegan, como el SpriteBatch del 2D (CL5.2).
namespace
{
using Client::App::PixelSize;
using Client::Render::Camera3D;
using Client::Render::MakePass3D;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawCommand;
using Client::Render::ModelDrawState;
using Client::Render::ModelVertex;
using Client::Render::Pass3D;
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

// Un triángulo cualquiera con la x en `x`, para reconocerlo.
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

// Otra textura, otra mezcla o la profundidad apagada son otro tramo, y vuelven a ser otro aunque el
// estado sea el de antes: lo que va después se dibuja después.
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

// Un estado sin triángulos no deja un tramo vacío.
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
    // El estado vuelve al de un pase recién empezado: opaco, sin textura y con profundidad.
    EXPECT_EQ(batch.Commands()[0].state, ModelDrawState{});
}
} // namespace
