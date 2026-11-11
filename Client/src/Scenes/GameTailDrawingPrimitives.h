#pragma once

#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include <array>

// Primitivas compartidas por las familias de la cola6374fe..637572.
namespace Client::Scenes::TailDrawing
{
inline void Blend(Render::ModelBatch& batch, Render::ModelBlend blend)
{
    auto state = batch.State();
    state.blend = blend;
    state.cullEnabled = false;
    state.depthWrite = blend == Render::ModelBlend::Alpha;
    state.alphaTest = blend == Render::ModelBlend::Alpha;
    state.texture2d = true;
    constexpr std::array operations{Render::ModelStateOperation::Blend, Render::ModelStateOperation::Cull,
                                    Render::ModelStateOperation::DepthWrite, Render::ModelStateOperation::AlphaTest,
                                    Render::ModelStateOperation::Texture2d};
    for (const auto operation : operations)
        batch.ApplyOperation(state, operation);
}
inline void Quad(Render::ModelBatch& batch, const std::array<Render::ModelVertex, 4>& vertices)
{
    batch.Triangle({vertices[0], vertices[1], vertices[2]});
    batch.Triangle({vertices[0], vertices[2], vertices[3]});
}
inline float F32(double value)
{
    return static_cast<float>(value);
}
} // namespace Client::Scenes::TailDrawing
