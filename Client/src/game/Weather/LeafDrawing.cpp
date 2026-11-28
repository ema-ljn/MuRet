#include "Weather/LeafDrawing.h"

#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Weather/Leaves.h"

#include <cstddef>
#include <cstdint>

namespace Client::Weather
{
void DrawLeaves(Render::ModelBatch& batch, const Leaves& leaves)
{
    const Render::Color white{1.0f, 1.0f, 1.0f, 1.0f};
    for (const Leaf& leaf : leaves.All())
    {
        if (!leaf.alive)
        {
            continue;
        }
        const LeafQuad quad = LeafQuadOf(leaf);
        batch.SetState(Render::ModelDrawState{.texture = Render::TextureSlot{static_cast<std::uint16_t>(leaf.type)},
                                              .blend = Render::ModelBlend::Additive});
        // glTranslatef and the vertex: GL does the sum, in float.
        const auto vertex = [&quad, &white](std::size_t k)
        { return Render::ModelVertex{quad.translation + quad.corners[k], quad.uv[k], white}; };
        batch.Triangle({vertex(0), vertex(1), vertex(2)});
        batch.Triangle({vertex(0), vertex(2), vertex(3)});
    }
}
} // namespace Client::Weather
