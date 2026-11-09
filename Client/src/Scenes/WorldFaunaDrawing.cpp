#include "Scenes/WorldFaunaDrawing.h"

#include "Core/Expected.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ILeafDrawingServices.h"
#include "World/Leaves.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <system_error>

namespace Client::Scenes
{
Core::Expected<void, std::error_code> DrawWorldLeaves(Render::ModelBatch& batch, World::Leaves& leaves,
                                                      const std::int32_t& map, const std::int32_t& selector,
                                                      ILeafDrawingServices* services)
{
    if (services == nullptr)
        return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
    const auto blend = services->Blend(World::LeafBlendOf(map));
    if (!blend.HasValue())
        return Core::Unexpected(blend.Error());
    const auto white = services->WhiteColor();
    if (!white.HasValue())
        return Core::Unexpected(white.Error());
    for (World::Leaf& leaf : leaves.All())
    {
        if (leaf.alive == 0)
            continue;
        const auto bound = services->BindTexture(leaf.type);
        if (!bound.HasValue())
            return Core::Unexpected(bound.Error());
        const std::int32_t routeMap = map;
        if (routeMap == 2)
        {
            const float scale = leaf.scale;
            const std::int32_t drawSelector = leaf.type;
            const LeafBillboardRequest request{drawSelector, leaf.position, scale, scale, leaf.light};
            const auto billboard = services->Billboard(batch, request);
            if (!billboard.HasValue())
                return Core::Unexpected(billboard.Error());
            continue;
        }
        const auto pushed = services->PushMatrix();
        if (!pushed.HasValue())
            return Core::Unexpected(pushed.Error());
        // Las tres lecturas son posteriores aPush, en orden Z/Y/X como53076e..530775.
        const float z = leaf.position.z;
        const float y = leaf.position.y;
        const float x = leaf.position.x;
        const glm::vec3 translation{x, y, z};
        const auto translated = services->Translate(x, y, z);
        if (!translated.HasValue())
            return Core::Unexpected(translated.Error());
        const Render::Models::OriginalMatrix matrix = Render::Models::OriginalAngleMatrix(leaf.angles);
        const std::int32_t lateMap = map;
        const std::int32_t lateSelector = lateMap == 34 && leaf.type == 102 ? selector : 0;
        const World::LeafGeometry geometry = World::LeafGeometryOf(leaf, lateMap, lateSelector);
        if (geometry.kind == World::LeafGeometryKind::Quad)
        {
            const World::LeafQuad quad = World::LeafQuadOf(translation, matrix, geometry.dimensions);
            const auto drawn = services->Quad(batch, quad);
            if (!drawn.HasValue())
                return Core::Unexpected(drawn.Error());
        }
        const auto popped = services->PopMatrix();
        if (!popped.HasValue())
            return Core::Unexpected(popped.Error());
    }
    return {};
}
} // namespace Client::Scenes
